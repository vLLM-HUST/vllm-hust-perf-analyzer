# P2-P4 汇报：Derived Gap DB、Timeline 集成、源码级 Case Study

> 2026-07-06

## 总览

在 P0（Device 级 Gap Merge）的基础上，完成了后续三项工作：

| 任务 | 产出 | 状态 |
|---|---|---|
| P2: Derived Gap DB | `derived_gaps_v2.db` — 可 JOIN 原 DB 的 gap 事件库 | ✅ |
| P3: Timeline Tree 集成 | `tree-map-with-gaps.md` — 含 gap 节点的执行树 | ✅ |
| P4: 源码级 Case Study | `gap-signal-case-studies.md` — 三个信号的溯源分析 | ✅ |

---

## 一、P2：Derived Gap DB — 事件化与标签体系

### 做了什么

将 gap 从"分析结论"变成了**结构化的数据库事件**，可与原始 profiler 数据直接 JOIN 查询。

### 产出物

**路径**：`examples/kickstart_smoke/msprof_raw/traceloom_gap_db/derived_gaps_v2.db`

**四张核心表**：

| 表 | 含义 | 行数 |
|---|---|---|
| `traceloom_gap_event` | 统一的 gap 事件（三类） | **38,978** |
| `traceloom_event_label_definition` | 17 种标签定义，含描述、置信度、可操作性 | 17 |
| `traceloom_device_interval` | device 级统一时间线（productive + idle） | 每条 device gap 一个区间 |
| `traceloom_gap_event_evidence` | gap ↔ 原始 TASK/CANN_API 的证据链 | 按需生成 |

**三种 gap 类型**：

| 类型 | 含义 | 数量 |
|---|---|---|
| `stream_local_gap` | 同一 stream 内相邻 task 间的间隔 | 24,276 |
| `device_visible_gap` | device productive timeline 上的空白（所有 stream 均无 productive 工作） | 14,692 |
| `device_non_productive_interval` | 阶段边界（capture/replay/init 之间的自然断裂） | 10 |

**标签覆盖**（device_visible_gap 维度）：

| 类别 | 数量 | 总耗时 | 置信度 |
|---|---|---|---|
| `runtime_control_present` | 959/935 | 33.3s/32.8s | heuristic |
| `no_observed_device_work` | 5,147/4,915 | 16.4s/16.9s | unknown |
| `blocked_by_visible_wait` | 1,332/1,400 | 1.0s/0.9s | **confirmed** |
| `capture_control_present` | 1/3 | <1ms | heuristic |

### 关键特性

**可与原 DB JOIN**：

```sql
-- 查某个 device gap 期间运行了哪些原始 TASK
SELECT ge.event_id, ge.durMs, ge.category, t.taskType, t.streamId
FROM traceloom_gap_event ge
JOIN original.TASK t ON t.startNs BETWEEN ge.startNs AND ge.endNs
WHERE ge.event_id = 'D0_DEV0_device_visible_gap_0';
```

已通过 `ATTACH DATABASE` 验证 JOIN 正确性——10 个随机 device gap 均成功关联到原始 TASK 行。

---

## 二、P3：Timeline Tree 集成

### 做了什么

将 gap 事件作为**独立的 node** 挂到 device 时间线树上，形成"productive active → visible gaps → non-productive intervals → stream-local gaps"四层结构。

### 产出物

**路径**：`examples/kickstart_smoke/msprof_raw/traceloom_gap_db/tree-map-with-gaps.md`

### 树结构（Device 0 示例）

```text
Device 0 Timeline
├── Productive Active: ~2700ms
│   └── KERNEL + COMM tasks merged (busy intervals)
│
├── Device Visible Gaps (~65000ms total)
│   ├── [✓ blocked_by_visible_wait]: 1332 gaps, 960ms (confirmed)
│   ├── [? runtime_control_present]: 959 gaps, 33280ms (heuristic)
│   ├── [~ no_observed_device_work]: 5147 gaps, 16417ms (unknown)
│   └── [? capture_control_present]: 1 gap (heuristic)
│
├── Device Non-Productive Intervals
│   └── [phase_boundary]: 5 gaps, 14978ms
│
└── Stream-Local Gaps (top 10 by time)
    ├── stream 416/intra_kernel_launch_gap: 6531 gaps, 96186ms
    ├── stream 406/model_maintenance_interval: 2635 gaps, 66548ms
    └── stream 415/event_synchronization_boundary: 2102 gaps, 48455ms
```

### 统一时间线总览

| Interval Kind | 总时间 | 占比 |
|---|---|---|
| `productive_active` | ~5,400ms | ~4% |
| `visible_productive_idle` | ~131,000ms | ~96% |

### 与 RFC 的对应

