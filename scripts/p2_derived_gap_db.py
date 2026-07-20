"""
Task 2: Derived Gap DB with Clear Event Labels
===============================================
Produces a derived gap database that:
1. Has an EVENT_LABEL_DEFINITIONS table with clear semantics
2. Has unified GAP_EVENT table (stream-local + device-visible + device-non-productive-interval)
3. Has GAP_EVENT_EVIDENCE table linking gaps back to original TASK/CANN_API rows
4. Has views for common queries
5. Is directly JOINable with the original msprof DBs
"""
import sqlite3
import os
import json
import sys
import bisect

DB_DIR = r"D:\vllm-hust-perf-analyzer\examples\kickstart_smoke\msprof_raw"

PRODUCTIVE_NAMES = {"KERNEL_AIVEC", "KERNEL_AICORE", "KERNEL_MIX_AIC", "KERNEL_MIX_AIV", "MEMCPY_ASYNC"}
WAIT_NAMES = {"EVENT_WAIT", "NOTIFY_WAIT"}
CAPTURE_NAMES = {"CAPTURE_WAIT", "CAPTURE_RECORD", "MEM_WRITE_VALUE"}
RECORD_NAMES = {"EVENT_RECORD", "NOTIFY_RECORD", "STARS_COMMON"}
CONTROL_NAMES = {"MODEL_MAINTAINCE", "MODEL_EXECUTE", "TASK_TIMEOUT_SET", "PROFILING_ENABLE"}

MIN_GAP_NS = 100_000
PHASE_CHANGE_NS = 1_000_000_000


def log(msg):
    print(msg, flush=True)


def load_string_map(cur):
    cur.execute("SELECT id, value FROM STRING_IDS")
    return {str(r[0]): r[1] for r in cur.fetchall()}


def classify_task(globalTaskId, taskType, str_map, comm_ids):
    tname = str_map.get(str(taskType), f"ID:{taskType}")
    if globalTaskId in comm_ids:
        return "comm", tname
    if tname in PRODUCTIVE_NAMES:
        return "productive_compute", tname
    if tname in WAIT_NAMES:
        return "wait", tname
    if tname in CAPTURE_NAMES:
        return "capture", tname
    if tname in RECORD_NAMES:
        return "record", tname
    if tname in CONTROL_NAMES:
        return "control", tname
    return "unknown", tname


def load_tasks(cur, str_map):
    cur.execute("SELECT DISTINCT t.globalTaskId FROM TASK t JOIN COMMUNICATION_TASK_INFO cti ON t.globalTaskId = cti.globalTaskId")
    comm_ids = {r[0] for r in cur.fetchall()}

    cur.execute("SELECT globalTaskId, streamId, taskType, startNs, endNs, connectionId FROM TASK ORDER BY startNs")
    tasks = []
    for r in cur.fetchall():
        tid, sid, ttype, s, e, cid = r
        tclass, tname = classify_task(tid, ttype, str_map, comm_ids)
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


def build_stream_task_index(tasks):
    streams = {}
    for t in tasks:
        sid = t["stream"]
        if sid not in streams:
            streams[sid] = {"starts": [], "ends": [], "classes": [], "tnames": [], "tasks": []}
        streams[sid]["starts"].append(t["start"])
        streams[sid]["ends"].append(t["end"])
        streams[sid]["classes"].append(t["class"])
        streams[sid]["tnames"].append(t["tname"])
        streams[sid]["tasks"].append(t)
    return streams


def overlap_info(stream_data, gap_start, gap_end):
    """Get all class and task info for a stream overlapping [gap_start, gap_end]."""
    classes, tnames, task_ids = set(), set(), set()
    starts, ends = stream_data["starts"], stream_data["ends"]
    if not starts:
        return classes, tnames, task_ids
    idx = bisect.bisect_right(ends, gap_start)
    for i in range(idx, len(starts)):
        if starts[i] >= gap_end:
            break
        classes.add(stream_data["classes"][i])
        tnames.add(stream_data["tnames"][i])
        task_ids.add(stream_data["tasks"][i]["id"])
    return classes, tnames, task_ids


