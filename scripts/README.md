# Gap Analysis Scripts

Device-level and stream-level gap analysis for TraceLoom msprof profiles.

## Pipeline

```text
query_msprof.py          → 探索 msprof DB 结构，列出 CANN_API / TASK 类型
query_stream_gaps.py     → Per-stream 相邻事件 gap 分析 + 标签归类
device_gap_merge.py      → Device 级 productive timeline merge (P0)
p2_derived_gap_db.py     → Derived Gap DB 生成，含 event label 定义 (P2)
analyze_device_gaps.py   → Device gap 归因深度查询 + 交叉验证
p3_p4_gap_integration.py → Timeline tree 集成 + 源码级 case study (P3+P4)
p5_host_evidence_layer.py → Host API Evidence Layer + msprof-analyze 借鉴 (P5)
```

## Usage

```bash
# Step 1: Explore DB structure
python scripts/query_msprof.py

# Step 2: Per-stream gap analysis
python scripts/query_stream_gaps.py

# Step 3: Device-level gap merge + derived DB
python scripts/device_gap_merge.py

# Step 4: Generate labeled gap DB (P2)
python scripts/p2_derived_gap_db.py

# Step 5: Deep analysis
python scripts/analyze_device_gaps.py

# Step 6: Timeline tree integration + case study (P3+P4)
python scripts/p3_p4_gap_integration.py

# Step 7: Host evidence layer (P5)
python scripts/p5_host_evidence_layer.py
```

## Output

All results are written to:
- `examples/kickstart_smoke/msprof_raw/traceloom_gap_db/derived_gaps_v2.db`
- `examples/kickstart_smoke/msprof_raw/traceloom_gap_db/tree-map-with-gaps.md`
- `examples/kickstart_smoke/msprof_raw/traceloom_gap_db/gap_definitions.json`
- `notes/gap-signal-case-studies.md`

## Derived Gap DB Schema

| Table | Description |
|---|---|
| `traceloom_gap_event` | Unified gap events (stream-local + device-visible + non-productive-interval) |
| `traceloom_event_label_definition` | 17 label definitions with gap type, category, confidence, description |
| `traceloom_gap_event_evidence` | Gap-to-original-TASK evidence links (for JOIN) |
| `traceloom_device_interval` | Unified device timeline (productive_active + visible_productive_idle) |
| `traceloom_gap_host_evidence` | Per-gap host API evidence (CANN_API overlap analysis) |
| `traceloom_stream_idle_ratio` | Per-stream idle/compute ratio (small operator blocking detection) |

## Views

- `traceloom_v_unified_gap_timeline` — Full device timeline with gap labels
- `traceloom_v_gap_summary_by_category` — Grouped gap statistics
- `traceloom_v_gap_hotspot` — Top actionable gaps
- `traceloom_v_enhanced_gap_diagnosis` — Full diagnosis with host evidence
- `traceloom_v_evidence_impact` — Evidence-driven category migration statistics
