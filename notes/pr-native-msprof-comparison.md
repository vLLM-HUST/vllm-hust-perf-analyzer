# PR: TraceLoom Native C++ 引擎与华为 msprof-analyze 架构对比分析

> 分析日期：2026-07-06

## 概述

本 PR 对学长开发的 TraceLoom Native C++ 引擎（`native/`，~165 个源文件）与华为 MindSpore 团队的
[msprof-analyze](https://github.com/Ascend/msprof-analyze)（~665 个 .py 文件）进行了**架构级对比分析**，
识别了 7 个可相互借鉴的技术模式。

## 两项目基本参数

| 维度 | TraceLoom Native | msprof-analyze |
|---|---|---|
| 语言 | C++17 (CMake) | Python (setuptools) |
| 规模 | 102 .cpp + 63 .h | 665 .py |
| 构建 | CMake presets, 三层静态库 | setup.py, pip install |
| 并行模型 | C++ ThreadPool, `parallel_for` | Python multiprocessing + Manager |
| 目标平台 | Ascend (msprof), Hygon, CUDA | Ascend (msprof, pytorch, mindspore) |
| License | MIT | Apache 2.0 |

---

## 一、相同模式：可以直接映射的设计

### 1.1 管道式分析架构（Pipeline Pattern）

**两项目均采用 "Adapter → Analysis → Output" 的三段式架构。**

TraceLoom：
```
SourceAdapter::load()
  → NativeIr (统一 IR)
  → run_native_pipeline()
    → build_anchors_and_tokens
    → build_protected_sequence
    → build_boundary_index
    → partition + candidate_scan (并行)
    → pattern_candidate_table + reduce
    → cost_summary_lite
  → SidecarWriter (SQLite 输出)
```

msprof-analyze：
```
DataPreprocessor::preprocess()  (适配器)
  → BaseAnalysis::run()
    → mapper_func (并行 Map)
    → reducer_func (Reduce 聚合)
    → DBManager / FileManager (输出)
  → AdvisorController::analyze_all()
```

**相似度：高。** 两项目都采用 MapReduce 风格处理多 rank 数据。
TraceLoom 的 `scan_candidate_partitions_with_diagnostics(partition_plan, thread_count)`
对应 msprof-analyze 的 `mapper_func` → `reducer_func` 并行模式。

**可借鉴：** msprof-analyze 的 `AsyncAnalysisStatus` 机制（异步分析状态追踪）
可为 TraceLoom 的长时间分析提供进度反馈。

### 1.2 SQLite Sidecar 输出格式

**两项目都生成"增强版" SQLite 数据库，保留原始表并添加分析结果。**

TraceLoom（`sidecar_writer.h` + `schema.cpp`）：
- `traceloom_event`（21 列：event_id, symbol, category, role, family, semantic_role...）
- `traceloom_anchor`（12 列）
- `traceloom_anchor_aux_slot` + `traceloom_aux_link`
- `traceloom_viz_node`（30+ 列含 compute_us, comm_us, **idle_us**）

msprof-analyze（`DBManager` + `Constant`）：
- `ClusterStepTraceTime`（step, computing, communication, **free**, bubble...）
- `ClusterCommAnalyzerBandwidth` / `ClusterCommAnalyzerTime`
- `CommunicationBottleneck` 表

**相似度：高。** TraceLoom 的 `traceloom_viz_node` 的 `idle_us` 列对应 msprof-analyze 的
`ClusterStepTraceTime.free` 列。两者的 schema 设计思想一致：**star schema + denormalized report views**。

**可借鉴：** TraceLoom 的 `semantic_role` / `semantic_role_reason` 字段提供了比 msprof-analyze
更细粒度的归因标签。msprof-analyze 已成熟的 per-step aggregation 可作为 TraceLoom
report view 的参考。

### 1.3 Idle/Free 时间计算算法

**两项目采用完全相同的 interval merge 算法计算空闲时间。**

TraceLoom（`report_tree_rows.cpp: compute_interval_cost`）：
```cpp
// 1. 收集区间内所有非 anchor event
// 2. Sort + merge overlapping → active_union_ns
// 3. idle_us = gap_us - active_union_ns  (clipped to 0)
```

msprof-analyze（`time_range_calculator.py: RangeCaculator`）：
```python
# 1. merge_continuous_intervals() → 合并重叠区间
# 2. generate_free_intervals(start, end, tasks_df) → 从目标区间减去 busy 区间
# 3. free = 目标区间 − busy_union
```

**相似度：极高。** 算法完全一致（sort → merge → subtract），仅实现语言不同。
TraceLoom 对 "wait events 是否计入 active" 有额外处理（`is_wait_event` 过滤），
msprof-analyze 在 `FreeAnalysis` 中对 PyTorch/CANN/Device 三层分别做了类似区分。

### 1.4 多平台适配器模式

**两项目都设计了可扩展的 profile 数据源适配器接口。**

TraceLoom：
```cpp
// 接口：SourceAdapter
// 实现：AscendSQLiteAdapter, HygonSQLiteAdapter,
//        AclgraphFixtureAdapter, ProtectedSequenceFixtureReader
```

msprof-analyze：
```python
# 接口：BaseAnalysis (抽象基类)
# 实现：PytorchDataPreprocessor, MindsporeDataPreprocessor,
#        MsprofDataPreprocessor
```

**相似度：高。** TraceLoom 用 C++ 虚接口 + 编译期条件编译实现，msprof-analyze 用
Python 继承 + 动态导入实现。设计意图一致：**一次分析管线，多种数据源**。

---

## 二、不同之处：TraceLoom 独特的设计选择

### 2.1 Grammar Engine（Sequitur 风格语法归纳）

**TraceLoom 独有的核心创新。** msprof-analyze 没有对应机制。

TraceLoom 实现了基于状态机的语法归纳引擎（`grammar_engine.h`）：
- 多轮迭代（max_rounds = 10000）
- 多生产者：`kAdjacentRun`（相邻重复）、`kPairDiscovery`（模式对）
- 自动发现嵌套循环结构：`Repeat x36 → Repeat x24 → MatMul/Rope/AllReduce`
- 压缩评价指标：`gain = k * (len - 1) - (len + 1)`

msprof-analyze 的 loop/stage 检测依赖于 profiler metadata 中的 step 标记，
不进行自动的序列模式挖掘。StepTraceTimeAnalysis 读取预存 step 信息，
不会从零发现结构。

**价值：** Grammar engine 是 TraceLoom 对 msprof-analyze 的**最大差异化优势**——
不需要 profiler 预先标注 step 信息就能从任意 trace 中恢复执行结构。

### 2.2 Protected Interval 机制

TraceLoom 独有的边界保护概念（`protected_interval_table.h`）：
- `ProtectedIntervalKind`: `kGraphReplayUnit`, `kUserWindow`
- `BoundaryPolicy`: `kNoCross`, `kAllowEnclosing`, `kBlockAnyOverlap`
- 用于确保 ACLGraph/CUDA Graph replay 的原子区间不被 pattern scan 切分

msprof-analyze 对 graph capture/replay 的处理分散在多处
（`ascend_aclgraph.py` 的 `_segment_tasks`、CUDA 相关单独逻辑），
没有统一的 "不可分割区间" 抽象。

**价值：** Protected Interval 是一个**更干净的抽象**——
将 graph 边界从 pattern mining 中解耦，让两者独立演化。

### 2.3 Unified IR（平台无关的中间表示）

TraceLoom 的 `NativeIr` 包含 14 张 IR 表：
```
trace_events, tasks, communication_ops, anchors, tokens,
capture_slots, graph_templates, replay_units, stream_info,
source_refs, symbols (via StringTable), protected_intervals
```

msprof-analyze 的数据流是 **profiler table → pandas DataFrame → 分析 → CSV/DB**，
中间没有一个显式的、有 schema 的 IR 层。

**价值：** Unified IR 让 TraceLoom 的分析管线与平台适配器解耦——
新的 profile 格式只需实现新的 Adapter，分析逻辑完全不变。这是 TraceLoom
实现 "Ascend / CUDA / Hygon 统一分析" 的基础。

---

## 三、msprof-analyze 中可以借鉴的内容

### 3.1 FreeAnalysis 的三层证据链

msprof-analyze 的 `FreeAnalysis` 将空闲归因到三层：
```
Device Task → CANN API (connectionId) → PyTorch API
```

TraceLoom 目前只有两层（Device Task → CANN API）。
msprof-analyze 的 **"Idle PyTorch layer vs Abnormal CANN layer" 二分法**
（通过 `next_wait - prev_wait` 的阈值比较判断瓶颈在 PyTorch 前端还是 CANN 下发）
可以直接借鉴到 TraceLoom 的 Host Evidence 层。

**建议：** 在 `traceloom_gap_event` 中增加 `host_boundary` 和 `dispatch_boundary` 两个子类别，
参考 msprof-analyze 的 `DIFF_WAIT_THRESHOLD_NS = 50us` 阈值设计。

### 3.2 Step 级别的 Per-Iteration 聚合

msprof-analyze 的 `StepTraceTimeAnalysis` 对每个 step 计算：
```
computing, communication, overlapped, free, bubble, stage
```

产出 `cluster_step_trace_time.csv` / `ClusterStepTraceTime` 表。

TraceLoom 目前仅在 tree node 级别汇总 gap，
没有 per-iteration（per occurrence）的 gap 明细。
Tree node 的 multiple occurrences 被平均化，丢失了 iteration 间的方差信息。

**建议：** 在 `traceloom_gap_event` 的 view 中添加 per-occurrence 统计，
支持检测 "某些 iteration 的 gap 异常大"。

### 3.3 OpScheduleAdvice 的小算子阻塞检测

msprof-analyze 的 `op_schedule_advice.py`：
```python
# 检测 "free time > operator time" 的反模式
if op_free > op_dur * SMALL_OP_DUR_RATIO:
    small_op_num += 1
```

这是一个简洁的诊断模式：
"某个 token 的 idle 时间比其计算时间多很多 → host dispatch 瓶颈"。

**建议：** 在 TraceLoom 的 report view 中加入 `idle_to_compute_ratio` 指标，
对 `ratio > N` 的 tree node 标记 warning。

### 3.4 异步分析状态追踪

msprof-analyze 的 `AsyncAnalysisStatus` 提供了分析进度查询机制——
在大 profile 分析耗时较长时非常有用。

**建议：** TraceLoom 的长时间运行（大 trace 的 grammar engine 可能跑数千轮）
可参考此模式，通过 `--progress` flag 输出中间轮次状态。

---

## 四、TraceLoom 对 msprof-analyze 的优势

| 维度 | TraceLoom 优势 | 说明 |
|---|---|---|
| **Pattern Mining** | Grammar Engine 自动发现结构 | 不依赖 profiler metadata 的 step 标注 |
| **IR 抽象** | Unified NativeIr | 分析管线与平台解耦 |
| **Protected Interval** | 统一的图边界抽象 | ACLGraph/CUDA Graph 用同一概念处理 |
| **语义标注** | semantic_role + semantic_role_reason | 提供 op 级别的可解释性 |
| **性能** | C++ 原生 + 多线程 | 大 profile 上显著快于 Python |
| **Gap 分类** | 7 类 + 4 级置信度 | vs msprof-analyze 的 3 类无置信度 |

---

## 五、对后续开发的建议

### 5.1 可立即借鉴（低实现成本）
1. FreeAnalysis 的 **三层证据链**（加 PyTorch 层追踪）
2. OpScheduleAdvice 的 **小算子阻塞检测**（idle/compute ratio）
3. Per-step 的 **iteration 级 gap 统计**

### 5.2 中期可考虑
4. `AsyncAnalysisStatus` 风格的**进度反馈**
5. msprof-analyze 的 `ConfigurableThreshold` 模式——将 `gap_us`、`min_length` 等做成可从配置加载

### 5.3 TraceLoom 应保持的差异化优势
- **Grammar Engine** 不妥协——这是 TraceLoom 最独特的价值
- **Protected Interval** 的干净抽象——不要为了兼容牺牲
- **confidence 体系**——msprof-analyze 没有，保持

---

## 六、总结

TraceLoom Native C++ 引擎在架构设计上与华为 msprof-analyze 存在**高度结构相似性**，
表明两者在 "profile 分析管线" 这个问题的解法上存在共识。但 TraceLoom 的 Grammar Engine、
Protected Interval、Unified IR 和 confidence 体系构成了
**对 msprof-analyze 的显著差异化优势**。

msprof-analyze 更成熟的部分（三层证据链、per-step 聚合、小算子检测）
可以作为 TraceLoom 下一步功能完善的参考。

---

*分析覆盖文件：TraceLoom `native/` 全部 165 个源文件 + msprof-analyze `msprof_analyze/` 核心模块*
