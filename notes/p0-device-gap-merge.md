# P0: Device-Level Gap Merge + Derived Gap DB

> 实现日期：2026-06-24
> 基于 TraceLoom RFC "Stream Gap 归因"，第一阶段实现成果

## 做了什么

实现了从 "stream 内 gap 分析" 到 **"device 全局 productive timeline 级别的 gap 分析"** 的升级。

核心逻辑：

```
所有 stream 的 productive task (KERNEL + COMM)
    │
    ▼
合并重叠 → Device Busy Timeline (productive_active)
    │
    ▼
Busy 之间的空白 → Device Visible Gap (visible_productive_idle)
    │
    ▼
投影到各 stream 状态 + 归类 + 打标签 → Derived Gap DB
```

## 关键发现

### 1. Productive 时间占比极低（~3%）

| 指标 | Device 0 | Device 1 |
|---|---|---|
| 总 Trace 时长 | 68.5s | 67.5s |
| 总 Task 数 | 84,928 | 57,455 |
| Productive 时间 | 2.7s (3.9%) | 1.7s (2.5%) |
| Device Gap 数 (>100μs) | 7,444 | 7,258 |

> 3.9% 并不是说 NPU 利用率低——kickstart profile 包含了 engine init、ACL graph capture、HCCL setup、prefill、decode 全流程，大量时间是初始化开销。

### 2. Device Gap 分类分布（两设备一致）

| 类别 | Device 0 | Device 1 | 含义 |
|---|---|---|---|
| `runtime_control_present` | 33.3s (959 gaps) | 32.8s (935 gaps) | MODEL_MAINTAINCE / control tasks 在运行 |
| `no_observed_device_work` | 16.4s (5,147 gaps) | 16.9s (4,915 gaps) | 所有 stream 都没有可见任务 |
| `phase_change_boundary` | 15.0s (5 gaps) | 15.0s (5 gaps) | 巨型阶段边界 (>1s) |
| `blocked_by_visible_wait` | 1.0s (1,332 gaps) | 0.9s (1,400 gaps) | Stream 上有 EVENT_WAIT / NOTIFY_WAIT |
| `capture_control_present` | <1ms | <1ms | CAPTURE 控制任务 |

### 3. Top Hotspot 分析

最大的 10 个非阶段边界 gap 中：
- `no_observed_device_work` 类占据了 top 7（每个 ~1s）—— **这些是真正的"暗区"，需要进一步调查**
- `runtime_control_present` 占据了剩余位置（每个 ~700ms）

## 产出物

### Derived Gap DB

路径：`examples/kickstart_smoke/msprof_raw/traceloom_gap_db/derived_gaps.db`

表结构（对应 RFC proposed output schema）：

| 表 | 对应 RFC | 含义 |
|---|---|---|
| `traceloom_device_gap` | `traceloom_idle_explanation` | Device 级 gap 事件，含 category/confidence/reason |
| `traceloom_stream_gap` | — | Stream 内相邻 task 间的大 gap |
| `traceloom_stream_state` | `traceloom_stream_state` | Per-stream 可观测状态区间（RFC Layer 2） |
| `traceloom_device_interval` | `traceloom_device_interval` | Productive active 区间 |
| `traceloom_v_device_gap_summary` | View | 按类别和置信度的 device gap 汇总 |
| `traceloom_v_device_idle_hotspot` | View | Top 30 热点 gap（排除阶段边界） |

### 关键 SQL 查询

```sql
-- 查看所有的 device gap 类别分布
SELECT * FROM traceloom_v_device_gap_summary;

-- 查看最大的 20 个非阶段边界 gap
SELECT * FROM traceloom_v_device_idle_hotspot;

-- 把某个 gap 和原始 TASK 数据 JOIN
SELECT d.event_id, d.durMs, d.category, t.taskType, t.streamId
FROM traceloom_device_gap d
JOIN TASK t ON t.startNs BETWEEN d.startNs AND d.endNs
WHERE d.event_id = 'D0_DEV0_GAP6879';

-- 查看 stream-local gap 的 transition 模式
SELECT prev_class, next_class, COUNT(*) as cnt, SUM(durNs)/1e6 as total_ms
FROM traceloom_stream_gap
WHERE db_idx = 0
GROUP BY prev_class, next_class
ORDER BY total_ms DESC;
```

## 与 RFC 的对应

| RFC Layer | 本实现 | 状态 |
|---|---|---|
| Layer 1: Global Productive Timeline | `traceloom_device_interval` (productive_active) | ✅ 已实现 |
| Layer 2: Per-Stream State Timeline | `traceloom_stream_state` | ✅ 已实现 |
| Layer 3: Idle Explanation | `traceloom_device_gap` (category + confidence) | ⚠️ 初版（5 类，待细化） |
| Layer 4: Host Evidence | 未实现（留到下一步） | ❌ 待实现 |
| Layer 5-6: Aggregation | Views (`v_device_gap_summary`) | ⚠️ 基础视图 |

## 下一步

1. **Host Evidence 层**：对每个 device gap，查询重叠的 CANN_API，给 `queued_visible_task_delay` 和 `host_sync_api_present` 提供证据
2. **细化 runtime_control_present**：33s 的 control 时间中，哪些是 MODEL_MAINTAINCE（模型维护）？哪些是正常的阶段切换？
3. **接入 loop tree**：把 gap events 作为 timeline node 接到 TraceLoom 的 reconstruction 系统
4. **调查 no_observed_device_work**：最大 1s 的"完全无任务"gap 是什么？可能需要底层的 profiler 机制分析