def label_device_gap(gap, stream_data):
    dur = gap["durNs"]
    if dur >= PHASE_CHANGE_NS:
        all_classes = set()
        for sid, sd in stream_data.items():
            all_classes.update(overlap_info(sd, gap["startNs"], gap["endNs"])[0])
        if "productive_compute" not in all_classes and "comm" not in all_classes:
            return ("device_non_productive_interval", "phase_boundary", "heuristic",
                    "Gap > 1s with no productive work — stage/phase boundary")

    wait_streams, capture_streams, record_streams = [], [], []
    all_classes = set()
    for sid, sd in stream_data.items():
        cls, _, _ = overlap_info(sd, gap["startNs"], gap["endNs"])
        all_classes.update(cls)
        if "wait" in cls:
            wait_streams.append(sid)
        if "capture" in cls:
            capture_streams.append(sid)
        if "record" in cls:
            record_streams.append(sid)

    if wait_streams:
        return ("device_visible_gap", "blocked_by_visible_wait", "confirmed",
                f"Stream(s) {wait_streams} have EVENT_WAIT/NOTIFY_WAIT tasks")
    if capture_streams:
        return ("device_visible_gap", "capture_control_present", "heuristic",
                f"Stream(s) {capture_streams} have CAPTURE_WAIT/control tasks")
    if record_streams or "control" in all_classes:
        return ("device_visible_gap", "runtime_control_present", "heuristic",
                "Model maintenance or record tasks present")
    if "productive_compute" in all_classes or "comm" in all_classes:
        return ("device_visible_gap", "queued_visible_task_delay", "contextual",
                "Productive work exists but gap at device level")
    return ("device_visible_gap", "no_observed_device_work", "unknown",
            "No observed task activity on any stream")


def process_device(db_path, db_idx, device_id):
    conn = sqlite3.connect(db_path)
    str_map = load_string_map(conn.cursor())
    tasks = load_tasks(conn.cursor(), str_map)
    t_min, t_max = tasks[0]["start"], tasks[-1]["end"]

    # --- Productive timeline ---
    productive = [(t["start"], t["end"]) for t in tasks if t["class"] in ("productive_compute", "comm")]
    busy = merge_intervals(productive)

    # --- Build stream index ---
    stream_data = build_stream_task_index(tasks)

    # --- Device gaps ---
    device_gaps = []
    gid = 0
    if busy and busy[0][0] > t_min and busy[0][0] - t_min >= MIN_GAP_NS:
        device_gaps.append({"gap_id": gid, "startNs": t_min, "endNs": busy[0][0],
                            "durNs": busy[0][0] - t_min, "position": "leading"}); gid += 1
    for i in range(len(busy) - 1):
        gs, ge = busy[i][1], busy[i + 1][0]
        if ge - gs >= MIN_GAP_NS:
            device_gaps.append({"gap_id": gid, "startNs": gs, "endNs": ge,
                                "durNs": ge - gs, "position": "middle"}); gid += 1
    if busy and t_max - busy[-1][1] >= MIN_GAP_NS:
        device_gaps.append({"gap_id": gid, "startNs": busy[-1][1], "endNs": t_max,
                            "durNs": t_max - busy[-1][1], "position": "trailing"}); gid += 1

    # --- Stream-local gaps ---
    stream_gaps = []
    sgid = 0
    for sid, stasks in stream_data.items():
        st = sorted(stasks["tasks"], key=lambda t: t["start"])
        for i in range(len(st) - 1):
            gs, ge = st[i]["end"], st[i + 1]["start"]
            dur = ge - gs
            if dur >= MIN_GAP_NS:
                prev, nxt = st[i], st[i + 1]
                # Label based on transition pattern
                if prev["class"] == "record" and nxt["class"] == "wait":
                    sub_category = "event_synchronization_boundary"
                elif prev["class"] == "wait" and nxt["class"] == "productive_compute":
                    sub_category = "wait_to_compute_transition"
                elif prev["class"] == "productive_compute" and nxt["class"] == "productive_compute":
                    sub_category = "intra_kernel_launch_gap"
                elif prev["class"] == "control" and nxt["class"] == "control":
                    sub_category = "model_maintenance_interval"
                elif "capture" in (prev["class"], nxt["class"]):
                    sub_category = "capture_control_boundary"
                elif "record" in (prev["class"], nxt["class"]):
                    sub_category = "event_record_boundary"
                else:
                    sub_category = "other_transition"

                stream_gaps.append({
                    "gap_id": sgid, "db_idx": db_idx, "device_id": device_id,
                    "streamId": sid, "startNs": gs, "endNs": ge, "durNs": dur,
                    "gap_type": "stream_local_gap", "sub_category": sub_category,
                    "prev_taskType": prev["tname"], "prev_class": prev["class"],
                    "next_taskType": nxt["tname"], "next_class": nxt["class"],
                    "prev_globalTaskId": prev["id"], "next_globalTaskId": nxt["id"],
                }); sgid += 1

    # --- Label device gaps ---
    device_gap_events = []
    for g in device_gaps:
        gap_type, category, confidence, reason = label_device_gap(g, stream_data)
        # Build stream state evidence
        evidence_streams = {}
        for sid, sd in stream_data.items():
            cls, tnames, tids = overlap_info(sd, g["startNs"], g["endNs"])
            if cls:
                evidence_streams[str(sid)] = {"states": list(cls), "task_types": list(tnames),
                                               "task_ids": list(tids)}

        device_gap_events.append({
            "event_id": f"D{db_idx}_DEV{device_id}_{gap_type}_{g['gap_id']}",
            "db_idx": db_idx, "device_id": device_id,
            "startNs": g["startNs"], "endNs": g["endNs"],
            "durNs": g["durNs"], "durMs": round(g["durNs"] / 1e6, 3),
            "gap_type": gap_type, "category": category,
            "confidence": confidence, "reason": reason,
            "position": g["position"],
            "evidence_streams": json.dumps(evidence_streams),
        })

    conn.close()
    return {
        "device_gap_events": device_gap_events,
        "stream_gap_events": stream_gaps,
        "busy_intervals": [(s, e, e - s) for s, e in busy],
        "t_min": t_min, "t_max": t_max,
        "task_count": len(tasks),
    }


