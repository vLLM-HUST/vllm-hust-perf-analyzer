"""
Deep analysis of device gap classification results.
Queries the derived gap DB + original msprof DBs.
"""
import sqlite3
import json
import os

GAP_DB = r"D:\vllm-hust-perf-analyzer\examples\kickstart_smoke\msprof_raw\traceloom_gap_db\derived_gaps.db"

DB_DIR = r"D:\vllm-hust-perf-analyzer\examples\kickstart_smoke\msprof_raw"
MS_DB = {}
for root, dirs, files in os.walk(DB_DIR):
    for f in files:
        if f.endswith(".db") and f.startswith("msprof"):
            MS_DB[f"db{len(MS_DB)}"] = os.path.join(root, f)

conn_gap = sqlite3.connect(GAP_DB)
conn_gap.row_factory = sqlite3.Row

# ============================================================
# A: Overall gap statistics
# ============================================================
print("=" * 70)
print("A: Device Gap 总体统计")
print("=" * 70)

cur = conn_gap.cursor()
cur.execute("""
    SELECT device_id, gap_type, category, confidence,
           COUNT(*) as cnt,
           SUM(durMs) as total_ms,
           AVG(durMs) as avg_ms,
           MIN(durMs) as min_ms,
           MAX(durMs) as max_ms
    FROM traceloom_device_gap
    GROUP BY device_id, category, confidence
    ORDER BY total_ms DESC
""")

print(f"\n{'Dev':<4} {'Category':<35} {'Conf':<12} {'Count':>7} {'Total_ms':>12} {'Avg_ms':>10} {'Min_ms':>10} {'Max_ms':>10}")
print("-" * 104)
for r in cur.fetchall():
    print(f"{r['device_id']:<4} {r['category']:<35} {r['confidence']:<12} "
          f"{r['cnt']:>7} {r['total_ms']:>12,.1f} {r['avg_ms']:>10,.1f} "
          f"{r['min_ms']:>10,.3f} {r['max_ms']:>10,.1f}")

# ============================================================
# B: Gap size distribution by category
# ============================================================
print("\n" + "=" * 70)
print("B: 各类别的 Gap 大小分布")
print("=" * 70)

cur.execute("""
    SELECT device_id, category,
           CASE
               WHEN durMs >= 1000 THEN '>1s'
               WHEN durMs >= 100 THEN '100ms-1s'
               WHEN durMs >= 10 THEN '10-100ms'
               WHEN durMs >= 1 THEN '1-10ms'
               ELSE '<1ms'
           END as size_range,
           COUNT(*) as cnt,
           SUM(durMs) as total_ms
    FROM traceloom_device_gap
    GROUP BY device_id, category, size_range
    ORDER BY device_id, category, MIN(durMs)
""")

for dev_id in [0, 1]:
    print(f"\n  Device {dev_id}:")
    categories = {}
    for r in cur.fetchall():
        if r['device_id'] != dev_id:
            continue
        cat = r['category']
        if cat not in categories:
            categories[cat] = []
        categories[cat].append((r['size_range'], r['cnt'], r['total_ms']))

    for cat, sizes in categories.items():
        print(f"    {cat}:")
        for sz, cnt, total in sizes:
            print(f"      {sz:<12}  count={cnt:>6}  total={total:>10,.1f}ms")

# ============================================================
# C: Deep dive into runtime_control_present
# ============================================================
print("\n" + "=" * 70)
print("C: runtime_control_present 深入分析")
print("   问：这 33s 的 gap 里到底在跑什么？")
print("=" * 70)

