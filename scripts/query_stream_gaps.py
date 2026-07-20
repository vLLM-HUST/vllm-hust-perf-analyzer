import sqlite3
import os

db_dir = r"D:\vllm-hust-perf-analyzer\examples\kickstart_smoke\msprof_raw"

# Threshold in ns: 50us = 50000ns for "significant" gap
THRESHOLD_NS = 50_000  # 50 microseconds
# Also look at larger thresholds
THRESHOLDS = [50_000, 100_000, 500_000, 1_000_000]  # 50us, 100us, 500us, 1ms

db_files = []
for root, dirs, files in os.walk(db_dir):
    for f in files:
        if f.endswith(".db"):
            db_files.append(os.path.join(root, f))

for db_path in db_files:
    rel = os.path.relpath(db_path, db_dir)
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    cur = conn.cursor()

    print(f"{'='*80}")
    print(f"DB: {rel}")
    print(f"{'='*80}")

    # ===== Step 1: Compute per-stream inter-task gaps =====
    print("\n--- A1: Per-stream inter-task gap distribution ---")
    cur.execute("""
        WITH task_with_next AS (
            SELECT
                t.streamId,
                t.globalTaskId,
                t.taskType,
                t.startNs,
                t.endNs,
                LEAD(t.startNs) OVER (
                    PARTITION BY t.streamId
                    ORDER BY t.startNs
                ) as next_startNs,
                LEAD(t.globalTaskId) OVER (
                    PARTITION BY t.streamId
                    ORDER BY t.startNs
                ) as next_globalTaskId,
                LEAD(t.taskType) OVER (
                    PARTITION BY t.streamId
                    ORDER BY t.startNs
                ) as next_taskType
            FROM TASK t
        )
        SELECT
            streamId,
            COUNT(*) as gap_count,
            COUNT(CASE WHEN next_startNs - endNs > ? THEN 1 END) as significant_gaps,
            MIN(next_startNs - endNs) / 1000.0 as min_gap_us,
            AVG(next_startNs - endNs) / 1000.0 as avg_gap_us,
            MAX(next_startNs - endNs) / 1000.0 as max_gap_us,
            SUM(next_startNs - endNs) / 1000.0 as total_gap_us
        FROM task_with_next
        WHERE next_startNs IS NOT NULL
        GROUP BY streamId
        ORDER BY significant_gaps DESC
        LIMIT 20
    """, (THRESHOLD_NS,))

    rows = cur.fetchall()
    print(f"Streams with most significant gaps (>{THRESHOLD_NS/1000:.0f}us):")
    print(f"{'Stream':<10} {'TotalGaps':>10} {'SigGaps':>8} {'Min_us':>10} {'Avg_us':>10} {'Max_us':>12} {'TotalGap_ms':>12}")
    print("-" * 76)
    for r in rows:
        print(f"{r['streamId']:<10} {r['gap_count']:>10,} {r['significant_gaps']:>8,} "
              f"{r['min_gap_us']:>10,.1f} {r['avg_gap_us']:>10,.1f} "
              f"{r['max_gap_us']:>12,.1f} {r['total_gap_us']/1000:>12,.1f}")

    # ===== Step 2: Top individual gaps with context =====
    print("\n--- A2: Top 30 inter-task gaps with full context ---")
    cur.execute("""
        WITH task_with_next AS (
            SELECT
                t.streamId,
                t.globalTaskId,
                t.taskType as prev_type_id,
                t.startNs as prev_start,
                t.endNs as prev_end,
                t.connectionId as prev_connId,
                LEAD(t.startNs) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) as next_start,
                LEAD(t.globalTaskId) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) as next_taskId,
                LEAD(t.taskType) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) as next_type_id,
                LEAD(t.connectionId) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) as next_connId
            FROM TASK t
        )
        SELECT
            tw.streamId,
            tw.prev_type_id,
            tw.prev_start,
            tw.prev_end,
            tw.next_type_id,
            tw.next_start,
            (tw.next_start - tw.prev_end) / 1000.0 as gap_us,
            tw.prev_connId,
            tw.next_connId
        FROM task_with_next tw
        WHERE tw.next_start IS NOT NULL
          AND (tw.next_start - tw.prev_end) > ?
        ORDER BY (tw.next_start - tw.prev_end) DESC
        LIMIT 40
    """, (THRESHOLD_NS,))

    # Resolve taskType names via STRING_IDS
    cur.execute("SELECT id, value FROM STRING_IDS")
    str_map = {str(r['id']): r['value'] for r in cur.fetchall()}

    rows = cur.fetchall()
    print(f"\n  Top 40 gaps > {THRESHOLD_NS/1000:.0f}us:")
    print(f"  {'Stream':<8} {'Gap_ms':>10} {'PrevType':<25} {'NextType':<25} {'PrevEnd_ns':>18} {'NextStart_ns':>18}")
    print("  " + "-" * 108)

    for r in rows:
        prev_type = str_map.get(str(r['prev_type_id']), f"ID:{r['prev_type_id']}")
        next_type = str_map.get(str(r['next_type_id']), f"ID:{r['next_type_id']}")
        gap_ms = r['gap_us'] / 1000.0
        print(f"  {r['streamId']:<8} {gap_ms:>10,.1f} {prev_type:<25} {next_type:<25} "
              f"{r['prev_end']:>18} {r['next_start']:>18}")

    # ===== Step 3: Gap pattern analysis =====
    print("\n--- A3: Gap patterns (prev_type -> next_type transitions with large gaps) ---")
    cur.execute("""
        WITH task_with_next AS (
            SELECT
                t.streamId,
                t.taskType as prev_type,
                LEAD(t.taskType) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) as next_type,
                LEAD(t.startNs) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) - t.endNs as gap_ns
            FROM TASK t
        )
        SELECT
            prev_type,
            next_type,
            COUNT(*) as occurrence,
            MIN(gap_ns) / 1000.0 as min_gap_us,
            AVG(gap_ns) / 1000.0 as avg_gap_us,
            MAX(gap_ns) / 1000.0 as max_gap_us,
            SUM(gap_ns) / 1000.0 as total_gap_us,
            COUNT(CASE WHEN gap_ns > ? THEN 1 END) as large_gaps
        FROM task_with_next
        WHERE next_type IS NOT NULL
        GROUP BY prev_type, next_type
        HAVING large_gaps > 0 OR AVG(gap_ns) > ?
        ORDER BY large_gaps DESC, avg_gap_us DESC
        LIMIT 30
    """, (THRESHOLD_NS, THRESHOLD_NS))

    rows = cur.fetchall()
    print(f"  Transitions with large gaps:")
    print(f"  {'PrevType':<25} {'NextType':<25} {'Count':>7} {'Large':>6} {'Avg_us':>10} {'Max_us':>12} {'Total_ms':>10}")
    print("  " + "-" * 119)
    for r in rows:
        prev = str_map.get(str(r['prev_type']), f"ID:{r['prev_type']}")
        next_t = str_map.get(str(r['next_type']), f"ID:{r['next_type']}")
        print(f"  {prev:<25} {next_t:<25} {r['occurrence']:>7,} {r['large_gaps']:>6,} "
              f"{r['avg_gap_us']:>10,.1f} {r['max_gap_us']:>12,.1f} {r['total_gap_us']/1000:>10,.1f}")

    # ===== Step 4: What's happening on other streams during the largest gaps? =====
    print("\n--- A4: Largest single gap — what else was running on device? ---")
    cur.execute("""
        WITH task_with_next AS (
            SELECT
                t.streamId,
                t.endNs as gap_start,
                LEAD(t.startNs) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) as gap_end,
                LEAD(t.startNs) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) - t.endNs as gap_ns
            FROM TASK t
        )
        SELECT streamId, gap_start, gap_end, gap_ns / 1000.0 as gap_us
        FROM task_with_next
        ORDER BY gap_ns DESC
        LIMIT 3
    """)

    top_gaps = cur.fetchall()
    for rank, gap in enumerate(top_gaps, 1):
        print(f"\n  Gap #{rank}: stream {gap['streamId']}, "
              f"gap {gap['gap_us']/1000:.1f}ms ({gap['gap_us']:.0f}us)")
        print(f"  gap_start={gap['gap_start']}, gap_end={gap['gap_end']}")

        # Tasks active on other streams during this gap
        cur.execute("""
            SELECT t.streamId, COUNT(*) as task_count,
                   SUM(t.endNs - t.startNs) / 1000.0 as active_us
            FROM TASK t
            WHERE t.streamId != ?
              AND t.startNs < ? AND t.endNs > ?
            GROUP BY t.streamId
            HAVING active_us > ?
            ORDER BY active_us DESC
            LIMIT 10
        """, (gap['streamId'], gap['gap_end'], gap['gap_start'], THRESHOLD_NS / 1000.0))

        other_streams = cur.fetchall()
        if other_streams:
            print(f"  Other streams active during this gap:")
            for os_row in other_streams:
                print(f"    stream {os_row['streamId']}: {os_row['task_count']} tasks, "
                      f"{os_row['active_us']/1000:.1f}ms active")
        else:
            print(f"  No other streams active — potential true idle or profiler blind spot")

        # CANN_API calls overlapping this gap
        cur.execute("""
            SELECT COALESCE(s.value, c.name) as api_name, COUNT(*) as cnt,
                   SUM(c.endNs - c.startNs) / 1000.0 as total_us,
                   AVG((c.endNs - c.startNs) / 1000.0) as avg_us
            FROM CANN_API c
            JOIN STRING_IDS s ON CAST(c.name AS TEXT) = CAST(s.id AS TEXT)
            WHERE c.startNs < ? AND c.endNs > ?
            GROUP BY c.name
            HAVING total_us > ?
            ORDER BY cnt DESC
            LIMIT 10
        """, (gap['gap_end'], gap['gap_start'], 100.0))  # Only APIs > 100us total

        apis = cur.fetchall()
        if apis:
            print(f"  Overlapping CANN_API calls:")
            for api in apis:
                print(f"    {api['api_name']:<45} cnt={api['cnt']:>6,} "
                      f"total={api['total_us']/1000:>8,.1f}ms avg={api['avg_us']:>8,.1f}us")
        else:
            print(f"  No significant CANN_API calls overlapping")

    # ===== Step 5: Gap type classification =====
    print("\n\n--- A5: Gap classification by context ---")
    print("Classifying each gap by its surrounding task types...")

    cur.execute("""
        WITH task_with_next AS (
            SELECT
                t.streamId,
                t.taskType as prev_type,
                t.endNs as gap_start,
                t.globalTaskId,
                LEAD(t.startNs) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) as gap_end,
                LEAD(t.taskType) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) as next_type,
                LEAD(t.startNs) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) - t.endNs as gap_ns
            FROM TASK t
        )
        SELECT
            CASE
                WHEN gap_ns > 1000000 THEN '>1ms'
                WHEN gap_ns > 500000 THEN '500us-1ms'
                WHEN gap_ns > 100000 THEN '100us-500us'
                WHEN gap_ns > 50000 THEN '50us-100us'
                ELSE '<50us'
            END as gap_range,
            COUNT(*) as count,
            AVG(gap_ns) / 1000.0 as avg_gap_us,
            SUM(gap_ns) / 1000.0 as total_gap_us
        FROM task_with_next
        WHERE next_type IS NOT NULL
        GROUP BY gap_range
        ORDER BY MIN(gap_ns)
    """)

    print(f"\n  Gap size distribution:")
    print(f"  {'Range':<15} {'Count':>8} {'Avg_us':>10} {'Total_ms':>12} {'PctOfTotal':>10}")
    print("  " + "-" * 59)
    total_gap = 0
    gaps = []
    for r in cur.fetchall():
        gaps.append((r['gap_range'], r['count'], r['avg_gap_us'], r['total_gap_us']))
        total_gap += r['total_gap_us']
    for g in gaps:
        pct = g[3] / total_gap * 100 if total_gap > 0 else 0
        print(f"  {g[0]:<15} {g[1]:>8,} {g[2]:>10,.1f} {g[3]/1000:>12,.1f} {pct:>9,.1f}%")

    # ===== Step 6: Specific gap labels =====
    print("\n--- A6: Try labeling large gaps (>100us) ---")

    # Fetch large gaps with context
    cur.execute("""
        WITH task_with_next AS (
            SELECT
                t.streamId,
                t.globalTaskId,
                t.taskType as prev_type,
                t.endNs as gap_start,
                t.connectionId as prev_conn,
                LEAD(t.startNs) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) as gap_end,
                LEAD(t.taskType) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) as next_type,
                LEAD(t.connectionId) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) as next_conn,
                LEAD(t.startNs) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) - t.endNs as gap_ns
            FROM TASK t
        )
        SELECT *
        FROM task_with_next
        WHERE next_type IS NOT NULL
          AND gap_ns > 100000  -- >100us
        ORDER BY gap_ns DESC
        LIMIT 100
    """)

    large_gaps = cur.fetchall()
    print(f"  Found {len(large_gaps)} gaps > 100us\n")

    # Classify each gap
    labels_count = {}
    stream_labels = {}

    for g in large_gaps:
        prev_type = str_map.get(str(g['prev_type']), f"ID:{g['prev_type']}")
        next_type = str_map.get(str(g['next_type']), f"ID:{g['next_type']}")

        label = None

        # Label: Intra-kernel gap (same kernel type continues)
        if prev_type == next_type and 'KERNEL' in prev_type.upper():
            label = "intra_kernel_launch_gap"
        # Label: Event wait related
        elif 'EVENT_WAIT' in prev_type or 'EVENT_WAIT' in next_type:
            label = "event_synchronization_boundary"
        # Label: Notify wait related
        elif 'NOTIFY_WAIT' in prev_type or 'NOTIFY_WAIT' in next_type:
            label = "notify_synchronization_boundary"
        # Label: Capture related
        elif 'CAPTURE' in prev_type or 'CAPTURE' in next_type:
            label = "capture_control_boundary"
        # Label: Memory copy to compute
        elif 'MEMCPY' in prev_type and 'KERNEL' in next_type:
            label = "memcpy_to_compute_transition"
        elif 'KERNEL' in prev_type and 'MEMCPY' in next_type:
            label = "compute_to_memcpy_transition"
        # Label: Record event gaps
        elif 'EVENT_RECORD' in prev_type or 'EVENT_RECORD' in next_type:
            label = "event_record_boundary"
        # Label: Model maintenance related
        elif 'MODEL' in prev_type or 'MODEL' in next_type:
            label = "model_maintenance_boundary"
        # Label: Compute to compute (likely host dispatch delay)
        elif 'KERNEL' in prev_type and 'KERNEL' in next_type:
            label = "compute_to_compute_dispatch_gap"
        else:
            label = "other_unclassified"

        labels_count[label] = labels_count.get(label, 0) + 1
        sid = str(g['streamId'])
        if sid not in stream_labels:
            stream_labels[sid] = {}
        stream_labels[sid][label] = stream_labels[sid].get(label, 0) + 1

    print(f"  Label distribution (gaps > 100us):")
    print(f"  {'Label':<45} {'Count':>8} {'Pct':>7}")
    print("  " + "-" * 62)
    total_labeled = sum(labels_count.values())
    for label, cnt in sorted(labels_count.items(), key=lambda x: -x[1]):
        pct = cnt / total_labeled * 100 if total_labeled > 0 else 0
        print(f"  {label:<45} {cnt:>8} {pct:>6,.1f}%")

    # Stream-level label distribution
    print(f"\n  Per-stream label breakdown (top streams by total large gaps):")
    print(f"  {'Stream':<8} {'Total':>6} ", end="")
    all_labels = sorted(set(l for sd in stream_labels.values() for l in sd))
    for l in all_labels:
        print(f"{l[:12]:>12} ", end="")
    print("\n  " + "-" * (20 + 13 * len(all_labels)))

    sorted_streams = sorted(stream_labels.items(),
                           key=lambda x: sum(x[1].values()), reverse=True)[:15]
    for sid, s_labels in sorted_streams:
        total = sum(s_labels.values())
        print(f"  {sid:<8} {total:>6} ", end="")
        for l in all_labels:
            cnt = s_labels.get(l, 0)
            print(f"{cnt:>12} ", end="")
        print()

    # ===== Step 7: Stream timeline samples =====
    print("\n--- A7: Sample timeline fragments from high-gap streams ---")

    # Get top 3 streams by total gap time
    cur.execute("""
        WITH gaps AS (
            SELECT
                t.streamId,
                LEAD(t.startNs) OVER (
                    PARTITION BY t.streamId ORDER BY t.startNs
                ) - t.endNs as gap_ns
            FROM TASK t
        )
        SELECT streamId, SUM(gap_ns) / 1000.0 as total_gap_us, COUNT(*) as gap_cnt
        FROM gaps
        WHERE gap_ns IS NOT NULL AND gap_ns > ?
        GROUP BY streamId
        ORDER BY total_gap_us DESC
        LIMIT 5
    """, (100_000,))

    top_streams = [r['streamId'] for r in cur.fetchall()]

    for sid in top_streams[:3]:
        print(f"\n  Stream {sid} — first 25 events (showing gaps > {THRESHOLD_NS/1000:.0f}us):")

        cur.execute("""
            WITH events AS (
                SELECT
                    t.globalTaskId,
                    t.taskType,
                    t.startNs,
                    t.endNs,
                    (t.endNs - t.startNs) / 1000.0 as dur_us,
                    LEAD(t.startNs) OVER (
                        PARTITION BY t.streamId ORDER BY t.startNs
                    ) - t.endNs as gap_ns,
                    LEAD(t.taskType) OVER (
                        PARTITION BY t.streamId ORDER BY t.startNs
                    ) as next_type
                FROM TASK t
                WHERE t.streamId = ?
            )
            SELECT * FROM events
            WHERE gap_ns IS NOT NULL AND gap_ns > ?
            ORDER BY gap_ns DESC
            LIMIT 10
        """, (sid, THRESHOLD_NS))

        print(f"  {'Gap_us':>9} {'Dur_us':>9} {'PrevType':<25} {'NextType':<25}")
        print("  " + "-" * 75)
        for r in cur.fetchall():
            pt = str_map.get(str(r['taskType']), f"ID:{r['taskType']}")
            nt = str_map.get(str(r['next_type']), f"ID:{r['next_type']}")
            print(f"  {r['gap_ns']/1000:>9,.1f} {r['dur_us']:>9,.1f} {pt:<25} {nt:<25}")

    conn.close()
    print("\n" + "="*80 + "\n")
