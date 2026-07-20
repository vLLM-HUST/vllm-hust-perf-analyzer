# P5 汇报：Host API Evidence Layer + 华为 msprof-analyze 借鉴落地

> 2026-07-08 | 分支: `feature/derived-gap-db-timeline-integration`

## 概述

基于[上一轮对比分析](pr-native-msprof-comparison.md)的结论，将华为 msprof-analyze 中三个可借鉴的
设计落地到 TraceLoom gap 分析管线中。核心成果：**实现了 RFC Layer 4（Host Evidence），
将不可解释的 gap 时间从 16.4s 缩减到 0.5s（−97%）**。

## 借鉴 1：Host API Evidence Layer → 拆解 no_observed_device_work

### 来源

msprof-analyze 的 `FreeAnalysis` 对每个 free 区间区分三层根因：
- Device task running（设备有 task 在跑，不是真 idle）
- Idle PyTorch layer（前端没下发任务）
- Abnormal CANN layer（CANN 下发瓶颈）

TraceLoom 之前只有两层（Device Task → CANN_API），且 `no_observed_device_work`
（16.4s, 占全部 gap 的 25%）完全没有细分。

### 实现

对 derived_gaps_v2.db 中 11,960 个 device gap（涵盖 `no_observed_device_work`、
`runtime_control_present`、`capture_control_present`），逐一查询每个 gap 区间内重叠的
CANN_API 调用，按 5 类 host 活动归类：

| Host 活动类型 | 识别 API | 示例 |
|---|---|---|
| sync_wait | aclrtSynchronizeStream, aclrtSynchronizeDeviceWithTimeout, aclrtSynchronizeEvent | Host 在等设备 |
| memory_mgmt | DevMalloc, DevFree, HostMalloc, aclrtMemcpy* | Host 在做内存分配/释放/拷贝 |
| launch | launch, aclrtLaunchKernelWithHostArgs, LaunchKernelV2 | Host 在提交 kernel |
| event_lifecycle | aclrtCreateEvent*, aclrtDestroyEvent, aclrtQueryEventStatus | Event 资源生命周期 |
| graph_control | aclmdlRICapture*, ModelExecute | ACL Graph 控制 |

### 效果：no_observed_device_work 被拆解

| 迁移后类别 | Gap 数量 | 总时长 | 原始类别 |
|---|---|---|---|
| `host_memory_management` | 2,849 | **8.9s** | 来自 no_observed_device_work |
| `host_sync_api_present` | 1,411 | **2.2s** | 来自 no_observed_device_work |
| `host_event_lifecycle` | 1 | <1ms | 来自 no_observed_device_work |
| `no_observed_device_work`（残余） | 1,632 | **0.5s** | 真正无法解释的 dark interval |
| `queued_visible_task_delay` | 4,169 | 21.7s | 从 runtime_control_present 重新归类 |
| `runtime_control_present`（保持） | 1,894 | 66.1s | 确认为 MODEL_MAINTAINCE / control |
| `capture_control_present`（保持） | 4 | <1ms | 保持原标签 |

```
Before:  no_observed_device_work = 16.4s  (25% of all gap time)
After:   no_observed_device_work =  0.5s  ( 3% of all gap time)

97% 的未知时间被解释。其中 8.9s 是 host 内存管理，2.2s 是 host 同步等待。
```

### 新增 DB 对象

**表 `traceloom_gap_host_evidence`**：

| 字段 | 说明 |
|---|---|
| event_id → traceloom_gap_event | FK 关联 |
| total_host_us, sync_us, memory_us, launch_us, event_us, graph_us | 各类 host 活动耗时 |
| activity_types | JSON: host 活动类别列表 |
| top_apis | JSON: 该 gap 期间最活跃的 5 个 CANN_API |
| refined_category, refined_confidence | Host 证据增强后的类别和置信度 |
| host_reason | 人类可读的解释文字 |