for db_idx, db_path in MS_DB.items():
    dev_id = int(db_idx.replace("db", ""))
    conn_ms = sqlite3.connect(db_path)
    str_map = {}
    for r in conn_ms.execute("SELECT id, value FROM STRING_IDS"):
        str_map[str(r[0])] = r[1]

    # Get a random device gap in this category to inspect
    cur = conn_gap.cursor()
    cur.execute("""
        SELECT event_id, startNs, endNs, durMs, stream_states
        FROM traceloom_device_gap
        WHERE db_idx = ? AND category = 'runtime_control_present'
        ORDER BY durMs DESC
        LIMIT 5
    """, (dev_id,))

    top_gaps = cur.fetchall()

    for i, g in enumerate(top_gaps):
        ss = json.loads(g['stream_states']) if g['stream_states'] else {}
        stream_list = list(ss.keys())

        if i == 0:  # Only show detail for the largest one
            print(f"\n  Device {dev_id} — 最大的 runtime_control_present gap: "
                  f"{g['event_id']} ({g['durMs']:.1f}ms)")
            print(f"    涉及的 streams: {len(ss)}")
            # Show stream states
            for sid, states in ss.items():
                print(f"      stream {sid}: {states}")

            # What task types were running?
            print(f"\n    该 gap 区间内的 TASK 类型分布:")
            conn_ms2 = sqlite3.connect(db_path)
            cur2 = conn_ms2.cursor()
            cur2.execute("""
                SELECT t.taskType, COUNT(*) as cnt,
                       SUM(t.endNs - t.startNs)/1000.0 as total_us
                FROM TASK t
                WHERE t.startNs < ? AND t.endNs > ?
                GROUP BY t.taskType
                ORDER BY cnt DESC
                LIMIT 15
            """, (g['endNs'], g['startNs']))
            for r2 in cur2.fetchall():
                tname = str_map.get(str(r2[0]), f"ID:{r2[0]}")
                print(f"        {tname:<35} count={r2[1]:>6}  total={r2[2]:>10,.0f}us")
            conn_ms2.close()

        # Stream state overview for each of the top 5
        state_types = {}
        for sid, states in ss.items():
            for s in states:
                state_types[s] = state_types.get(s, 0) + 1
        if i == 0:
            print(f"\n    所有 stream 的状态类型汇总: {state_types}")

    conn_ms.close()

# ============================================================
# D: Deep dive into no_observed_device_work
# ============================================================
print("\n" + "=" * 70)
print("D: no_observed_device_work 深入分析")
print("   问：这 16s 的 gap 真的是完全没有任何可见活动吗？")
print("=" * 70)

for dev_id in [0, 1]:
    cur = conn_gap.cursor()
    cur.execute("""
        SELECT event_id, db_idx, device_id, startNs, endNs, durMs, position
        FROM traceloom_device_gap
        WHERE device_id = ? AND category = 'no_observed_device_work'
        ORDER BY durMs DESC
        LIMIT 5
    """, (dev_id,))

    top_gaps = cur.fetchall()
    print(f"\n  Device {dev_id} — Top 5 no_observed_device_work gaps:")

    for g in top_gaps:
        # Confirm: are there really no TASK rows?
        db_path = MS_DB.get(f"db{dev_id}", list(MS_DB.values())[dev_id])
        conn_ms = sqlite3.connect(db_path)
        cur2 = conn_ms.cursor()
        cur2.execute("""
            SELECT COUNT(*) as cnt,
                   COUNT(DISTINCT streamId) as streams
            FROM TASK
            WHERE startNs < ? AND endNs > ?
        """, (g['endNs'], g['startNs']))
        task_count, stream_count = cur2.fetchone()

        # Check CANN_API activity
        cur2.execute("""
            SELECT COUNT(*) as cnt,
                   COUNT(DISTINCT globalTid) as threads
            FROM CANN_API
            WHERE startNs < ? AND endNs > ?
        """, (g['endNs'], g['startNs']))

        api_count, api_threads = cur2.fetchone()

        # Top CANN APIs in this gap
        cur2.execute("""
            SELECT COALESCE(s.value, CAST(c.name AS TEXT)) as api_name,
                   COUNT(*) as cnt,
                   SUM(c.endNs - c.startNs)/1000.0 as total_us
            FROM CANN_API c
            LEFT JOIN STRING_IDS s ON CAST(c.name AS TEXT) = CAST(s.id AS TEXT)
            WHERE c.startNs < ? AND c.endNs > ?
            GROUP BY c.name
            ORDER BY cnt DESC
            LIMIT 5
        """, (g['endNs'], g['startNs']))

        apis = list(cur2.fetchall())

        print(f"\n    {g['event_id']}: {g['durMs']:.1f}ms, position={g['position']}")
        print(f"      TASK rows in gap:    {task_count} (on {stream_count} streams)")
        print(f"      CANN_API rows in gap: {api_count} (on {api_threads} threads)")
        if apis:
            print(f"      Top CANN_API calls in gap:")
            for a in apis:
                print(f"        {a[0]:<45} cnt={a[1]:>6}  total={a[2]:>10,.0f}us")
        else:
            print(f"      No CANN_API calls during this gap → true dark interval")

        conn_ms.close()

