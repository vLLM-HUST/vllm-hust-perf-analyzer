# P0 深入分析：Device Gap 归类结果详解

> 2026-06-24
> 基于 Derived Gap DB 的交叉查询分析

## 一、全景：65 秒 Gap 的构成

| 类别 | Device 0 | Device 1 | 可操作性 |
|---|---|---|---|
| `runtime_control_present` | 33.3s (959 gaps) | 32.8s (935 gaps) | ⚠️ 可能是 profiler artifact |
| `no_observed_device_work` | 16.4s (5,147 gaps) | 16.9s (4,915 gaps) | ❌ 完全未知 |
| `phase_change_boundary` | 15.0s (5 gaps) | 15.0s (5 gaps) | — 阶段边界，正常 |
| `blocked_by_visible_wait` | 1.0s (1,332 gaps) | 0.9s (1,400 gaps) | ✅ 可确认 |
| `capture_control_present` | <1ms (1 gap) | <1ms (3 gaps) | — 可忽略 |

> 能确认的等待只占总 gap 时间的 1.5%。超过一半（50.7%）集中在 `runtime_control_present`，
> 另有四分之一（25.0%）完全无法解释。这意味着归因工作的核心挑战不在"解释等待"，
> 而在"区分 profiler artifact vs 真实空闲"。

## 二、核心发现：MODEL_MAINTAINCE 是 0 耗时的标记事件

### 现象

查询最大的 `runtime_control_present` gap（D0_GAP890，725.2ms）：

```
gap 区间内只有 3 个 MODEL_MAINTAINCE task
但这 3 个 task 的 duration = 0us（startNs = endNs）
```

### 含义

**`MODEL_MAINTAINCE` 不是真正的运行时任务，而是 profiler 在 timeline 上插入的"阶段标记点"。**

它的 startNs = endNs = 0 duration，仅表示"此时刻 profiler 进入/维持在模型维护阶段"。
33s 的 `runtime_control_present` 时间不是 33s 的计算或等待——而是 profiler 用数千个标记点
撑开了大段时间窗口，这些窗口的边界由标记点定义，窗口内部的实际活动未知。

### 验证

同设备内所有大的 `runtime_control_present` gap 都落在同一个 stream 上：

| Device | Stream | Stream 角色 |
|---|---|---|
| Device 0 | stream 406 | 模型维护流（之前已发现的 "model_maintenance_boundary"） |
| Device 1 | stream 702 | 模型维护流 |

这与之前 per-stream 分析完全吻合：stream 406/702 几乎每一对相邻 task 都是
`MODEL_MAINTAINCE → MODEL_MAINTAINCE`，2,635 个 gap 中每个都 >1ms。

> **结论**：学长的判断"MODEL_MAINTAINCE 可能和 ACL Graph 执行模式有关，避免把 profiler artifact 当 runtime 行为"
> 在此得到了数据层面的强印证。下一步需要查 Ascend/CANN 开源代码确认 `MODEL_MAINTAINCE` 的 profiler 插桩点。

## 三、no_observed_device_work：设备空闲时 host 在做什么？

### 验证：是否真的"无任何可见任务"？

逐个查询最大的 5 个 "无任务" gap：

| Gap | 时长 | TASK 数 | CANN_API 数 | Host 端在做什么 |
|---|---|---|---|---|
| D0_GAP6879 | 989.8ms | **0** | 28 | `aclrtMemcpyAsync`(×5) + `HostMalloc`(×2) + `aclrtPointerGetAttributes`(×5) |
| D0_GAP181 | 927.7ms | **0** | 53 | `aclmdlRICaptureGetInfo`(×7) + `aclrtDestroyEvent`(×4) + `aclrtQueryEventStatus`(×4) |
| D0_GAP93 | 874.7ms | **0** | 21 | `aclrtGetResInCurrentThread`(×4) + `CtxGetSysParamOpt`(×2) + `aclnnAddRmsNorm`(×1) |
| D0_GAP24 | 818.1ms | **0** | 56 | `CtxGetSysParamOpt`(×18) + `DevMalloc`(×2) + `aclnnPowScalarTensorGetWorkspaceSize`(×1) |
| D0_GAP209 | 622.0ms | **0** | 84 | `StreamSyncTaskFinish`(×35) + `DevFree`(×8) + `aclrtFree`(×8) + `DestroyEvent`(×4) |

### 模式识别

这些 gap 中 host 的活动可以归为三类：

| Host 活动类型 | 典型 API | 频率 | 含义 |
|---|---|---|---|
| **内存管理** | `DevMalloc`, `DevFree`, `HostMalloc`, `aclrtFree`, `aclrtFreeHost` | 高 | Host 在清理/分配设备内存 |
| **Event/Stream 生命周期** | `aclrtDestroyEvent`, `EventDestroy`, `StreamSyncTaskFinish` | 高 | 回收 event 和 stream 资源 |
| **Context/属性查询** | `aclrtGetResInCurrentThread`, `CtxGetSysParamOpt`, `aclrtQueryEventStatus` | 高 | Host 在做状态查询 |

> **结论**："设备无任务"不意味着"系统空闲"。这些 ~1s 的 gap 大概率是 vLLM/Ascend 引擎在
> iteration 之间做内存回收和 event 清理。设备在这段时间里确实没有被提交新任务，但 host 线程
> 是在忙的——只是忙的不是"往设备提交 kernel"，而是"清理上一个 iteration 的资源"。

### 待解决