**Views**：
- `traceloom_v_enhanced_gap_diagnosis` — 带 host evidence 的完整诊断视图
- `traceloom_v_evidence_impact` — 证据驱动的类别迁移统计

## 借鉴 2：Idle/Compute Ratio → 小算子阻塞检测

### 来源

msprof-analyze `OpScheduleAdvice`：检测 `free_time > op_time × ratio` 的反模式，
用于识别 "太多时间花在 dispatch 而非计算上" 的瓶颈。

### 实现

对每个 stream 计算 `gap_total_ms / task_total_ms`，按阈值诊断：

| Ratio | 诊断 |
|---|---|
| > 5x | CRITICAL: host dispatch bottleneck |
| > 1x | WARNING: significant idle overhead |
| > 0.2x | MODERATE: acceptable overhead |
| ≤ 0.2x | OK |

输出到 `traceloom_stream_idle_ratio` 表。

**代表性结果**（Device 0）：

| Stream | 角色 | Task Time | Gap Time | Ratio | Diagnosis |
|---|---|---|---|---|---|
| 416 | compute_stream | 1,002ms | 33,630ms | **33.6x** | CRITICAL |
| 415 | sync_stream | 2,413ms | 48,361ms | **20.0x** | CRITICAL |
| 406 | maintenance_stream | 3,154ms | 66,548ms | **21.1x** | CRITICAL |

> 注：kickstart profile 覆盖了 engine init + ACL graph capture + HCCL setup + inference 全流程，
> init/capture 阶段 compute 极少而控制等待极多。对纯 decode 阶段做切片分析时这个指标会更有诊断价值。

## 借鉴 3：Dynamic Stream Labeling → 泛化 Stream 角色识别

### 来源

msprof-analyze `StepTraceTimeAnalysis` 的 per-rank 数据处理模式。

### 实现

将 stream 标签从 "硬编码 stream ID"（如 stream 415 = sync_stream）改为
**"基于 transition pattern 占比的动态聚类"**——对每个 stream，找出主导 gap pattern，
仅当占比 > threshold 时赋值标签。

**结果**（两 device 完全一致）：

| Dev0 Stream | Dev1 Stream | 主导 Pattern | 纯度 | Label |
|---|---|---|---|---|
| 416 | 708 | intra_kernel_launch_gap | 99.5%/99.4% | **compute_stream** |
| 406 | 702 | model_maintenance_interval | 100%/100% | **maintenance_stream** |
| 415 | 707 | event_synchronization_boundary | 99.1%/99.5% | **sync_stream** |
| 418 | 710 | intra_kernel_launch_gap | 100%/100% | **compute_stream** |
| 410 | 704 | event_record_boundary | 57%/59% | **record_stream** |

同一个 vLLM-Ascend runtime 在两 device 上产生了**完全一致的 stream 角色分配**，
验证了 pattern-based labeling 不依赖特定 stream ID。换模型/配置时标签不会因 ID 变化而失效。

## 与 RFC 的对应

| RFC Layer | 状态 | 本次变化 |
|---|---|---|
| Layer 1: Productive Timeline | ✅ | — |
| Layer 2: Stream State Timeline | ✅ | — |
| Layer 3: Idle Explanation (7 类) | ✅ | 7/7 类全部实现（之前缺 2 类） |
| **Layer 4: Host Evidence** | ✅ | **本次新增** — `traceloom_gap_host_evidence` |
| Layer 5-6: Aggregation | ⚠️ | 基础 views + idle/compute ratio 完成 |

## 文件清单

| 文件 | 说明 |
|---|---|
| `scripts/p5_host_evidence_layer.py` | Host evidence 分析脚本（~350 行） |
| `scripts/README.md` | 更新了管线文档 |
| `notes/p5-host-evidence-layer.md` | 本汇报文件 |
| `examples/.../traceloom_gap_db/derived_gaps_v2.db` | 增强后的 gap DB（含 host evidence 表） |
