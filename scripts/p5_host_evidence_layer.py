"""
P5: Host API Evidence Layer + msprof-analyze 借鉴落地
=====================================================
Adds RFC Layer 4 (Host Evidence) to the gap analysis pipeline:

1. Host API overlap detection: for each device gap, queries CANN_API
   and classifies host activity into memory_mgmt / sync_wait / launch / idle
2. Idle/compute ratio: detects "small operator blocking" pattern (from msprof-analyze)
3. Per-stream gap pattern clustering: dynamic stream labels (not hardcoded IDs)
4. Updated derived gap DB with host evidence tables and diagnostic views

Requires: p2_derived_gap_db.py output (derived_gaps_v2.db)
"""

import sqlite3
import os
import json
import bisect

BASE = r"D:\vllm-hust-perf-analyzer"
DB_DIR = os.path.join(BASE, "examples", "kickstart_smoke", "msprof_raw")
GAP_DB = os.path.join(DB_DIR, "traceloom_gap_db", "derived_gaps_v2.db")

MS_DB_FILES = []
for root, dirs, files in os.walk(DB_DIR):
    for f in files:
        if f.endswith(".db") and f.startswith("msprof"):
            MS_DB_FILES.append(os.path.join(root, f))
    if len(MS_DB_FILES) >= 2:
        break


def log(msg):
    print(msg, flush=True)


# ============================================================
# Part 1: Host API Evidence for Device Gaps
# ============================================================
log("=" * 60)
log("Part 1: Host API Evidence Layer")
log("=" * 60)

# Connect to gap DB
gap_conn = sqlite3.connect(GAP_DB)
gap_conn.row_factory = sqlite3.Row

# Load device gaps that need host evidence
cur = gap_conn.cursor()
cur.execute("""
    SELECT event_id, db_idx, device_id, startNs, endNs, durMs, category, confidence
    FROM traceloom_gap_event
    WHERE gap_type = 'device_visible_gap'
      AND confidence IN ('unknown', 'contextual', 'heuristic')
    ORDER BY durMs DESC
""")
gaps_needing_evidence = [dict(r) for r in cur.fetchall()]
log(f"Gaps needing host evidence: {len(gaps_needing_evidence)}")

# For each gap, query CANN_API in the corresponding msprof DB
# Load string maps for both devices
str_maps = []
for db_path in MS_DB_FILES:
    conn = sqlite3.connect(db_path)
    sm = {}
    for r in conn.execute("SELECT id, value FROM STRING_IDS"):
        sm[str(r[0])] = r[1]
    str_maps.append(sm)
    conn.close()

# Classify CANN_API names into activity types
HOST_SYNC_APIS = {
    "aclrtSynchronizeStream", "StreamSynchronize",
    "aclrtSynchronizeDeviceWithTimeout", "DeviceSynchronize",
    "aclrtSynchronizeEvent", "EventSynchronize",
}
HOST_MEMORY_APIS = {
    "aclrtMallocHost", "HostMalloc", "aclrtFreeHost", "HostFree",
    "aclrtMalloc", "DevMalloc", "aclrtFree", "DevFree",
    "aclrtMallocWithCfg", "aclrtMallocHostWithCfg",
    "aclrtMemcpyAsync", "MemcpyAsync", "MemcpyAsync_Huge",
    "aclrtMemcpy", "MemCopySync",
}
HOST_LAUNCH_APIS = {
    "launch", "aclrtLaunchKernelWithHostArgs", "LaunchKernelV2",
    "KernelLaunchWithHandle", "LaunchRandomNumTask",
    "aclrtRandomNumAsync",
}
HOST_EVENT_APIS = {
    "aclrtCreateEventExWithFlag", "EventCreateEx",
    "aclrtDestroyEvent", "EventDestroy",
    "aclrtRecordEvent", "EventRecord",
    "aclrtQueryEventStatus",
    "aclrtStreamWaitEvent", "StreamWaitEvent",
}
HOST_GRAPH_APIS = {
    "aclmdlRICaptureGetInfo", "aclmdlRICaptureBegin", "aclmdlRICaptureEnd",
    "aclmdlRIExecuteAsync", "ModelExecute",
}