def write_output_db(all_results, output_path):
    conn = sqlite3.connect(output_path)
    cur = conn.cursor()

    # Drop old tables
    for tbl in ["traceloom_gap_event", "traceloom_gap_event_evidence",
                "traceloom_event_label_definition", "traceloom_device_interval",
                "traceloom_v_unified_gap_timeline", "traceloom_v_gap_summary_by_category",
                "traceloom_v_gap_hotspot"]:
        cur.execute(f"DROP TABLE IF EXISTS {tbl}")
        cur.execute(f"DROP VIEW IF EXISTS {tbl}")

    # ==== Table 1: Event Label Definitions ====
    cur.execute("""
        CREATE TABLE traceloom_event_label_definition (
            label_key TEXT PRIMARY KEY,
            gap_type TEXT NOT NULL,
            category TEXT NOT NULL,
            confidence TEXT NOT NULL,
            description TEXT,
            is_actionable INTEGER DEFAULT 0,
            display_priority INTEGER DEFAULT 50
        )
    """)

    labels = [
        # Stream-local gap labels
        ("stream_local_gap.event_synchronization_boundary", "stream_local_gap",
         "event_synchronization_boundary", "confirmed",
         "Stream-local gap between EVENT_RECORD and EVENT_WAIT — cross-stream sync signal", 1, 10),
        ("stream_local_gap.intra_kernel_launch_gap", "stream_local_gap",
         "intra_kernel_launch_gap", "contextual",
         "Stream-local gap between two KERNEL tasks — host dispatch delay", 1, 20),
        ("stream_local_gap.wait_to_compute_transition", "stream_local_gap",
         "wait_to_compute_transition", "confirmed",
         "Stream-local gap from EVENT_WAIT/NOTIFY_WAIT to KERNEL — wait ends, compute begins", 1, 15),
        ("stream_local_gap.model_maintenance_interval", "stream_local_gap",
         "model_maintenance_interval", "heuristic",
         "Consecutive MODEL_MAINTAINCE tasks — profiler marker interval, not runtime work", 0, 80),
        ("stream_local_gap.capture_control_boundary", "stream_local_gap",
         "capture_control_boundary", "heuristic",
         "Gap involving CAPTURE_WAIT/CAPTURE_RECORD — ACL graph capture phase", 0, 70),
        ("stream_local_gap.event_record_boundary", "stream_local_gap",
         "event_record_boundary", "heuristic",
         "Gap involving EVENT_RECORD/NOTIFY_RECORD — event lifecycle boundary", 0, 60),
        ("stream_local_gap.other_transition", "stream_local_gap",
         "other_transition", "unknown",
         "Unclassified stream-local gap transition", 0, 90),

        # Device-visible gap labels
        ("device_visible_gap.blocked_by_visible_wait", "device_visible_gap",
         "blocked_by_visible_wait", "confirmed",
         "Device-level gap where one+ streams have visible EVENT_WAIT/NOTIFY_WAIT tasks", 1, 5),
        ("device_visible_gap.queued_visible_task_delay", "device_visible_gap",
         "queued_visible_task_delay", "contextual",
         "Device-level gap where productive tasks exist but are delayed (host dispatch bottleneck)", 1, 10),
        ("device_visible_gap.host_sync_api_present", "device_visible_gap",
         "host_sync_api_present", "contextual",
         "Device-level gap with overlapping host synchronization API (aclrtSynchronizeStream etc.)", 1, 15),
        ("device_visible_gap.capture_control_present", "device_visible_gap",
         "capture_control_present", "heuristic",
         "Device-level gap with CAPTURE/control tasks on some streams — may be ACL graph overhead", 0, 50),
        ("device_visible_gap.runtime_control_present", "device_visible_gap",
         "runtime_control_present", "heuristic",
         "Device-level gap with runtime control/model maintenance tasks", 0, 55),
        ("device_visible_gap.no_observed_device_work", "device_visible_gap",
         "no_observed_device_work", "unknown",
         "Device-level gap with no visible task on any stream — profiler blind spot or true idle", 0, 90),

        # Device non-productive interval labels
        ("device_non_productive_interval.phase_boundary", "device_non_productive_interval",
         "phase_boundary", "heuristic",
         "Gap > 1s with no productive work — capture/replay/init stage boundary", 0, 5),
        ("device_non_productive_interval.init_phase", "device_non_productive_interval",
         "init_phase", "heuristic",
         "Leading gap before first productive task — engine initialization phase", 0, 10),
    ]
    cur.executemany("INSERT INTO traceloom_event_label_definition VALUES (?,?,?,?,?,?,?)", labels)

    # ==== Table 2: Unified Gap Event Table ====
    cur.execute("""
        CREATE TABLE traceloom_gap_event (
            event_id TEXT PRIMARY KEY,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            startNs INTEGER NOT NULL,
            endNs INTEGER NOT NULL,
            durNs INTEGER NOT NULL,
            durMs REAL NOT NULL,
            gap_type TEXT NOT NULL,
            category TEXT NOT NULL,
            sub_category TEXT,
            confidence TEXT NOT NULL,
            reason TEXT,
            position TEXT,
            streamId INTEGER,
            evidence_json TEXT,
            FOREIGN KEY (gap_type, category) REFERENCES traceloom_event_label_definition(gap_type, category)
        )
    """)

    # Insert device gaps
    for r in all_results:
        for g in r["device_gap_events"]:
            label_key = f"{g['gap_type']}.{g['category']}"
            cur.execute("""
                INSERT INTO traceloom_gap_event VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
            """, (g["event_id"], g["db_idx"], g["device_id"],
                  g["startNs"], g["endNs"], g["durNs"], g["durMs"],
                  g["gap_type"], g["category"], None, g["confidence"],
                  g["reason"], g["position"], None, g["evidence_streams"]))

        # Insert stream gaps
        for g in r["stream_gap_events"]:
            label_key = f"{g['gap_type']}.{g['sub_category']}"
            evidence = json.dumps({"prev_task": g["prev_globalTaskId"],
                                   "next_task": g["next_globalTaskId"]})
            cur.execute("""
                INSERT INTO traceloom_gap_event VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
            """, (f"S{g['db_idx']}_DEV{g['device_id']}_STR{g['streamId']}_GAP{g['gap_id']}",
                  g["db_idx"], g["device_id"],
                  g["startNs"], g["endNs"], g["durNs"], round(g["durNs"] / 1e6, 3),
                  g["gap_type"], g["sub_category"], g["sub_category"], "confirmed",
                  f"{g['prev_taskType']} -> {g['next_taskType']}",
                  "middle", g["streamId"], evidence))

    # ==== Table 3: Gap Event Evidence (links to original TASK rows) ====
    cur.execute("""
        CREATE TABLE traceloom_gap_event_evidence (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            gap_event_id TEXT NOT NULL,
            evidence_type TEXT NOT NULL,
            globalTaskId INTEGER,
            connectionId INTEGER,
            streamId INTEGER,
            taskType TEXT,
            startNs INTEGER,
            endNs INTEGER,
            FOREIGN KEY (gap_event_id) REFERENCES traceloom_gap_event(event_id)
        )
    """)

    # ==== Table 4: Device Intervals (productive + idle) ====
    cur.execute("""
        CREATE TABLE traceloom_device_interval (
            interval_id INTEGER PRIMARY KEY AUTOINCREMENT,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            startNs INTEGER NOT NULL,
            endNs INTEGER NOT NULL,
            durNs INTEGER NOT NULL,
            durMs REAL NOT NULL,
            interval_kind TEXT NOT NULL,
            gap_event_id TEXT,
            FOREIGN KEY (gap_event_id) REFERENCES traceloom_gap_event(event_id)
        )
    """)

    # Insert productive + gap intervals as unified timeline
    interval_id = 0
    for r in all_results:
        db_idx = r["device_gap_events"][0]["db_idx"] if r["device_gap_events"] else 0
        dev_id = r["device_gap_events"][0]["device_id"] if r["device_gap_events"] else 0

        # Merge busy and gap into unified timeline
        events = []
        for s, e, d in r["busy_intervals"]:
            events.append((s, e, "productive_active", None))
        for g in r["device_gap_events"]:
            events.append((g["startNs"], g["endNs"], "visible_productive_idle", g["event_id"]))
        events.sort(key=lambda x: x[0])

        for s, e, kind, gap_id in events:
            dur = e - s
            cur.execute("""INSERT INTO traceloom_device_interval VALUES (?,?,?,?,?,?,?,?,?)""",
                        (interval_id, db_idx, dev_id, s, e, dur, round(dur / 1e6, 3), kind, gap_id))
            interval_id += 1

    # ==== Views ====
    cur.execute("""
        CREATE VIEW traceloom_v_unified_gap_timeline AS
        SELECT di.interval_id, di.db_idx, di.device_id,
               di.startNs, di.endNs, di.durNs, di.durMs,
               di.interval_kind,
               COALESCE(ge.gap_type, 'N/A') as gap_type,
               COALESCE(ge.category, 'N/A') as category,
               COALESCE(ge.confidence, 'N/A') as confidence,
               COALESCE(ge.reason, '') as reason,
               eld.description as label_description,
               eld.is_actionable
        FROM traceloom_device_interval di
        LEFT JOIN traceloom_gap_event ge ON di.gap_event_id = ge.event_id
        LEFT JOIN traceloom_event_label_definition eld
            ON ge.gap_type = eld.gap_type AND ge.category = eld.category
        ORDER BY di.db_idx, di.device_id, di.startNs
    """)

    cur.execute("""
        CREATE VIEW traceloom_v_gap_summary_by_category AS
        SELECT eld.gap_type, eld.category, eld.confidence, eld.description,
               COUNT(ge.event_id) as event_count,
               COALESCE(SUM(ge.durMs), 0) as total_ms,
               COALESCE(AVG(ge.durMs), 0) as avg_ms,
               COALESCE(MIN(ge.durMs), 0) as min_ms,
               COALESCE(MAX(ge.durMs), 0) as max_ms,
               eld.is_actionable, eld.display_priority
        FROM traceloom_event_label_definition eld
        LEFT JOIN traceloom_gap_event ge
            ON eld.gap_type = ge.gap_type AND eld.category = ge.category
        GROUP BY eld.gap_type, eld.category
        ORDER BY eld.display_priority
    """)

    cur.execute("""
        CREATE VIEW traceloom_v_gap_hotspot AS
        SELECT ge.*, eld.description, eld.is_actionable
        FROM traceloom_gap_event ge
        JOIN traceloom_event_label_definition eld
            ON ge.gap_type = eld.gap_type AND ge.category = eld.category
        WHERE (ge.gap_type = 'device_visible_gap' AND eld.is_actionable = 1)
           OR ge.gap_type = 'device_non_productive_interval'
        ORDER BY ge.durMs DESC
        LIMIT 50
    """)

    # ==== Verify ====
    cur.execute("SELECT COUNT(*) FROM traceloom_gap_event")
    total_events = cur.fetchone()[0]
    cur.execute("SELECT gap_type, COUNT(*) FROM traceloom_gap_event GROUP BY gap_type")
    for r in cur.fetchall():
        log(f"  {r[0]}: {r[1]} events")

    conn.commit()
    conn.close()
    return output_path


