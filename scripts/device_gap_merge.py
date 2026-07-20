"""
Device-level gap merge + Derived Gap DB (optimized)
====================================================
- Aggregates host API data once, not per-gap
- Uses binary search for gap-to-stream projection
- Outputs SQLite derived gap DB
"""
import sqlite3
import os
import json
import sys
import bisect

DB_DIR = r"D:\vllm-hust-perf-analyzer\examples\kickstart_smoke\msprof_raw"

PRODUCTIVE_TASK_NAMES = {"KERNEL_AIVEC", "KERNEL_AICORE", "KERNEL_MIX_AIC", "KERNEL_MIX_AIV", "MEMCPY_ASYNC"}
WAIT_TASK_NAMES = {"EVENT_WAIT", "NOTIFY_WAIT"}
CAPTURE_TASK_NAMES = {"CAPTURE_WAIT", "CAPTURE_RECORD", "MEM_WRITE_VALUE"}
RECORD_TASK_NAMES = {"EVENT_RECORD", "NOTIFY_RECORD", "STARS_COMMON"}
CONTROL_TASK_NAMES = {"MODEL_MAINTAINCE", "MODEL_EXECUTE", "TASK_TIMEOUT_SET", "PROFILING_ENABLE"}

MIN_GAP_NS = 100_000
PHASE_CHANGE_NS = 1_000_000_000
SMALL_BATCH_NS = 1_000_000_000  # 1s batch for query efficiency


def log(msg):
    print(msg, flush=True)


def load_string_map(cur):
    cur.execute("SELECT id, value FROM STRING_IDS")
    return {str(r[0]): r[1] for r in cur.fetchall()}


def classify_tasks(cur, str_map):
    """Load and classify all TASK rows."""
    cur.execute("""
        SELECT DISTINCT t.globalTaskId
        FROM TASK t
        JOIN COMMUNICATION_TASK_INFO cti ON t.globalTaskId = cti.globalTaskId
    """)
    comm_ids = {r[0] for r in cur.fetchall()}

    cur.execute("""
        SELECT globalTaskId, streamId, taskType, startNs, endNs, connectionId
        FROM TASK ORDER BY startNs
    """)

    tasks = []
    for r in cur.fetchall():
        tid, sid, ttype, s, e, cid = r
        tname = str_map.get(str(ttype), f"ID:{ttype}")
        if tid in comm_ids:
            tclass = "comm"
        elif tname in PRODUCTIVE_TASK_NAMES:
            tclass = "productive_compute"
        elif tname in WAIT_TASK_NAMES:
            tclass = "wait"
        elif tname in CAPTURE_TASK_NAMES:
            tclass = "capture"
        elif tname in RECORD_TASK_NAMES:
            tclass = "record"
        elif tname in CONTROL_TASK_NAMES:
            tclass = "control"
        else:
            tclass = "unknown"
        tasks.append({"id": tid, "stream": sid, "type": ttype, "tname": tname,
                       "start": s, "end": e, "conn": cid, "class": tclass})
    return tasks


def merge_intervals(intervals):
    if not intervals:
        return []
    intervals.sort(key=lambda x: x[0])
    merged = [list(intervals[0])]
    for s, e in intervals[1:]:
        if s <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], e)
        else:
            merged.append([s, e])
    return merged


def build_device_busy(tasks):
    productive = [(t["start"], t["end"]) for t in tasks
                  if t["class"] in ("productive_compute", "comm")]
    return merge_intervals(productive)


def compute_device_gaps(busy, t_min, t_max):
    gaps = []
    gid = 0
    # leading
    if busy and busy[0][0] > t_min:
        dur = busy[0][0] - t_min
        if dur >= MIN_GAP_NS:
            gaps.append({"gap_id": gid, "startNs": t_min, "endNs": busy[0][0],
                         "durNs": dur, "position": "leading"}); gid += 1
    # middle
    for i in range(len(busy) - 1):
        gs, ge = busy[i][1], busy[i + 1][0]
        dur = ge - gs
        if dur >= MIN_GAP_NS:
            gaps.append({"gap_id": gid, "startNs": gs, "endNs": ge,
                         "durNs": dur, "position": "middle"}); gid += 1
    # trailing
    if busy and busy[-1][1] < t_max:
        dur = t_max - busy[-1][1]
        if dur >= MIN_GAP_NS:
            gaps.append({"gap_id": gid, "startNs": busy[-1][1], "endNs": t_max,
                         "durNs": dur, "position": "trailing"}); gid += 1
    return gaps