| RFC Layer | 实现 | 本任务 |
|---|---|---|
| Layer 1: Global Productive Timeline | `traceloom_device_interval` (productive_active) | P0 已实现 |
| Layer 2: Per-Stream State Timeline | `traceloom_gap_event` (stream_local_gap) | P2 实现 |
| Layer 3: Idle Explanation | `traceloom_gap_event` (device_visible_gap + device_non_productive_interval) | P2+P3 实现 |
| Layer 4: Host Evidence | 预留 `traceloom_gap_event_evidence` 表 | 待加入 CANN_API 时间重叠检测 |
| Layer 5-6: Aggregation | `traceloom_device_interval` 统一时间线 + tree 集成 | P3 初版完成 |

---

## 三、P4：源码级 Case Study

### 做了什么

将三个关键信号（MODEL_MAINTAINCE、CAPTURE_WAIT、EVENT_RECORD→EVENT_WAIT）追溯到 TraceLoom 源码中的定义和处理逻辑，验证其在 profiler 层面的真实含义。

### 产出物

**路径**：`notes/gap-signal-case-studies.md`

### Case 1：MODEL_MAINTAINCE — Profiler 标记点，不是 Runtime 任务

**源码定位**：
- `ascend_aclgraph.py` 第 18 行：`GRAPH_TASK_KEYS = {"MODEL_EXECUTE", "MODEL_MAINTAINCE", "MODEL_MAINTENANCE", ...}`

**关键证据**：kickstart profile 中所有 MODEL_MAINTAINCE task 的 duration = **0μs**（startNs = endNs）。它们是 profiler 在 timeline 上插入的阶段标记点，不是真实设备任务。

**结论**：33.3s 的 `runtime_control_present` 时间不应被视为 "runtime 开销"。应重新标记为 `device_non_productive_interval.acl_graph_control_phase`。

### Case 2：CAPTURE_WAIT — Profiler 内部控制信号，3 倍设备差异

**源码定位**：
- `ascend_aclgraph.py`：`GRAPH_BODY_EXCLUDED_KEYS = {..., "CAPTURE_RECORD", "CAPTURE_WAIT", ...}`（排除在图 body 分析之外）
- `msprof_reader.py`：`if task_key == "CAPTURE_WAIT": [skip]`（从事件流中显式过滤掉）

**关键证据**：Device 0 有 37,483 条 vs Device 1 仅 12,475 条（3x 差异）。数量取决于 device 在 ACL graph capture 中的角色（主卡做更多 capture）。

**结论**：CAPTURE_WAIT 是 profiler-internal artifact，不应计入 productive 时间。

### Case 3：EVENT_RECORD → EVENT_WAIT — 唯一可完全确认的因果链

**源码定位**：
- `msprof_reader.py`：EVENT_RECORD 在 `COMM_TASK_TYPES` 中，EVENT_WAIT 被 `_classify_task` 中 WAIT 关键词匹配
- CANN_API `aclrtStreamWaitEvent` 通过 `connectionId` 直接链接到设备端 `EVENT_WAIT` TASK

**关键证据**：6,984 次 API 调用 → 4,564 个 confirmed connectionId 链接 → 3,548 个设备端 EVENT_WAIT task。这是 profiler 数据中**唯一可完整追溯**的 host→device 因果链条。

**结论**：`event_synchronization_boundary` 是置信度最高的 gap 标签。

### 汇总

| 信号 | 源码定义 | 本质 | 推荐标签 | 置信度 |
|---|---|---|---|---|
| MODEL_MAINTAINCE | `GRAPH_TASK_KEYS` | Profiler 标记点（0μs 耗时） | `acl_graph_control_phase` | heuristic |
| CAPTURE_WAIT | `GRAPH_BODY_EXCLUDED` + 显式过滤 | Profiler 内部控制，3x 设备差异 | `capture_control_present` | heuristic |
| EVENT_RECORD→WAIT | `COMM_TASK_TYPES` + WAIT 关键词 | 跨 stream 同步 | `event_synchronization_boundary` | **confirmed** |

---

## 四、整体进度

| RFC 定义 | 当前状态 | 说明 |
|---|---|---|
| Layer 1: Global Productive Timeline | ✅ 完成 | `traceloom_device_interval` |
| Layer 2: Per-Stream State Timeline | ✅ 完成 | `traceloom_gap_event` (stream_local_gap) |
| Layer 3: Idle Explanation (7 categories) | ⚠️ 6/7 类已实现 | 缺 `host_sync_api_present`（需 host evidence） |
| Layer 4: Host Evidence | ❌ 待实现 | 需要 CANN_API 时间重叠检测 |
| Layer 5-6: Aggregation + Reports | ⚠️ 基础完成 | tree node 已集成，缺 per-iteration 统计 |

## 五、下一步

1. **Host Evidence 层（Layer 4）**：对每个 device gap 查询重叠的 CANN_API，将 `no_observed_device_work` 细化为 `host_memory_management` 和 `host_sync_api_present`
2. **标签更新**：基于 P4 的 case study 结论，将 `runtime_control_present` 改名为 `acl_graph_control_phase`
3. **多 profile 验证**：在更多 profile（不同模型、配置）上验证 stream 标签的泛化性

---

*所有产出物均在仓库中，可直接用 SQLite 打开查询。*