host_evidence_results = []

for gap_idx, gap in enumerate(gaps_needing_evidence):
    if gap_idx % 1000 == 0:
        log(f"  Processing gap {gap_idx}/{len(gaps_needing_evidence)}...")

    db_idx = gap["db_idx"]
    if db_idx >= len(MS_DB_FILES):
        continue
    ms_db = MS_DB_FILES[db_idx]
    str_map = str_maps[db_idx]

    conn = sqlite3.connect(ms_db)
    cur2 = conn.cursor()

    # Query CANN_API overlapping this gap
    cur2.execute("""
        SELECT c.name, COUNT(*) as cnt,
               SUM(c.endNs - c.startNs) / 1000.0 as total_us,
               AVG(c.endNs - c.startNs) / 1000.0 as avg_us,
               COUNT(DISTINCT c.globalTid) as threads
        FROM CANN_API c
        WHERE c.startNs < ? AND c.endNs > ?
        GROUP BY c.name
        HAVING cnt >= 1
        ORDER BY total_us DESC
    """, (gap["endNs"], gap["startNs"]))

    api_rows = []
    activity_types = set()
    total_host_us = 0
    sync_us = 0
    memory_us = 0
    launch_us = 0
    event_us = 0
    graph_us = 0

    for r in cur2.fetchall():
        api_name = str_map.get(str(r[0]), f"ID:{r[0]}")
        cnt, total_us, avg_us, threads = r[1], r[2], r[3], r[4]
        api_rows.append({
            "api_name": api_name, "cnt": cnt, "total_us": total_us,
            "avg_us": avg_us, "threads": threads,
        })
        total_host_us += total_us

        if api_name in HOST_SYNC_APIS:
            activity_types.add("sync_wait")
            sync_us += total_us
        if api_name in HOST_MEMORY_APIS:
            activity_types.add("memory_mgmt")
            memory_us += total_us
        if api_name in HOST_LAUNCH_APIS:
            activity_types.add("launch")
            launch_us += total_us
        if api_name in HOST_EVENT_APIS:
            activity_types.add("event_lifecycle")
            event_us += total_us
        if api_name in HOST_GRAPH_APIS:
            activity_types.add("graph_control")
            graph_us += total_us

    conn.close()

    # Determine refined category based on host evidence
    refined_category = gap["category"]
    refined_confidence = gap["confidence"]
    host_reason = ""

    if gap["category"] == "no_observed_device_work":
        if "sync_wait" in activity_types:
            refined_category = "host_sync_api_present"
            refined_confidence = "contextual"
            host_reason = f"Host sync API active ({sync_us:.0f}us total)"
        elif "memory_mgmt" in activity_types:
            refined_category = "host_memory_management"
            refined_confidence = "contextual"
            host_reason = f"Host memory management ({memory_us:.0f}us total)"
        elif "launch" in activity_types:
            refined_category = "queued_visible_task_delay"
            refined_confidence = "contextual"
            host_reason = f"Host launch activity ({launch_us:.0f}us total)"
        elif "event_lifecycle" in activity_types:
            refined_category = "host_event_lifecycle"
            refined_confidence = "heuristic"
            host_reason = f"Host event lifecycle ops ({event_us:.0f}us total)"
        elif total_host_us > 0:
            refined_category = "no_observed_device_work"
            refined_confidence = "unknown"
            host_reason = f"Minor host activity ({total_host_us:.0f}us)"
        else:
            refined_category = "no_observed_device_work"
            refined_confidence = "unknown"
            host_reason = "No host or device activity — true dark interval"

    host_evidence_results.append({
        "event_id": gap["event_id"],
        "total_host_us": round(total_host_us, 1),
        "sync_us": round(sync_us, 1),
        "memory_us": round(memory_us, 1),
        "launch_us": round(launch_us, 1),
        "event_us": round(event_us, 1),
        "graph_us": round(graph_us, 1),
        "activity_types": list(activity_types),
        "api_count": len(api_rows),
        "top_apis": json.dumps(api_rows[:5]),
        "refined_category": refined_category,
        "refined_confidence": refined_confidence,
        "host_reason": host_reason,
    })

