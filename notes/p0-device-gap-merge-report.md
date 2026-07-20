# P0 汇报：Device 级 Gap Merge 与 Derived Gap DB

> 2026-06-24

## 背景回顾

上次汇报中完成了 stream 内部的 gap transition 分析和 stream 标签化。本次按照学长的建议，将视角从单个 stream 升级到 device 全局，实现 productive timeline 级别的 gap 合并分析。

## 做了什么

### 核心算法

```
Step 1: 所有 stream 的 productive task (KERNEL + COMM) 合并重叠
        → Device Busy Timeline

Step 2: Busy 之间的空白段
        → Device Visible Gap Intervals

Step 3: 每个 device gap 投影到各 stream 的可观测状态
        → 按优先级归类 + 打标签 + 置信度

Step 4: 全部事件输出为 SQLite 表
        → Derived Gap DB（可直接 JOIN 原始 profiler 数据查询）
```

### 实现细节

**Task 分类规则**：

| Class | TaskType | 用途 |
|---|---|---|
| productive_compute | KERNEL_AIVEC, KERNEL_AICORE, KERNEL_MIX_* | 参与 busy timeline |
| comm | 通过 COMMUNICATION_TASK_INFO 关联 | 参与 busy timeline |
| wait | EVENT_WAIT, NOTIFY_WAIT | Gap 解释：blocked_by_visible_wait |
| capture | CAPTURE_WAIT, CAPTURE_RECORD, MEM_WRITE_VALUE | Gap 解释：capture_control_present |
| record | EVENT_RECORD, NOTIFY_RECORD, STARS_COMMON | Gap 解释：runtime_control_present |
| control | MODEL_MAINTAINCE, MODEL_EXECUTE, TASK_TIMEOUT_SET | Gap 解释：runtime_control_present |

**Device Gap 归因优先级**（参考 RFC）：

1. `phase_change_boundary`（>1s 且无 productive work）→ confidence: heuristic
2. `blocked_by_visible_wait`（有 EVENT_WAIT/NOTIFY_WAIT）→ confidence: confirmed
3. `capture_control_present`（有 CAPTURE 类 task）→ confidence: heuristic
4. `runtime_control_present`（有 record/control 类 task）→ confidence: heuristic
5. `queued_visible_task_delay`（有 productive work 但仍存在 device 级延迟）→ confidence: contextual
6. `no_observed_device_work`（所有 stream 均无可见任务）→ confidence: unknown

**性能优化**：使用二分查找（bisect）做 gap-to-stream 投影，避免对每个 gap 遍历全部任务。两个设备各 ~8 万 task、~7,400 个 gap 在数秒内完成。

## 关键发现

### 1. Productive 时间占比

| 指标 | Device 0 | Device 1 |
|---|---|---|
| Trace 总时长 | 68.5s | 67.5s |
| Task 总数 | 84,928 | 57,455 |
| **Productive 时间** | **2.7s (3.9%)** | **1.7s (2.5%)** |
| Gap 总数 (>100μs) | 7,444 | 7,258 |

> 3.9% 不代表 NPU 利用率低。Kickstart profile 覆盖了 engine init → ACL graph capture → HCCL setup → prefill → decode 的**全流程**，大量时间为初始化开销。实际的 inference loop（decode）部分 productive 占比应该显著更高。后续分析可以按阶段切片。

### 2. Device Gap 分类分布（两设备高度一致）

| 类别 | Device 0 | Device 1 | 特征 |
|---|---|---|---|
| `runtime_control_present` | **33.3s** (959 gaps) | **32.8s** (935 gaps) | MODEL_MAINTAINCE 和 control task 覆盖 |
| `no_observed_device_work` | 16.4s (5,147 gaps) | 16.9s (4,915 gaps) | 所有 stream 均无可见 task |
| `phase_change_boundary` | 15.0s (5 gaps) | 15.0s (5 gaps) | >1s 的无 productive 阶段边界 |
| `blocked_by_visible_wait` | 1.0s (1,332 gaps) | 0.9s (1,400 gaps) | 有 EVENT_WAIT/NOTIFY_WAIT |
| `capture_control_present` | <1ms (1 gap) | <1ms (3 gaps) | CAPTURE 控制任务 |

### 3. Top Hotspot 分析（排除阶段边界后最大的 gap）

最大的 10 个非阶段边界 gap：

| 排名 | Event ID | 时长 | 类别 |
|---|---|---|---|
| 1 | D0_DEV0_GAP6879 | 989.8ms | no_observed_device_work |
| 2 | D1_DEV1_GAP6727 | 957.3ms | no_observed_device_work |
| 3 | D0_DEV0_GAP181 | 927.7ms | no_observed_device_work |
| 4 | D0_DEV0_GAP93 | 874.7ms | no_observed_device_work |
| 5 | D1_DEV1_GAP85 | 852.3ms | no_observed_device_work |
| 6 | D0_DEV0_GAP24 | 818.1ms | no_observed_device_work |
| 7 | D1_DEV1_GAP14 | 793.9ms | no_observed_device_work |
| 8-10 | — | ~700ms | runtime_control_present |