目前 `no_observed_device_work` 占 16s（25%），归因 confidence = unknown。
需要实现 **host evidence 层**（RFC Layer 4）来区分：
- "host 在做内存管理" → 可以标为 `host_memory_management`
- "host 在做同步等待" → 可以升级为 `host_sync_api_present`
- "host 也没有任何活动" → 保持 `no_observed_device_work`

## 四、blocked_by_visible_wait：跨 Stream 同步的精确画像

### Stream 分工

| Device | 等待流 | 出现次数 | 信号流 | 模式 |
|---|---|---|---|---|
| Dev0 | **stream 415** | 1,002 | stream 410 (EVENT_RECORD) | 415 等 410 的 event |
| Dev0 | **stream 416** | 403 | stream 410 (EVENT_RECORD) | 416 等 410 的 event |
| Dev0 | stream 410 | 57 | stream 108 (capture/record) | 410 等 108 的 event |
| Dev1 | **stream 707** | 1,015 | stream 704 (EVENT_RECORD) | 707 等 704 的 event |
| Dev1 | **stream 708** | 414 | stream 704 (EVENT_RECORD) | 708 等 704 的 event |
| Dev1 | stream 704 | 77 | stream 188 (capture/record) | 704 等 188 的 event |

### 典型场景

每个 gap 的 exemplar：

```
D0_GAP257 (681ms, 最大的 wait gap):
  stream 416: EVENT_WAIT          ← 在等
  stream 410: EVENT_RECORD        ← 在发信号
  host: aclrtStreamWaitEvent × 1  ← 主机侧记录了这个等待操作

D0_GAP6935 (15ms):
  stream 410: EVENT_WAIT          ← 在等
  stream 1944: CAPTURE/RECORD     ← capture 阶段的信号
```

### 大小分布

`blocked_by_visible_wait` 绝大多数是微小等待：

| 大小 | 数量 | 说明 |
|---|---|---|
| <1ms | 1,328 | 配置的 min gap 阈值以上 |
| 1-10ms | 2 | |
| 10-100ms | 1 | |
| 100ms-1s | 1 | 唯一的大等待：681ms（可能是 capture 阶段的特殊等待） |

> **结论**：跨 stream event 同步等待很频繁（~1,400 次），但每次都很小（<1ms）。
> 总耗时只有 1s，不是性能瓶颈。Stream 415/707 是"专门的同步流"——

## 五、phase_change_boundary：五个巨型 Gap 的上下文

每个 device 有 5 个 >1s 的 gap，分两类：

### 类型 A：Kernel 间的大间隔（~1.7s, ~2.1s, ~3.6s）

```
gap 前后都是同一个 stream 的 KERNEL_AIVEC
gap 之后第一个 task 也是 KERNEL_AIVEC，间隔只有 0us
```

→ 这些是 kernel 密集发射之间的自然间歇，gap 后立即恢复计算。可能是 host 在准备下一批 kernel 参数。

### 类型 B：Stage 切换（~5.3s, ~2.2s）

```
D0_GAP89 (5,324ms):
  前: KERNEL_AIVEC × 5（密集）
  后: MEMCPY_ASYNC → STARS_COMMON → KERNEL_AIVEC
  → 从纯计算阶段切换到 数据搬运+计算 阶段

D0_GAP150 (2,216ms):
  前: KERNEL_AIVEC × 5
  后: EVENT_RECORD → EVENT_WAIT → KERNEL_AIVEC
  → 跨 stage 的同步边界
```

> **结论**：阶段边界 gap 是两个 profile 阶段之间的自然断裂（如 ACL graph capture 开始前/结束后）。
> 其 duration 受 profiler 采样点的影响，不是 runtime 性能问题。归因时应直接过滤。

## 六、汇总判断

```
Device Gap 总时间: 65.6s
│
├── 33.3s (50.7%) → MODEL_MAINTAINCE profiler 标记点
│   └── 0us duration 的标记事件，撑开的时间窗口
│   └── 不是真正的 runtime 开销；需源码验证
│
├── 16.4s (25.0%) → 设备无任务，host 在做内存管理
│   └── CANN_API 有活动（DevMalloc/Free, DestroyEvent）
│   └── 需要 host evidence 层进一步拆解
│
├── 15.0s (22.8%) → 阶段边界
│   └── kernel 密集发射间歇 + capture/replay 阶段切换
│   └── 正常，应过滤
│
└── 1.0s (1.5%) → 跨 stream event 同步等待
    └── confirmed 证据，aclrtStreamWaitEvent 可关联
    └── 不是性能瓶颈（每次 <1ms）
```

## 七、下一步建议

1. **给 `no_observed_device_work` 加 host evidence**：
   用 CANN_API 的时间重叠检测区分 "host 内存管理" 和 "host 同步等待"，
   对应 RFC 的 `host_sync_api_present` 和 `host_memory_management`（新增类别）

2. **源码验证 MODEL_MAINTAINCE**：
   在 Ascend/CANN 开源代码中搜索 `MODEL_MAINTAINCE` 的 taskType 定义和插桩位置，
   确认其是 profiler 采集点而非 runtime 任务

3. **改进 productive timeline**：
   将 `MEMCPY_ASYNC` 是否计入 productive 做成可配置选项（当前计入了），
   因为它在 phase_change_boundary 中作为"gap 后第一件事"出现，可能是阶段边界的恢复标志

4. **stream 标签的泛化**：
   当前 stream 415=同步流、406=维护流的结论仅基于 kickstart profile。
   需在更多 profile（不同模型、不同配置）上验证 pattern 是否一致

---

*分析代码：`analyze_device_gaps.py`（查询 Derived Gap DB + 原始 msprof DB）*