# Print summary
log(f"\nHost evidence summary:")
cat_changes = {}
for r in host_evidence_results:
    old = r["event_id"].split("_")[-2] if "_" in r["event_id"] else "?"
    key = f"{old} -> {r['refined_category']}"
    cat_changes[key] = cat_changes.get(key, 0) + 1
for k, v in sorted(cat_changes.items(), key=lambda x: -x[1]):
    log(f"  {k}: {v} gaps")

# ============================================================
# Part 2: Idle/Compute Ratio Detection (from OpScheduleAdvice)
# ============================================================
log("\n" + "=" * 60)
log("Part 2: Idle/Compute Ratio Detection (msprof-analyze OpScheduleAdvice)")
log("=" * 60)

# For each stream, compute ratio of gap time to task time
for db_idx, db_path in enumerate(MS_DB_FILES):
    conn = sqlite3.connect(db_path)
    str_map = str_maps[db_idx]
    cur = conn.cursor()

    # Per-stream: total gap vs total task time
    cur.execute("""
        SELECT streamId,
               SUM(endNs - startNs) / 1000.0 as total_task_us,
               COUNT(*) as task_count
        FROM TASK
        GROUP BY streamId
        ORDER BY total_task_us DESC
        LIMIT 20
    """)
    stream_stats = [(r[0], r[1], r[2]) for r in cur.fetchall()]
    conn.close()

    # Get stream-level gap totals from gap DB
    gap_cur = gap_conn.cursor()
    gap_cur.execute("""
        SELECT streamId, SUM(durMs) as total_gap_ms, COUNT(*) as gap_count
        FROM traceloom_gap_event
        WHERE db_idx = ? AND gap_type = 'stream_local_gap'
        GROUP BY streamId
    """, (db_idx,))
    gap_stats = {(r["streamId"]): (r["total_gap_ms"], r["gap_count"]) for r in gap_cur.fetchall()}

    log(f"\n  Device {db_idx} — Stream idle/compute ratios (top 10 by task time):")
    log(f"  {'Stream':<8} {'Tasks':>7} {'Task_ms':>10} {'Gap_ms':>10} {'Gap/Task':>9} {'Diagnosis'}")

    stream_ratios = []
    for sid, task_us, task_cnt in stream_stats[:10]:
        task_ms = task_us / 1000.0
        gap_ms, gap_cnt = gap_stats.get(sid, (0, 0))
        ratio = gap_ms / task_ms if task_ms > 0 else 0
        if ratio > 5:
            diagnosis = "CRITICAL: host dispatch bottleneck"
        elif ratio > 1:
            diagnosis = "WARNING: significant idle overhead"
        elif ratio > 0.2:
            diagnosis = "MODERATE: acceptable overhead"
        else:
            diagnosis = "OK"

        log(f"  {sid:<8} {task_cnt:>7,} {task_ms:>10,.1f} {gap_ms:>10,.1f} {ratio:>8,.2f}x  {diagnosis}")
        stream_ratios.append({
            "db_idx": db_idx, "streamId": sid, "task_ms": round(task_ms, 1),
            "task_count": task_cnt, "gap_ms": round(gap_ms, 1), "gap_count": gap_cnt,
            "gap_task_ratio": round(ratio, 2), "diagnosis": diagnosis,
        })

# ============================================================
# Part 3: Per-Stream Gap Pattern Clustering (Dynamic Labels)
# ============================================================
log("\n" + "=" * 60)
log("Part 3: Dynamic Stream Gap Pattern Clustering")
log("=" * 60)