> Top 7 全部是 `no_observed_device_work`（不可解释的暗区），每个接近 1 秒。这些是后续调查的重点——可能是 profiler 盲区、真正的硬件 idle、或者非 ASCEND 控制的等待（如 CPU 端 Python 代码执行）。

### 4. 两设备对比：高度一致

两个 device 在 gap 规模、类别分布、top hotspot 模式上高度一致。这表明分析框架在跨设备比较场景下是可靠的。

## 产出物

### Derived Gap DB

**路径**：`examples/kickstart_smoke/msprof_raw/traceloom_gap_db/derived_gaps.db`

**表结构**：

| 表 | 对应 RFC | 行数 (Dev0) | 说明 |
|---|---|---|---|
| `traceloom_device_gap` | `traceloom_idle_explanation` | 7,444 | Device 级 gap 事件 |
| `traceloom_stream_gap` | — | 12,342 | Stream 内相邻 task 间的大 gap |
| `traceloom_stream_state` | `traceloom_stream_state` | 84,928 | Per-stream 状态区间 (Layer 2) |
| `traceloom_device_interval` | `traceloom_device_interval` | 32,186 | Productive active 区间 (Layer 1) |

**关键字段**（`traceloom_device_gap`）：

| 字段 | 说明 |
|---|---|
| event_id | 唯一标识，如 `D0_DEV0_GAP6879` |
| gap_type | `device_visible_gap` |
| category | 6 类归因标签 |
| confidence | confirmed / heuristic / contextual / unknown |
| reason | 人类可读的解释文字 |
| stream_states | JSON：每个 stream 在该 gap 期间的可观测状态 |
| phase_boundary | 是否为阶段边界 |

**Views**：

- `traceloom_v_device_gap_summary` — 按类别×置信度汇总
- `traceloom_v_device_idle_hotspot` — Top 30 热点（排除阶段边界）
- `traceloom_v_stream_gap_summary` — Stream gap transition 汇总
- `traceloom_v_device_gap_as_interval` — Device gap 以 interval 形式呈现（用于接入 timeline reconstruction）

### 可查询示例

```sql
-- 查最大的 device gap 类别
SELECT category, COUNT(*) as cnt, SUM(durMs) as total_ms
FROM traceloom_device_gap
GROUP BY category ORDER BY total_ms DESC;

-- 查某个 gap 区间内有哪些 TASK
SELECT d.event_id, d.durMs, t.taskType, t.streamId
FROM traceloom_device_gap d
JOIN TASK t ON t.startNs BETWEEN d.startNs AND d.endNs
WHERE d.event_id = 'D0_DEV0_GAP6879';

-- 跨设备对比
SELECT device_id, category, SUM(durMs)
FROM traceloom_device_gap
GROUP BY device_id, category;
```

## 与 RFC 的对应关系

| RFC | 实现状态 | 备注 |
|---|---|---|
| **Layer 1**: Global Productive Timeline | ✅ `traceloom_device_interval` | productive task union → interval merge |
| **Layer 2**: Per-Stream State Timeline | ✅ `traceloom_stream_state` | 每个 stream 的状态区间 |
| **Layer 3**: Idle Explanation | ⚠️ 5 类初版 | 待加入 queued_visible_task_delay 和 host_sync_api_present 的 host API 证据 |
| **Layer 4**: Host Evidence | ❌ 未实现 | 下一步 |
| **Layer 5-6**: Aggregation | ⚠️ 基础 views 可用 | 待接入 TraceLoom loop tree |

## 当前局限

1. **`no_observed_device_work` 太多（~16s）**：这些是完全无法解释的 gap。需要在 Host Evidence 层中查 CANN_API 表来区分"host 同步等待"和"真正的 profiler 盲区"
2. **`runtime_control_present` 需要细化**：33s 的 control 时间中，MODEL_MAINTAINCE 占大多数。需要进一步确认这些是否是 ACL Graph 执行的正常开销
3. **未接入 CANN_API host evidence**：connectionId 关联和 host sync API 的时间重叠检测尚未加入，导致 `queued_visible_task_delay` 和 `host_sync_api_present` 两个类别目前为空
4. **Task 分类仍有边界情况**：部分 taskType 可能被错误归类为 control 或 unknown，需要进一步对照 Ascend profiler 文档验证

## 下一步计划

基于学长的四个方向建议：

1. ✅ **Device Gap Merge** — 本次完成
2. 🔜 **完善 Host Evidence（Layer 4）**：对每个 gap 查询重叠的 CANN_API，实现 `host_sync_api_present` 和 `queued_visible_task_delay` 归因
3. 🔜 **接入 Timeline Reconstruction**：将 `traceloom_v_device_gap_as_interval` 作为 node 接入 TraceLoom 现有的 loop tree
4. 🔜 **源码级调查**：查 Ascend/CANN 开源仓库中 MODEL_MAINTAINCE、CAPTURE_WAIT 的定义，验证当前的归类假设，避免将 profiler artifact 当 runtime 行为

---

*代码和 DB 均已留在仓库中，可直接复现。*