# ============================================================
# E: blocked_by_visible_wait — which streams & what's waiting?
# ============================================================
print("\n" + "=" * 70)
print("E: blocked_by_visible_wait 深入分析")
print("   问：哪些 stream 在等？等待对应什么 CANN_API？")
print("=" * 70)

for dev_id in [0, 1]:
    cur = conn_gap.cursor()
    cur.execute("""
        SELECT event_id, db_idx, device_id, startNs, endNs, durMs, stream_states
        FROM traceloom_device_gap
        WHERE device_id = ? AND category = 'blocked_by_visible_wait'
        ORDER BY durMs DESC
        LIMIT 5
    """, (dev_id,))

    top_gaps = cur.fetchall()
    print(f"\n  Device {dev_id} — Top 5 blocked_by_visible_wait gaps:")

    for g in top_gaps:
        ss = json.loads(g['stream_states']) if g['stream_states'] else {}
        # Find which streams have 'wait' state
        wait_streams = [sid for sid, states in ss.items() if 'wait' in states]
        other_streams = [sid for sid, states in ss.items() if 'wait' not in states]

        print(f"\n    {g['event_id']}: {g['durMs']:.1f}ms")
        print(f"      Wait streams: {wait_streams}")
        if other_streams:
            print(f"      Other active streams: {other_streams}")
            for sid in other_streams:
                print(f"        stream {sid}: {ss[sid]}")

        # Check CANN_API: aclrtStreamWaitEvent during this gap?
        db_path = MS_DB.get(f"db{dev_id}", list(MS_DB.values())[dev_id])
        conn_ms = sqlite3.connect(db_path)
        cur2 = conn_ms.cursor()
        cur2.execute("""
            SELECT COUNT(*) as cnt,
                   SUM(c.endNs - c.startNs)/1000.0 as total_us
            FROM CANN_API c
            JOIN STRING_IDS s ON CAST(c.name AS TEXT) = CAST(s.id AS TEXT)
            WHERE s.value = 'aclrtStreamWaitEvent'
              AND c.startNs < ? AND c.endNs > ?
        """, (g['endNs'], g['startNs']))
        sw_cnt, sw_total = cur2.fetchone()
        if sw_cnt:
            print(f"      aclrtStreamWaitEvent during gap: {sw_cnt} calls, {sw_total:.0f}us")

        cur2.execute("""
            SELECT COUNT(*) as cnt
            FROM CANN_API c
            JOIN STRING_IDS s ON CAST(c.name AS TEXT) = CAST(s.id AS TEXT)
            WHERE s.value = 'aclrtSynchronizeStream'
              AND c.startNs < ? AND c.endNs > ?
        """, (g['endNs'], g['startNs']))
        ss_cnt = cur2.fetchone()[0]
        if ss_cnt:
            print(f"      aclrtSynchronizeStream during gap: {ss_cnt} calls")

        conn_ms.close()

    # Aggregate: which streams are most frequently in wait?
    cur = conn_gap.cursor()
    cur.execute("""
        SELECT stream_states
        FROM traceloom_device_gap
        WHERE device_id = ? AND category = 'blocked_by_visible_wait'
    """, (dev_id,))

    wait_stream_counts = {}
    for r in cur.fetchall():
        ss = json.loads(r['stream_states']) if r['stream_states'] else {}
        for sid, states in ss.items():
            if 'wait' in states:
                wait_stream_counts[sid] = wait_stream_counts.get(sid, 0) + 1

    print(f"\n    Device {dev_id} — 最常处于 wait 的 stream (在所有 blocked_by_visible_wait gap 中):")
    for sid, cnt in sorted(wait_stream_counts.items(), key=lambda x: -x[1])[:10]:
        print(f"      stream {sid}: {cnt} 次")

# ============================================================
# F: phase_change_boundary analysis
# ============================================================
print("\n" + "=" * 70)
print("F: phase_change_boundary 分析")
print("   问：5 个巨型 gap 分别标记了什么阶段变化？")
print("=" * 70)