def main():
    db_files = []
    for root, dirs, files in os.walk(DB_DIR):
        for f in files:
            if f.endswith(".db") and f.startswith("msprof"):
                db_files.append(os.path.join(root, f))
    db_files.sort()

    log(f"Found {len(db_files)} msprof DB(s)")

    all_results = []
    for db_idx, db_path in enumerate(db_files):
        rel = os.path.relpath(db_path, DB_DIR)
        log(f"\nProcessing [{db_idx}] {rel}")
        r = process_device(db_path, db_idx, db_idx)
        r["db_path"] = rel
        all_results.append(r)
        log(f"  Device gaps: {len(r['device_gap_events'])} | Stream gaps: {len(r['stream_gap_events'])}")

    # Write output
    output_dir = os.path.join(DB_DIR, "traceloom_gap_db")
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, "derived_gaps_v2.db")
    write_output_db(all_results, output_path)

    # Verify JOIN capability
    log(f"\nVerifying JOIN with original msprof DB...")
    verify_conn = sqlite3.connect(output_path)
    original_conn = sqlite3.connect(db_files[0])

    # Example: JOIN gap events with TASK table
    verify_conn.execute("ATTACH DATABASE ? AS original", (db_files[0],))
    cur = verify_conn.cursor()
    cur.execute("""
        SELECT ge.event_id, ge.durMs, ge.gap_type, ge.category,
               COUNT(t.globalTaskId) as overlapping_tasks
        FROM traceloom_gap_event ge
        LEFT JOIN original.TASK t
            ON t.startNs BETWEEN ge.startNs AND ge.endNs
        WHERE ge.gap_type = 'device_visible_gap'
        GROUP BY ge.event_id
        LIMIT 10
    """)
    log("  Sample JOIN results (gap events x TASK):")
    for r in cur.fetchall():
        log(f"    {r[0]}: {r[1]:.1f}ms, {r[2]}/{r[3]}, {r[4]} overlapping tasks")

    verify_conn.close()
    original_conn.close()

    # Summary JSON
    summary = {"label_definitions": [], "gap_summary": {}}
    conn = sqlite3.connect(output_path)
    cur = conn.cursor()
    cur.execute("SELECT * FROM traceloom_event_label_definition")
    summary["label_definitions"] = [dict(zip([d[0] for d in cur.description], r)) for r in cur.fetchall()]
    cur.execute("SELECT * FROM traceloom_v_gap_summary_by_category")
    summary["gap_summary"] = [dict(zip([d[0] for d in cur.description], r)) for r in cur.fetchall()]
    conn.close()

    json_path = os.path.join(output_dir, "gap_definitions.json")
    with open(json_path, "w") as f:
        json.dump(summary, f, indent=2, default=str)

    log(f"\nOutput: {output_path}")
    log(f"Labels: {json_path}")
    log("Done.")


if __name__ == "__main__":
    main()