# For each stream, cluster by dominant transition pattern
for db_idx in [0, 1]:
    cur = gap_conn.cursor()
    cur.execute("""
        SELECT streamId, sub_category, COUNT(*) as cnt, SUM(durMs) as total_ms
        FROM traceloom_gap_event
        WHERE db_idx = ? AND gap_type = 'stream_local_gap'
        GROUP BY streamId, sub_category
        ORDER BY streamId, total_ms DESC
    """, (db_idx,))

    stream_patterns = {}
    for r in cur.fetchall():
        sid = r["streamId"]
        if sid not in stream_patterns:
            stream_patterns[sid] = {"total_gaps": 0, "patterns": {}}
        stream_patterns[sid]["total_gaps"] += r["cnt"]
        stream_patterns[sid]["patterns"][r["sub_category"]] = {
            "cnt": r["cnt"], "total_ms": r["total_ms"]
        }

    log(f"\n  Device {db_idx} — Dynamic Stream Labels:")
    log(f"  {'Stream':<8} {'TotalGaps':>8} {'Dominant Pattern':<35} {'Pct':>7} {'Label'}")
    log("  " + "-" * 85)

    for sid, sp in sorted(stream_patterns.items(),
                          key=lambda x: -x[1]["total_gaps"]):
        if sp["total_gaps"] < 10:
            continue
        # Dominant pattern
        best_pat = max(sp["patterns"].items(), key=lambda x: x[1]["total_ms"])
        pat_name, pat_data = best_pat
        pct = pat_data["total_ms"] / sum(p["total_ms"] for p in sp["patterns"].values()) * 100

        # Assign label
        if pat_name == "event_synchronization_boundary":
            label = "sync_stream"
        elif pat_name == "intra_kernel_launch_gap":
            label = "compute_stream"
        elif pat_name == "model_maintenance_interval":
            label = "maintenance_stream"
        elif pat_name == "capture_control_boundary":
            label = "capture_stream"
        elif pat_name == "event_record_boundary":
            label = "record_stream"
        elif pat_name == "wait_to_compute_transition":
            label = "wait_recovery_stream"
        else:
            label = "mixed_stream"

        log(f"  {sid:<8} {sp['total_gaps']:>8,} {pat_name:<35} {pct:>6,.1f}%  {label}")

# ============================================================
# Part 4: Write Host Evidence to Enhanced Gap DB
# ============================================================
log("\n" + "=" * 60)
log("Part 4: Writing Enhanced Gap DB")
log("=" * 60)

# Create host evidence table
cur = gap_conn.cursor()

cur.execute("DROP TABLE IF EXISTS traceloom_gap_host_evidence")
cur.execute("""
    CREATE TABLE traceloom_gap_host_evidence (
        event_id TEXT PRIMARY KEY,
        total_host_us REAL,
        sync_us REAL,
        memory_us REAL,
        launch_us REAL,
        event_us REAL,
        graph_us REAL,
        activity_types TEXT,
        api_count INTEGER,
        top_apis TEXT,
        refined_category TEXT,
        refined_confidence TEXT,
        host_reason TEXT,
        FOREIGN KEY (event_id) REFERENCES traceloom_gap_event(event_id)
    )
""")

# Insert all evidence rows
for r in host_evidence_results:
    cur.execute("""
        INSERT OR REPLACE INTO traceloom_gap_host_evidence VALUES
        (?,?,?,?,?,?,?,?,?,?,?,?,?)
    """, (r["event_id"], r["total_host_us"], r["sync_us"], r["memory_us"],
          r["launch_us"], r["event_us"], r["graph_us"],
          json.dumps(r["activity_types"]), r["api_count"], r["top_apis"],
          r["refined_category"], r["refined_confidence"], r["host_reason"]))

# Create idle/compute ratio view
cur.execute("DROP TABLE IF EXISTS traceloom_stream_idle_ratio")
cur.execute("""
    CREATE TABLE traceloom_stream_idle_ratio (
        db_idx INTEGER, streamId INTEGER,
        task_ms REAL, task_count INTEGER,
        gap_ms REAL, gap_count INTEGER,
        gap_task_ratio REAL, diagnosis TEXT,
        PRIMARY KEY (db_idx, streamId)
    )
""")