cur = conn_gap.cursor()
cur.execute("""
    SELECT event_id, device_id, startNs, endNs, durMs, position
    FROM traceloom_device_gap
    WHERE category = 'phase_change_boundary'
    ORDER BY device_id, startNs
""")

for r in cur.fetchall():
    # What's the first task after this gap?
    db_path = list(MS_DB.values())[r['device_id']] if r['device_id'] < len(MS_DB) else list(MS_DB.values())[0]
    conn_ms = sqlite3.connect(db_path)
    str_map = {}
    for s in conn_ms.execute("SELECT id, value FROM STRING_IDS"):
        str_map[str(s[0])] = s[1]

    # First productive task after gap
    cur2 = conn_ms.cursor()
    cur2.execute("""
        SELECT t.globalTaskId, t.streamId, t.taskType, t.startNs,
               (t.startNs - ?) / 1000.0 as gap_from_end_us
        FROM TASK t
        WHERE t.startNs >= ?
        ORDER BY t.startNs
        LIMIT 5
    """, (r['endNs'], r['startNs']))

    first_tasks = list(cur2.fetchall())

    # Last task before gap
    cur2.execute("""
        SELECT t.globalTaskId, t.streamId, t.taskType, t.endNs
        FROM TASK t
        WHERE t.endNs <= ?
        ORDER BY t.endNs DESC
        LIMIT 5
    """, (r['startNs'],))

    last_tasks = list(cur2.fetchall())

    print(f"\n  {r['event_id']} (Dev {r['device_id']}): {r['durMs']:,.1f}ms, position={r['position']}")
    print(f"    最后 5 个 task (gap 之前):")
    for lt in last_tasks:
        tname = str_map.get(str(lt[2]), f"ID:{lt[2]}")
        print(f"      stream={lt[1]} type={tname:<30} end={lt[3]}")
    print(f"    最前 5 个 task (gap 之后):")
    for ft in first_tasks:
        tname = str_map.get(str(ft[2]), f"ID:{ft[2]}")
        print(f"      stream={ft[1]} type={tname:<30} start={ft[3]}  (距gap结束{ft[4]:.0f}us)")

    conn_ms.close()

# ============================================================
# G: Cross-category: which contributes to "real" wasted time?
# ============================================================
print("\n" + "=" * 70)
print("G: Gap 归因汇总 —— 可操作 vs 不可操作")
print("=" * 70)

# Categories we can act on vs cannot
actionable = ['blocked_by_visible_wait', 'queued_visible_task_delay']
potential_artifact = ['runtime_control_present', 'capture_control_present']
boundary = ['phase_change_boundary']
unknown = ['no_observed_device_work']

cur = conn_gap.cursor()
for dev_id in [0, 1]:
    cur.execute("""
        SELECT
            SUM(CASE WHEN category IN ('blocked_by_visible_wait', 'queued_visible_task_delay') THEN durMs ELSE 0 END) as actionable_ms,
            SUM(CASE WHEN category IN ('runtime_control_present', 'capture_control_present') THEN durMs ELSE 0 END) as potential_artifact_ms,
            SUM(CASE WHEN category = 'phase_change_boundary' THEN durMs ELSE 0 END) as phase_boundary_ms,
            SUM(CASE WHEN category = 'no_observed_device_work' THEN durMs ELSE 0 END) as unknown_ms,
            SUM(durMs) as total_gap_ms
        FROM traceloom_device_gap
        WHERE device_id = ?
    """, (dev_id,))
    r = cur.fetchone()
    print(f"\n  Device {dev_id} — 总 Gap: {r['total_gap_ms']:,.0f}ms")
    print(f"    可操作的等待:     {r['actionable_ms']:>10,.0f}ms  ({r['actionable_ms']/r['total_gap_ms']*100:.1f}%)")
    print(f"    可能的 profiler artifact: {r['potential_artifact_ms']:>10,.0f}ms  ({r['potential_artifact_ms']/r['total_gap_ms']*100:.1f}%)")
    print(f"    阶段边界:         {r['phase_boundary_ms']:>10,.0f}ms  ({r['phase_boundary_ms']/r['total_gap_ms']*100:.1f}%)")
    print(f"    完全未知:         {r['unknown_ms']:>10,.0f}ms  ({r['unknown_ms']/r['total_gap_ms']*100:.1f}%)")

conn_gap.close()
print("\nDone.")