def build_stream_task_index(tasks):
    """Build a per-stream list of (start, end, class) for efficient overlap query."""
    streams = {}
    for t in tasks:
        sid = t["stream"]
        if sid not in streams:
            streams[sid] = {"starts": [], "ends": [], "classes": [], "tasks": []}
        streams[sid]["starts"].append(t["start"])
        streams[sid]["ends"].append(t["end"])
        streams[sid]["classes"].append(t["class"])
        streams[sid]["tasks"].append(t)
    return streams


def overlap_classes(stream_data, gap_start, gap_end):
    """Find all classes that overlap with [gap_start, gap_end] via binary search."""
    classes = set()
    starts, ends = stream_data["starts"], stream_data["ends"]
    if not starts:
        return classes
    # Find first task that ends after gap_start, and last task that starts before gap_end
    idx = bisect.bisect_right(ends, gap_start)
    end_idx = len(starts)
    for i in range(idx, end_idx):
        if starts[i] >= gap_end:
            break
        classes.add(stream_data["classes"][i])
    return classes


def label_device_gap(gap, stream_data):
    """Label a device gap by checking overlapping classes on all streams."""
    dur = gap["durNs"]

    # Phase change: huge gap
    if dur >= PHASE_CHANGE_NS:
        has_productive = False
        for sid, sd in stream_data.items():
            cls = overlap_classes(sd, gap["startNs"], gap["endNs"])
            if "productive_compute" in cls or "comm" in cls:
                has_productive = True
                break
        if not has_productive:
            return ("phase_change_boundary", "heuristic",
                    "Gap > 1s with no productive work")

    # Collect all classes across all streams
    all_classes = set()
    wait_streams = []
    for sid, sd in stream_data.items():
        cls = overlap_classes(sd, gap["startNs"], gap["endNs"])
        all_classes.update(cls)
        if "wait" in cls:
            wait_streams.append(sid)

    if "wait" in all_classes:
        return ("blocked_by_visible_wait", "confirmed",
                f"Stream(s) {wait_streams} have visible wait tasks")
    if "capture" in all_classes:
        return ("capture_control_present", "heuristic",
                "CAPTURE/control tasks present")
    if "record" in all_classes:
        return ("runtime_control_present", "heuristic",
                "Record tasks present")
    if "control" in all_classes:
        return ("runtime_control_present", "heuristic",
                "Model maintenance/control tasks present")
    if "productive_compute" in all_classes or "comm" in all_classes:
        return ("queued_visible_task_delay", "contextual",
                "Productive work ongoing but delay at device level")
    return ("no_observed_device_work", "unknown",
            "No visible task on any stream")


def build_stream_gap_events(tasks, db_idx, device_id):
    """Stream-local gaps between consecutive tasks on the same stream."""
    streams = {}
    for t in tasks:
        streams.setdefault(t["stream"], []).append(t)

    gaps = []
    gid = 0
    for sid, stasks in streams.items():
        stasks.sort(key=lambda t: t["start"])
        for i in range(len(stasks) - 1):
            gs, ge = stasks[i]["end"], stasks[i + 1]["start"]
            dur = ge - gs
            if dur >= MIN_GAP_NS:
                prev, nxt = stasks[i], stasks[i + 1]
                gaps.append({
                    "gap_id": gid, "db_idx": db_idx, "device_id": device_id,
                    "streamId": sid, "startNs": gs, "endNs": ge, "durNs": dur,
                    "gap_type": "stream_local_gap",
                    "prev_taskType": prev["tname"], "prev_class": prev["class"],
                    "next_taskType": nxt["tname"], "next_class": nxt["class"],
                    "prev_globalTaskId": prev["id"], "next_globalTaskId": nxt["id"],
                })
                gid += 1
    return gaps