for r in stream_ratios:
    cur.execute("""
        INSERT OR REPLACE INTO traceloom_stream_idle_ratio VALUES
        (?,?,?,?,?,?,?,?)
    """, (r["db_idx"], r["streamId"], r["task_ms"], r["task_count"],
          r["gap_ms"], r["gap_count"], r["gap_task_ratio"], r["diagnosis"]))

# Enhanced diagnostic view
cur.execute("DROP VIEW IF EXISTS traceloom_v_enhanced_gap_diagnosis")
cur.execute("""
    CREATE VIEW traceloom_v_enhanced_gap_diagnosis AS
    SELECT
        ge.event_id, ge.db_idx, ge.device_id,
        ge.startNs, ge.endNs,
        ge.durMs, ge.gap_type, ge.category,
        ge.confidence,
        COALESCE(he.refined_category, ge.category) as refined_category,
        COALESCE(he.refined_confidence, ge.confidence) as refined_confidence,
        COALESCE(he.host_reason, ge.reason) as detailed_reason,
        he.total_host_us, he.sync_us, he.memory_us,
        he.activity_types,
        eld.is_actionable, eld.description
    FROM traceloom_gap_event ge
    LEFT JOIN traceloom_gap_host_evidence he ON ge.event_id = he.event_id
    LEFT JOIN traceloom_event_label_definition eld
        ON ge.gap_type = eld.gap_type
        AND COALESCE(he.refined_category, ge.category) = eld.category
    ORDER BY ge.durMs DESC
""")

# Category migration summary view
cur.execute("DROP VIEW IF EXISTS traceloom_v_evidence_impact")
cur.execute("""
    CREATE VIEW traceloom_v_evidence_impact AS
    SELECT
        ge.category as original_category,
        he.refined_category,
        ge.confidence as original_confidence,
        he.refined_confidence,
        COUNT(*) as gap_count,
        SUM(ge.durMs) as total_ms
    FROM traceloom_gap_event ge
    JOIN traceloom_gap_host_evidence he ON ge.event_id = he.event_id
    GROUP BY ge.category, he.refined_category, ge.confidence, he.refined_confidence
    ORDER BY total_ms DESC
""")

# Add host evidence label definitions
cur.execute("""
    INSERT OR REPLACE INTO traceloom_event_label_definition VALUES
    ('host_sync_api_present', 'device_visible_gap', 'host_sync_api_present', 'contextual',
     'Device gap with overlapping host synchronization API (aclrtSynchronizeStream etc.)',
     1, 15)
""")
cur.execute("""
    INSERT OR REPLACE INTO traceloom_event_label_definition VALUES
    ('host_memory_management', 'device_visible_gap', 'host_memory_management', 'contextual',
     'Device gap with host memory allocation/deallocation activity — likely GC or buffer management',
     0, 58)
""")
cur.execute("""
    INSERT OR REPLACE INTO traceloom_event_label_definition VALUES
    ('host_event_lifecycle', 'device_visible_gap', 'host_event_lifecycle', 'heuristic',
     'Device gap with event create/destroy/query activity — resource lifecycle overhead',
     0, 60)
""")

gap_conn.commit()

# Show evidence impact
cur.execute("SELECT * FROM traceloom_v_evidence_impact")
results = list(cur.fetchall())
log(f"\nEvidence impact (category migrations):")
log(f"  {'Original':<30} {'Refined':<30} {'Count':>6} {'TotalMs':>10}")
for r in results:
    log(f"  {r['original_category']:<30} {r['refined_category']:<30} {r['gap_count']:>6} {r['total_ms']:>10,.1f}")

gap_conn.close()

# Write output
output_path = os.path.join(DB_DIR, "traceloom_gap_db", "host_evidence_summary.json")
with open(output_path, "w") as f:
    json.dump({
        "evidence_count": len(host_evidence_results),
        "category_migrations": [dict(r) for r in results],
        "stream_ratios": stream_ratios,
    }, f, indent=2, default=str)
log(f"\nSummary: {output_path}")

log("\nP5 complete.")
