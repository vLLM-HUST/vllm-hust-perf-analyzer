import sqlite3
import os

db_dir = r"D:\vllm-hust-perf-analyzer\examples\kickstart_smoke\msprof_raw"

db_files = []
for root, dirs, files in os.walk(db_dir):
    for f in files:
        if f.endswith(".db"):
            db_files.append(os.path.join(root, f))

for db_path in db_files:
    rel = os.path.relpath(db_path, db_dir)
    conn = sqlite3.connect(db_path)
    cur = conn.cursor()

    print(f"{'='*70}")
    print(f"DB: {rel}")
    print(f"{'='*70}")

    # ===== Q1: Sync/Wait/Event related CANN APIs =====
    print("\n--- Q1: Sync & Wait / Event / Record related CANN APIs ---")
    cur.execute("""
        SELECT COALESCE(s.value, c.name) as api_name,
               COUNT(*) as cnt,
               SUM(c.endNs - c.startNs) / 1000.0 as total_us,
               AVG((c.endNs - c.startNs) / 1000.0) as avg_us,
               COUNT(DISTINCT c.globalTid) as threads
        FROM CANN_API c
        JOIN STRING_IDS s ON CAST(c.name AS TEXT) = CAST(s.id AS TEXT)
        WHERE s.value LIKE '%Synchronize%'
           OR s.value LIKE '%sync%'
           OR s.value LIKE '%Wait%'
           OR s.value LIKE '%Notify%'
           OR s.value LIKE '%Event%'
           OR s.value LIKE '%Flush%'
           OR s.value LIKE '%Record%'
           OR s.value LIKE '%record%'
        GROUP BY c.name
        ORDER BY cnt DESC
    """)
    rows = cur.fetchall()
    print(f"{'API Name':<55} {'Count':>8} {'Total_ms':>12} {'Avg_us':>10} {'Thr':>4}")
    print("-" * 91)
    for row in rows:
        name, cnt, total_us, avg_us, threads = row
        total_ms = (total_us if total_us else 0) / 1000.0
        avg = avg_us if avg_us else 0
        print(f"{name:<55} {cnt:>8,} {total_ms:>12,.1f} {avg:>10,.1f} {threads:>4}")

    # ===== Q2: All CANN_API names (every distinct) =====
    print("\n--- Q2: ALL distinct CANN_API names (resolved) ---")
    cur.execute("""
        SELECT COALESCE(s.value, 'ID:' || CAST(c.name AS TEXT)) as api_name,
               COUNT(*) as cnt,
               SUM(c.endNs - c.startNs) / 1000.0 as total_us,
               AVG((c.endNs - c.startNs) / 1000.0) as avg_us,
               c.type as api_type
        FROM CANN_API c
        LEFT JOIN STRING_IDS s ON CAST(c.name AS TEXT) = CAST(s.id AS TEXT)
        GROUP BY c.name
        ORDER BY cnt DESC
    """)
    rows = cur.fetchall()
    print(f"Total distinct API names: {len(rows)}")
    print(f"{'API Name':<55} {'Count':>8} {'Total_ms':>12} {'Avg_us':>10} {'Type':>8}")
    print("-" * 95)
    for row in rows:
        name, cnt, total_us, avg_us, api_type = row
        total_ms = (total_us if total_us else 0) / 1000.0
        avg = avg_us if avg_us else 0
        print(f"{name:<55} {cnt:>8,} {total_ms:>12,.1f} {avg:>10,.1f} {api_type:>8}")

    # ===== Q3: TASK taskType breakdown =====
    print("\n--- Q3: TASK taskType breakdown (resolved) ---")
    cur.execute("""
        SELECT t.taskType, COALESCE(s.value, 'UNKNOWN:' || CAST(t.taskType AS TEXT)) as type_name,
               COUNT(*) as cnt,
               SUM(t.endNs - t.startNs) / 1000.0 as total_us,
               AVG((t.endNs - t.startNs) / 1000.0) as avg_us,
               COUNT(DISTINCT t.streamId) as stream_count
        FROM TASK t
        LEFT JOIN STRING_IDS s ON CAST(t.taskType AS TEXT) = CAST(s.id AS TEXT)
        GROUP BY t.taskType
        ORDER BY cnt DESC
    """)
    print(f"{'Type':<10} {'TypeName':<35} {'Count':>8} {'Total_ms':>12} {'Avg_us':>10} {'Streams':>7}")
    print("-" * 84)
    for row in cur.fetchall():
        tt, tn, cnt, total_us, sc, avg_us = row
        total_ms = (total_us if total_us else 0) / 1000.0
        avg = avg_us if avg_us else 0
        print(f"{tt:<10} {tn:<35} {cnt:>8,} {total_ms:>12,.1f} {avg:>10,.1f} {sc:>7}")

    # ===== Q4: Key APIs -> TASK join details =====
    print("\n--- Q4: Key APIs connectionId join to TASK ---")
    key_apis = [
        'aclrtStreamWaitEvent', 'StreamWaitEvent',
        'aclrtSynchronizeStream', 'aclrtSynchronizeDeviceWithTimeout',
        'aclrtSynchronizeEvent',
        'aclrtRecordEvent', 'EventRecord',
        'aclrtMemcpyAsync', 'MemcpyAsync_Huge',
        'launch', 'aclrtLaunchKernelWithHostArgs', 'LaunchKernelV2',
        'hcom_allReduce_',
        'aclrtSynchronizeDevice'
    ]
    for api_name in key_apis:
        cur.execute("""
            SELECT COUNT(*) as cnt,
                   SUM(c.endNs - c.startNs) / 1000.0 as total_us,
                   AVG((c.endNs - c.startNs) / 1000.0) as avg_us
            FROM CANN_API c
            JOIN STRING_IDS s ON CAST(c.name AS TEXT) = CAST(s.id AS TEXT)
            WHERE s.value = ?
        """, (api_name,))
        row = cur.fetchone()
        if row and row[0] > 0:
            cnt, total_us, avg_us = row
            total_ms = (total_us if total_us else 0) / 1000.0
            avg = avg_us if avg_us else 0
            # Join stats
            cur.execute("""
                SELECT COUNT(DISTINCT t.streamId) as streams,
                       COUNT(DISTINCT c.connectionId) as conns,
                       COUNT(*) as matched_rows
                FROM CANN_API c
                JOIN STRING_IDS s ON CAST(c.name AS TEXT) = CAST(s.id AS TEXT)
                JOIN TASK t ON c.connectionId = t.connectionId
                WHERE s.value = ? AND c.connectionId != '' AND c.connectionId != '0'
            """, (api_name,))
            row2 = cur.fetchone()
            join_str = ""
            if row2 and row2[0] > 0:
                join_str = f"  -> TASK: {row2[2]} rows, {row2[0]} streams, {row2[1]} conns"
            print(f"  {api_name:<45} cnt={cnt:>7,}  tot={total_ms:>10,.1f}ms  avg={avg:>8,.1f}us{join_str}")

    # ===== Q5: aclrtStreamWaitEvent stream mapping =====
    print("\n--- Q5: aclrtStreamWaitEvent -> TASK stream distribution ---")
    cur.execute("""
        SELECT t.streamId, COUNT(DISTINCT c.connectionId) as conns,
               COUNT(DISTINCT t.globalTaskId) as tasks,
               SUM(t.endNs - t.startNs) / 1000.0 as task_total_us
        FROM CANN_API c
        JOIN STRING_IDS s ON CAST(c.name AS TEXT) = CAST(s.id AS TEXT)
        JOIN TASK t ON c.connectionId = t.connectionId
        WHERE s.value = 'aclrtStreamWaitEvent'
        GROUP BY t.streamId
        ORDER BY conns DESC
        LIMIT 15
    """)
    print(f"  {'StreamId':<15} {'Connections':>12} {'Tasks':>10} {'TaskTotal_ms':>14}")
    print("  " + "-" * 53)
    for row in cur.fetchall():
        sid, conns, tasks, total_us = row
        total_ms = (total_us if total_us else 0) / 1000.0
        print(f"  {str(sid):<15} {conns:>12,} {tasks:>10,} {total_ms:>14,.1f}")

    # ===== Q6: Timeline range =====
    print("\n--- Q6: Timeline range ---")
    cur.execute("SELECT MIN(startNs), MAX(endNs), COUNT(*) FROM TASK")
    mn, mx, cnt = cur.fetchone()
    if mn and mx:
        dur_s = (mx - mn) / 1e9
        print(f"  TASK: {cnt:,} tasks, {mn} -> {mx}, duration: {dur_s:.3f}s")
    cur.execute("SELECT MIN(startNs), MAX(endNs), COUNT(*) FROM CANN_API")
    mn, mx, cnt = cur.fetchone()
    if mn and mx:
        dur_s = (mx - mn) / 1e9
        print(f"  CANN_API: {cnt:,} calls, {mn} -> {mx}, duration: {dur_s:.3f}s")

    # ===== Q7: Compute kernel info =====
    print("\n--- Q7: COMPUTE_TASK_INFO sample ---")
    cur.execute("PRAGMA table_info(COMPUTE_TASK_INFO)")
    cols = [c[1] for c in cur.fetchall()]
    print(f"  Columns: {cols}")
    cur.execute("SELECT * FROM COMPUTE_TASK_INFO LIMIT 5")
    for row in cur.fetchall():
        print(f"  {row}")

    # ===== Q8: Communication task info =====
    print("\n--- Q8: COMMUNICATION_TASK_INFO sample ---")
    cur.execute("SELECT * FROM COMMUNICATION_TASK_INFO LIMIT 3")
    for row in cur.fetchall():
        print(f"  {row}")

    conn.close()
    print()