def build_stream_state_timeline(tasks, db_idx, device_id):
    """Per-stream observable state intervals (RFC Layer 2)."""
    class_to_state = {
        "productive_compute": "running_compute", "comm": "running_comm",
        "wait": "running_wait", "capture": "running_capture_control",
        "record": "running_record", "control": "running_runtime_control",
        "unknown": "unknown",
    }
    intervals = []
    for i, t in enumerate(tasks):
        state = class_to_state.get(t["class"], "unknown")
        intervals.append({
            "state_id": i, "db_idx": db_idx, "device_id": device_id,
            "streamId": t["stream"], "startNs": t["start"], "endNs": t["end"],
            "durNs": t["end"] - t["start"], "state": state,
            "source_event_id": t["id"], "taskType": t["tname"],
            "confidence": "confirmed",
        })
    return intervals


def process_device(db_path, db_idx, device_id):
    conn = sqlite3.connect(db_path)
    cur = conn.cursor()

    str_map = load_string_map(cur)
    tasks = classify_tasks(cur, str_map)

    t_min = tasks[0]["start"]
    t_max = tasks[-1]["end"]
    total_dur_s = (t_max - t_min) / 1e9

    # Class counts
    cc = {}
    for t in tasks:
        cc[t["class"]] = cc.get(t["class"], 0) + 1
    log(f"  Device {device_id}: {len(tasks):,} tasks | {total_dur_s:.1f}s | classes: {cc}")

    # ---- Step 1: Device busy timeline ----
    busy = build_device_busy(tasks)
    productive_s = sum(e - s for s, e in busy) / 1e9
    log(f"    Busy intervals: {len(busy)} | Productive: {productive_s:.1f}s ({productive_s/total_dur_s*100:.1f}%)")

    # ---- Step 2: Device gaps ----
    device_gaps = compute_device_gaps(busy, t_min, t_max)
    log(f"    Device gaps (>{MIN_GAP_NS/1000:.0f}us): {len(device_gaps)}")

    # ---- Build stream index for fast overlap queries ----
    stream_data = build_stream_task_index(tasks)
    log(f"    Streams: {len(stream_data)}")

    # ---- Step 3: Label each device gap ----
    device_gap_events = []
    for g in device_gaps:
        cat, conf, reason = label_device_gap(g, stream_data)

        # Build stream state summary
        stream_summary = {}
        for sid, sd in stream_data.items():
            cls = overlap_classes(sd, g["startNs"], g["endNs"])
            if cls:
                stream_summary[str(sid)] = list(cls)

        device_gap_events.append({
            "event_id": f"D{db_idx}_DEV{device_id}_GAP{g['gap_id']}",
            "db_idx": db_idx, "device_id": device_id,
            "startNs": g["startNs"], "endNs": g["endNs"],
            "durNs": g["durNs"], "durMs": round(g["durNs"] / 1e6, 3),
            "gap_type": "device_visible_gap",
            "category": cat, "confidence": conf, "reason": reason,
            "position": g["position"],
            "stream_states": json.dumps(stream_summary),
            "phase_boundary": 1 if cat == "phase_change_boundary" else 0,
        })

    # Category summary
    cat_counts = {}
    for g in device_gap_events:
        cat_counts[g["category"]] = cat_counts.get(g["category"], 0) + 1
    log(f"    Labeled device gaps:")
    for cat, cnt in sorted(cat_counts.items(), key=lambda x: -x[1]):
        log(f"      {cat}: {cnt}")

    # ---- Step 4: Stream-local gaps ----
    stream_gaps = build_stream_gap_events(tasks, db_idx, device_id)
    log(f"    Stream-local gaps: {len(stream_gaps)}")

    # ---- Step 5: Stream state timeline ----
    state_intervals = build_stream_state_timeline(tasks, db_idx, device_id)
    log(f"    Stream state intervals: {len(state_intervals)}")

    conn.close()
    return {
        "device_gap_events": device_gap_events,
        "stream_gap_events": stream_gaps,
        "stream_state_intervals": state_intervals,
        "busy_intervals": busy,
        "task_count": len(tasks),
        "productive_ratio": round(productive_s / total_dur_s * 100, 1),
        "total_dur_s": round(total_dur_s, 1),
    }


