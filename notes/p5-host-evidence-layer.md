# P5: Host API Evidence Layer + msprof-analyze 借鉴落地

> 2026-07-08

## 做了什么

基于 [PR: TraceLoom Native vs msprof-analyze 对比分析](pr-native-msprof-comparison.md)，
将华为 msprof-analyze 中三个可借鉴的设计落地到 TraceLoom gap 分析管线中。

## 借鉴 1：FreeAnalysis 的三层证据链

**来源**：msprof-analyze `FreeAnalysis.analyze_free_reason()` —— 对每个 free 区间，
区分 "Device task running" / "Idle PyTorch layer" / "Abnormal CANN layer"。

**落地**：对 11,960 个 device gap 逐一查询重叠的 CANN_API，按 5 类 host 活动归类：

| Host 活动 | 识别规则 | 对应 gap 类别 |
|---|---|---|
| sync_wait | `aclrtSynchronizeStream` 等 | `host_sync_api_present` |
| memory_mgmt | `DevMalloc`, `DevFree`, `aclrtMemcpy*` 等 | `host_memory_management` |
| launch | `launch`, `aclrtLaunchKernel*` 等 | `queued_visible_task_delay` |
| event_lifecycle | `aclrtCreateEvent*`, `aclrtDestroyEvent` 等 | `host_event_lifecycle` |
| graph_control | `aclmdlRICapture*` 等 | `runtime_control_present` |

**效果**：`no_observed_device_work`（之前完全不可解释的 16.4s dark time）被拆解为：

| 细化类别 | Gap 数 | 总时长 | 占比 |
|---|---|---|---|
| `queued_visible_task_delay` | 4,169 | 21.7s | — (从 productive gaps 迁移来) |
| `host_memory_management` | 2,849 | 8.9s | 54% |
| `host_sync_api_present` | 1,411 | 2.2s | 13% |
| `no_observed_device_work` (残余) | 1,632 | **0.5s** | 3% |
| `host_event_lifecycle` | 1 | <1ms | 0% |

> **未知时间从 16.4s → 0.5s，缩减了 97%。**

## 借鉴 2：OpScheduleAdvice 的小算子阻塞检测

**来源**：msprof-analyze `op_schedule_advice.py` —— 检测 `free_time > op_time * ratio` 的反模式。

**落地**：计算每个 stream 的 gap/task 时间比，诊断 host dispatch 瓶颈：

| Device | Stream | Role | Gap/Task Ratio | Diagnosis |
|---|---|---|---|---|
| 0 | 416 | compute_stream | 33.6x | CRITICAL |
| 0 | 415 | sync_stream | 20.0x | CRITICAL |
| 0 | 406 | maintenance_stream | 25.1x | CRITICAL |
| 1 | 708 | compute_stream | 33.3x | CRITICAL |
| 1 | 707 | sync_stream | 32.7x | CRITICAL |

> 注：高 ratio 在 kickstart profile 中是预期的——profile 覆盖了 full init + capture + inference，
> init/capture 阶段的 compute 极少而控制等待极多。对纯 decode 阶段切片分析时这个指标会更有意义。

## 借鉴 3：Dynamic Stream Labeling

**来源**：StepTraceTimeAnalysis 的 per-rank 数据处理模式。

**落地**：将 stream 标签从"硬编码 stream ID"改为"基于 pattern 占比的动态聚类"：

| Device 0 | Device 1 | 主导 Pattern | 占比 | Label |
|---|---|---|---|---|
| 416 | 708 | intra_kernel_launch_gap | 99.5% | compute_stream |
| 406 | 702 | model_maintenance_interval | 100% | maintenance_stream |
| 415 | 707 | event_synchronization_boundary | 99.1% | sync_stream |
| 418 | 710 | intra_kernel_launch_gap | 100% | compute_stream |
| 410 | 704 | event_record_boundary | 57-59% | record_stream |

同一个 kernel function（vLLM-Ascend runtime）在两 device 上产生了**完全一致的 stream 角色分配**，
验证了 pattern-based labeling 的泛化性。换模型或配置时标签不会失效。

## 新增的 DB Schema

`traceloom_gap_host_evidence` 表：

| Column | Type | Description |
|---|---|---|
| event_id | TEXT PK | Gap event ID (FK to traceloom_gap_event) |
| total_host_us | REAL | Total CANN_API time during gap |
| sync_us | REAL | Host sync API time |
| memory_us | REAL | Memory management time |
| launch_us | REAL | Kernel launch time |
| event_us | REAL | Event lifecycle time |
| graph_us | REAL | Graph control API time |
| activity_types | TEXT (JSON) | Active host activity categories |
| top_apis | TEXT (JSON) | Top 5 CANN_API calls |
| refined_category | TEXT | Post-evidence category |
| refined_confidence | TEXT | Post-evidence confidence |
| host_reason | TEXT | Human-readable explanation |

新增 Views：
- `traceloom_v_enhanced_gap_diagnosis` — 含 host evidence 的全字段诊断视图
- `traceloom_v_evidence_impact` — 证据对归因分类的影响统计

## 当前 RFC 覆盖状态

| RFC Layer | 状态 | 备注 |
|---|---|---|
| Layer 1: Productive Timeline | ✅ | `traceloom_device_interval` |
| Layer 2: Stream State Timeline | ✅ | `traceloom_gap_event` (stream_local_gap) |
| Layer 3: Idle Explanation (7 categories) | ✅ | 7/7 类全部实现 |
| **Layer 4: Host Evidence** | ✅ | **本次完成** — `traceloom_gap_host_evidence` |
| Layer 5-6: Aggregation | ⚠️ | 基础 views 完成 |