def write_output_db(all_results, output_path):
    conn = sqlite3.connect(output_path)
    cur = conn.cursor()

    cur.execute("DROP TABLE IF EXISTS traceloom_device_gap")
    cur.execute("DROP TABLE IF EXISTS traceloom_stream_gap")
    cur.execute("DROP TABLE IF EXISTS traceloom_stream_state")
    cur.execute("DROP TABLE IF EXISTS traceloom_device_interval")
    cur.execute("DROP VIEW IF EXISTS traceloom_v_device_gap_summary")
    cur.execute("DROP VIEW IF EXISTS traceloom_v_stream_gap_summary")
    cur.execute("DROP VIEW IF EXISTS traceloom_v_device_idle_hotspot")

    cur.execute("""
        CREATE TABLE traceloom_device_gap (
            event_id TEXT PRIMARY KEY, db_idx INTEGER, device_id INTEGER,
            startNs INTEGER, endNs INTEGER, durNs INTEGER, durMs REAL,
            gap_type TEXT, category TEXT, confidence TEXT,
            reason TEXT, position TEXT,
            stream_states TEXT, phase_boundary INTEGER
        )
    """)
    cur.execute("""
        CREATE TABLE traceloom_stream_gap (
            gap_id INTEGER, db_idx INTEGER, device_id INTEGER,
            streamId INTEGER, startNs INTEGER, endNs INTEGER, durNs INTEGER,
            gap_type TEXT, prev_taskType TEXT, prev_class TEXT,
            next_taskType TEXT, next_class TEXT,
            prev_globalTaskId INTEGER, next_globalTaskId INTEGER,
            PRIMARY KEY (db_idx, device_id, gap_id)
        )
    """)
    cur.execute("""
        CREATE TABLE traceloom_stream_state (
            state_id INTEGER, db_idx INTEGER, device_id INTEGER,
            streamId INTEGER, startNs INTEGER, endNs INTEGER, durNs INTEGER,
            state TEXT, source_event_id INTEGER, taskType TEXT, confidence TEXT,
            PRIMARY KEY (db_idx, device_id, state_id)
        )
    """)
    cur.execute("""
        CREATE TABLE traceloom_device_interval (
            interval_id INTEGER PRIMARY KEY AUTOINCREMENT,
            db_idx INTEGER, device_id INTEGER,
            startNs INTEGER, endNs INTEGER, durNs INTEGER,
            interval_kind TEXT
        )
    """)

    for r in all_results:
        db_idx = r["device_gap_events"][0]["db_idx"] if r["device_gap_events"] else 0
        dev_id = r["device_gap_events"][0]["device_id"] if r["device_gap_events"] else 0

        for g in r["device_gap_events"]:
            cur.execute("""INSERT OR REPLACE INTO traceloom_device_gap VALUES
                (?,?,?,?,?,?,?,?,?,?,?,?,?,?)""",
                (g["event_id"], g["db_idx"], g["device_id"], g["startNs"], g["endNs"],
                 g["durNs"], g["durMs"], g["gap_type"], g["category"], g["confidence"],
                 g["reason"], g["position"], g["stream_states"], g["phase_boundary"]))

        for g in r["stream_gap_events"]:
            cur.execute("""INSERT OR REPLACE INTO traceloom_stream_gap VALUES
                (?,?,?,?,?,?,?,?,?,?,?,?,?,?)""",
                (g["gap_id"], g["db_idx"], g["device_id"], g["streamId"],
                 g["startNs"], g["endNs"], g["durNs"], g["gap_type"],
                 g["prev_taskType"], g["prev_class"], g["next_taskType"], g["next_class"],
                 g["prev_globalTaskId"], g["next_globalTaskId"]))

        for s in r["stream_state_intervals"]:
            cur.execute("""INSERT OR REPLACE INTO traceloom_stream_state VALUES
                (?,?,?,?,?,?,?,?,?,?,?)""",
                (s["state_id"], s["db_idx"], s["device_id"], s["streamId"],
                 s["startNs"], s["endNs"], s["durNs"], s["state"],
                 s["source_event_id"], s["taskType"], s["confidence"]))

        for s, e in r["busy_intervals"]:
            cur.execute("""INSERT INTO traceloom_device_interval
                (db_idx, device_id, startNs, endNs, durNs, interval_kind)
                VALUES (?,?,?,?,?,'productive_active')""",
                (db_idx, dev_id, s, e, e - s))

    # Views
    cur.execute("""
        CREATE VIEW traceloom_v_device_gap_summary AS
        SELECT db_idx, device_id, category, confidence,
               COUNT(*) as gap_count, SUM(durMs) as total_ms,
               AVG(durMs) as avg_ms, SUM(phase_boundary) as phase_boundary_count
        FROM traceloom_device_gap
        GROUP BY db_idx, device_id, category, confidence
        ORDER BY total_ms DESC
    """)
    cur.execute("""
        CREATE VIEW traceloom_v_stream_gap_summary AS
        SELECT db_idx, device_id, prev_class, next_class,
               COUNT(*) as gap_count, SUM(durNs)/1e6 as total_ms,
               AVG(durNs)/1000.0 as avg_us
        FROM traceloom_stream_gap
        GROUP BY db_idx, device_id, prev_class, next_class
        ORDER BY total_ms DESC
    """)
    cur.execute("""
        CREATE VIEW traceloom_v_device_idle_hotspot AS
        SELECT * FROM traceloom_device_gap
        WHERE phase_boundary = 0
        ORDER BY durNs DESC LIMIT 30
    """)
    cur.execute("""
        CREATE VIEW traceloom_v_device_gap_as_interval AS
        SELECT event_id AS interval_id, db_idx, device_id,
               startNs, endNs, durNs,
               'visible_productive_idle' AS interval_kind,
               category, confidence
        FROM traceloom_device_gap
    """)

    conn.commit()
    conn.close()
    log(f"\nDerived gap DB written to: {output_path}")


def main():
    db_files = []
    for root, dirs, files in os.walk(DB_DIR):
        for f in files:
            if f.endswith(".db") and f.startswith("msprof"):
                db_files.append(os.path.join(root, f))
    db_files.sort()

    log(f"Found {len(db_files)} msprof DB(s)\n")

    all_results = []
    for db_idx, db_path in enumerate(db_files):
        rel = os.path.relpath(db_path, DB_DIR)
        log(f"[{db_idx}] {rel}")
        result = process_device(db_path, db_idx, db_idx)
        result["db_path"] = rel
        all_results.append(result)
        log("")

    # Cross-device comparison
    log("=" * 60)
    log("Cross-Device Summary")
    log("=" * 60)
    for r in all_results:
        log(f"\n  {r['db_path']}:")
        log(f"    Tasks: {r['task_count']:,} | Productive: {r['productive_ratio']}% | "
            f"Duration: {r['total_dur_s']}s")
        log(f"    Device gaps: {len(r['device_gap_events'])} | Stream gaps: {len(r['stream_gap_events'])}")

        cat_totals = {}
        for g in r["device_gap_events"]:
            cat_totals[g["category"]] = cat_totals.get(g["category"], 0.0) + g["durMs"]
        log(f"    Gap by category (total duration):")
        for cat, dur in sorted(cat_totals.items(), key=lambda x: -x[1])[:8]:
            log(f"      {cat:<40} {dur:>12,.1f}ms")

    # Write output
    output_dir = os.path.join(DB_DIR, "traceloom_gap_db")
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, "derived_gaps.db")
    write_output_db(all_results, output_path)

    # Summary JSON
    summary = {}
    for r in all_results:
        key = r["db_path"]
        summary[key] = {
            "task_count": r["task_count"],
            "productive_ratio": r["productive_ratio"],
            "duration_s": r["total_dur_s"],
            "device_gap_count": len(r["device_gap_events"]),
            "stream_gap_count": len(r["stream_gap_events"]),
            "busy_interval_count": len(r["busy_intervals"]),
            "gap_categories": {},
        }
        for g in r["device_gap_events"]:
            cat = g["category"]
            if cat not in summary[key]["gap_categories"]:
                summary[key]["gap_categories"][cat] = {"count": 0, "total_ms": 0.0}
            summary[key]["gap_categories"][cat]["count"] += 1
            summary[key]["gap_categories"][cat]["total_ms"] += round(g["durMs"], 3)
    with open(os.path.join(output_dir, "summary.json"), "w") as f:
        json.dump(summary, f, indent=2)

    log(f"\nOutput directory: {output_dir}")
    log("Done.")


if __name__ == "__main__":
    main()
