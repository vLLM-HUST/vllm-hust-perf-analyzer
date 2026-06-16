# TraceLoom 论文重构设计草案

## 总判断

当前 `IEEE_Conference_Template/traceloom_profile_database.tex` 的问题不是
内容错，而是论文对象放错了。它现在把 TraceLoom 写成：

```text
一个 queryable profile database / augmented SQLite schema
```

这会让文章天然落到工具说明、schema 说明、case-study demo 的层级。更强的写法应该是：

```text
一篇关于现代 LLM serving framework 内在结构的 profiling study。
TraceLoom 是让这种 study 可落地、可复查、可归因的结构化 trace 分析算法。
```

换句话说，文章主语应该从 `TraceLoom` 改成：

```text
Modern LLM serving frameworks
```

TraceLoom 的位置应该从“论文标题里的主角”降一层，变成方法贡献：

```text
structured trace attribution layer
```

这会更接近 `2017_profiling_final.pdf` 的研究姿势：不是比较 Storm/Flink 谁快，而是抽象 DSP 系统的共同设计，再用 profiling 说明这些设计在新硬件条件下暴露出什么问题，并用小优化验证诊断。

## 建议标题

首选：

```text
Revisiting the Design of LLM Serving Frameworks through Structured Trace Attribution
```

备选：

```text
Where Does LLM Serving Time Go? A Design-Level Profiling Study of Modern Inference Frameworks
```

如果希望更像 2017 paper：

```text
Revisiting the Design of LLM Serving Frameworks on Modern Accelerators
```

我建议用第一个。它同时保留两件事：

1. `Revisiting the Design` 对齐参考论文的 design-revisiting 姿势。
2. `Structured Trace Attribution` 给 TraceLoom 留出清晰的方法贡献位置。

## 中心论点

可以把整篇文章钉在这句话上：

```text
Modern LLM serving frameworks are no longer simple inference wrappers. They are online, stateful runtime systems whose performance is governed by the interaction among KV-cache state, token-level scheduling, accelerator execution, and application-level program structure.
```

中文理解：

```text
现代 LLM serving framework 不是模型 forward 的封装器，而是在线有状态运行时系统。它的性能由 KV 状态、token 调度、硬件执行和应用语义结构共同决定。
```

TraceLoom 对应的方法论论点是：

```text
End-to-end metrics and flat kernel tables are insufficient for this class of systems. A useful profiler must recover repeated execution structure, attribute auxiliary costs to stable semantic anchors, and connect runtime changes to source-level optimization hypotheses.
```

## 对当前 tex 的核心批评

当前版本的贡献是：

```text
C1. augmented profile database model
C2. relational representation of anchor-based loop analysis
C3. preliminary Ascend/CANN implementation and case study
```

这三个贡献都偏“TraceLoom 自身的数据结构”。问题是：

1. 论文主问题太窄：读者会觉得这是一个 SQLite-backed profiler extension，而不是 LLM serving 系统论文。
2. case study 太像 demo：`5000 normalized events / 771 anchors / 53 nodes` 证明了工具能跑，但没有说明 LLM serving framework 的 common design 在哪里失效。
3. 缺少 benchmark philosophy：参考论文的 benchmark 是机制探针，不是随手跑几个 workload。当前版本没有把 workload 和 design principle 绑定起来。
4. 缺少 finding 组织：参考论文 Section V 是 `Study the Impact of Common Designs`，每个 subsection 都有 Finding。当前版本没有这样的系统论文骨架。
5. 缺少验证性扰动：参考论文最后有 non-blocking tuple batching 和 NUMA-aware placement。我们这里也需要至少一个小而明确的 perturbation / optimization / negative case。

## 新贡献设计

建议把 contributions 改成如下五条：

```text
C1. We identify four common design principles of modern LLM serving frameworks:
    KV state management, online token scheduling, hardware-aware execution,
    and program-aware serving.

C2. We design a mechanism-driven profiling methodology that stresses these
    common designs with targeted workloads rather than ranking frameworks by
    aggregate throughput alone.

C3. We propose structured trace attribution, implemented in TraceLoom, which
    compresses raw accelerator timelines into semantic anchors, repeated
    execution structures, auxiliary-cost windows, and queryable evidence tables.

C4. We apply this method to representative Ascend/vLLM-Ascend serving traces
    and show how repeated decode/layer structures, communication neighborhoods,
    and auxiliary costs can be recovered from low-level profiler output.

C5. We use perturbation-based case studies, including communication/runtime
    configuration and Decode All-to-All Buffer Reuse, to test whether apparent
    optimization hypotheses actually change stable runtime structures beyond
    run-to-run variance.
```

如果实验规模暂时不够跨 vLLM/SGLang/TensorRT-LLM，就不要在 C4 里承诺“representative frameworks”。可以写成：

```text
We instantiate the study on vLLM-Ascend and discuss how the methodology extends to other serving engines.
```

这样诚实，也更稳。

## 文章结构

建议用下面这个骨架替换当前 tex 的结构。

### 1. Introduction

目标：

1. 说明 LLM serving 已经从 batch inference 变成在线有状态 runtime。
2. 抽象出现有 framework 的共同设计。
3. 说明硬件和 workload 变化：长上下文、RAG、agent、结构化输出、多卡通信、异构加速器。
4. 指出现有 profiling 视角不足：端到端指标太宏观，kernel top-k 太扁平，timeline 太碎。
5. 引出 TraceLoom：不是新 runtime，而是 design-level profiling 的结构化归因层。

Introduction 的逻辑应该像参考论文摘要：

```text
systems share key designs
    -> hardware/workload changed
    -> revisit these designs through profiling
    -> raw profilers are insufficient
    -> structured trace attribution
    -> findings / perturbation validation
```

### 2. Background: LLM Serving as an Online Stateful Runtime

这一节不要先讲 TraceLoom schema，而要先讲 serving framework 的内在结构：

1. Autoregressive lifecycle: prefill and decode.
2. KV cache as persistent per-request state.
3. Continuous batching and iteration-level scheduling.
4. Hardware lowering: ragged token streams to accelerator kernels.
5. Program-aware runtime: prefix reuse, grammar/FSM, RAG/agent DAG, structured output.

这里可以放第一张核心图：

```text
Application / Program layer
  RAG, agent, structured output, shared prefix

Serving runtime layer
  scheduler, continuous batching, prefix cache, grammar runtime

State layer
  KV blocks, radix tree, eviction, migration

Execution layer
  prefill kernels, decode kernels, sampling, collectives

Hardware layer
  NPU/GPU cores, HBM, L2, interconnect
```

### 3. Common Designs and Profiling Questions

这一节对应参考论文的 `Preliminaries and Background`，但更锋利：直接把四个 common designs 写成本文研究对象。

```text
D1. KV State / Memory Design
    Question: Does KV management remove memory waste or introduce hidden block, reuse, and eviction costs?

D2. Online Token Scheduling Design
    Question: Does continuous batching improve throughput at the cost of TTFT, ITL, and tail latency?

D3. Hardware Execution Design
    Question: Where does serving time move among compute, memory bandwidth, communication, synchronization, and runtime overhead?

D4. Program-aware Serving Design
    Question: When do prefix reuse, structured output, and program-level semantics help, and when do they shift cost to CPU/runtime paths?
```

这一节应该输出一张 `Design-to-bottleneck matrix`：

```text
Design              Benefit              Hidden cost                 Profiling signal
Paged KV            less fragmentation   lookup / churn / pressure   KV block churn, HBM BW
Continuous batching throughput           queueing / tail latency     ITL variance, occupancy
Chunked prefill     protects decode      TTFT / prefill ineff.       prefill wait, decode stalls
Prefix cache        avoids recompute     lookup / eviction overhead  hit rate, saved tokens
Structured output   valid output         grammar / mask overhead     sampling/grammar time
Multi-GPU serving   capacity             collectives / imbalance     HCCL/NCCL time, skew
```

### 4. Methodology

这一节要对齐参考论文的 `Methodology`，而不是当前 tex 的 `Augmented Profile Database`。

#### 4.1 Mechanism-driven workloads

不要只用 ShareGPT。workload 要像参考论文的七个 stream applications 一样，是机制探针：

```text
Short chat
  scheduler overhead, ITL, baseline online serving

Long-context RAG
  prefill compute, KV allocation, chunked prefill

Long output generation
  decode bandwidth, KV read path, per-token latency

Shared-prefix multi-query
  prefix cache / radix cache effectiveness

Multi-turn conversation
  KV lifecycle, cache reuse, eviction

Structured JSON output
  grammar/FSM/logits masking overhead

Mixed online workload
  queueing, preemption, fairness, tail latency

Multi-GPU serving
  TP/PP/EP communication, KV placement, rank imbalance
```

如果短期只能跑 vLLM-Ascend，就把这些写成 methodology target，实验证据集中在其中 2-3 个最扎实的 workload。

#### 4.2 Profiling stack

分三层：

```text
Macro metrics
  TTFT, ITL/TPOT, throughput, goodput under SLO, P95/P99 latency

Runtime decomposition
  queueing, scheduling, prefill, decode, sampling, KV management, communication

Structured trace attribution
  semantic anchors, auxiliary windows, repeat tree, node cost table,
  communication neighborhoods, source-path evidence
```

TraceLoom 放在第三层。

#### 4.3 Perturbation design

每个 profiling paper 都需要“验证诊断”的小实验。可以设计：

```text
P1. Communication/runtime perturbation
    HCCL AIV on/off or communication backend configuration.

P2. Source-level perturbation
    Decode All-to-All Buffer Reuse patch.

P3. Scheduler perturbation
    chunked prefill on/off, max batched tokens, decode-first policy.

P4. State perturbation
    prefix cache on/off, block size, KV memory utilization.
```

短期最现实的是 P1 + P2，因为仓库里已经有 reproduce 路径。

### 5. Overall Performance

这一节只给宏观症状，不要做排行榜。

写法：

```text
We first report aggregate serving behavior to establish symptoms, but we do not treat absolute framework rankings as the main result.
```

图表：

1. TTFT / ITL / throughput under selected workloads.
2. run-to-run variance。
3. prefill-heavy vs decode-heavy 的宏观差异。

这节要为后面的 structured trace attribution 铺垫：

```text
Macro metrics show something changed, but cannot say whether the intended runtime path changed.
```

### 6. Study the Impact of Common Designs

这是全篇主菜，对齐参考论文 Section V。

建议每个小节都用 `Finding` 开头。

#### 6.1 KV State Management

可能 finding：

```text
Finding 1: KV-related optimizations are workload-sensitive; their benefits appear only when the trace shows stable reductions in repeated prefill or decode state paths, not merely when aggregate throughput moves.
```

证据需求：

1. prefix/shared workload 或 long-context workload。
2. KV allocation / block churn / cache hit signals。
3. TraceLoom node-level cost变化。

#### 6.2 Online Token Scheduling

可能 finding：

```text
Finding 2: Continuous batching converts request-level concurrency into token-level iteration structure, but mixed prefill/decode workloads expose scheduler-induced waiting and ITL variance.
```

证据需求：

1. short chat vs mixed workload。
2. decode loop repeat structure。
3. queue / prefill / decode阶段的变化。

#### 6.3 Hardware Execution and Communication

这是 TraceLoom 目前最强的地方。

可能 finding：

```text
Finding 3: Distributed decode exposes highly repetitive layer-level structures, where communication anchors such as AllReduce/All-to-All sit inside stable repeated neighborhoods rather than isolated top-k events.
```

证据：

1. Pattern Compression Tree: outer decode repeat + nested layer repeat。
2. HCCL communication share。
3. node cost table / tree-map。

#### 6.4 Auxiliary Runtime Cost

这里是 TraceLoom 的独特贡献。

可能 finding：

```text
Finding 4: Auxiliary events should not be discarded as noise; removing them from the main anchor sequence while retaining anchor-local prelude cost exposes hidden runtime and synchronization overhead.
```

证据：

1. auxiliary window aggregation。
2. largest prelude slots。
3. 对比 top-k 看不到的成本。

#### 6.5 Program-aware Serving

如果暂时没有足够实验，可以作为 discussion-backed finding 或 future-facing analysis，不要硬写成已验证结论。

可能写法：

```text
Finding 5 candidate: Program-aware mechanisms such as prefix reuse and structured decoding change the unit of scheduling from token streams to language-model programs, requiring profiling interfaces that expose prefix, branch, and constraint structure.
```

这里要谨慎：没有实验就写成 implication，不写成 hard finding。

### 7. TraceLoom: Structured Trace Attribution

这一节才讲当前 tex 里的核心技术，但要服务上面的研究问题。

推荐结构：

```text
7.1 Semantic anchors
    compute / collective / synchronization / data movement anchors

7.2 Anchor-auxiliary attribution
    auxiliary events are attached to following anchors, then aggregated by node coverage

7.3 Pattern Compression Tree
    repeated anchor sequence -> repeat nodes -> loop/tree view

7.4 Queryable evidence database
    SQL is the evidence interface, not the paper's main thesis

7.5 Trace-to-source hypothesis loop
    macro change -> stable node -> raw events -> source path -> perturbation judgment
```

当前 tex 的 database schema 表可以保留，但不要在主线中过早出现。它最好放在方法节靠后，或者作为一个 compact table。

### 8. Perturbation Case Studies

这一节对应参考论文 Section VI `Towards More Efficient DSP Systems`。

但我们的重点不一定是“优化成功”，也可以是“优化假设被证伪”。

建议写两个 case：

#### Case A: HCCL / AIV perturbation

问题：

```text
Communication runtime choices是否改变 repeated communication neighborhood?
```

展示：

1. macro throughput / generation time。
2. communication anchor neighborhood。
3. repeated node成本变化。

#### Case B: Decode All-to-All Buffer Reuse

问题：

```text
看似合理的 buffer reuse 是否稳定降低通信前置辅助开销？
```

推荐结论方向：

```text
The patch is source-level plausible, but the measured macro deltas are within run-to-run variance and the targeted communication neighborhoods do not show a stable cost reduction. This negative result demonstrates why trace-level optimization gates are needed.
```

这个 negative case 很有价值。它能让文章不只是“我们发明了一个工具”，而是“我们有能力判断一个优化假设有没有真正改变运行路径”。

### 9. Implications

讨论未来 serving framework 应该暴露什么 profile surface：

1. explicit prefill/decode markers。
2. scheduler iteration IDs。
3. KV block lifecycle events。
4. prefix-cache hit/miss and saved-token counters。
5. grammar / structured decoding cost markers。
6. communication group / collective neighborhood IDs。
7. trace-level CI gates。

可以把结论写成：

```text
LLM serving systems need performance observability at the same abstraction level as their runtime designs.
```

### 10. Related Work

分类不要只写 profiler 工具：

1. LLM serving systems: vLLM, SGLang, TensorRT-LLM, Orca, Sarathi, TGI, LMDeploy。
2. LLM serving benchmark / workload studies。
3. Accelerator profilers: msprof, Nsight Systems, CUPTI, Perfetto。
4. Trace compression / performance diagnosis / database-backed profiling。
5. Stream/system profiling studies, including 2017 DSP paper as conceptual precedent。

### 11. Conclusion

结论不要说：

```text
TraceLoom is an augmented profile database.
```

要说：

```text
Modern LLM serving frameworks should be profiled as online stateful runtime systems. By combining design-level workloads with structured trace attribution, we can move beyond throughput rankings and ask whether KV, scheduling, communication, and program-level optimizations actually change the intended execution structures.
```

## 三张核心图

### Figure 1: Common design stack

用于 Introduction / Background。

```text
Program structure
  RAG / agent / structured output / shared prefix

Runtime scheduling
  continuous batching / chunked prefill / priority / preemption

State management
  KV blocks / radix cache / eviction / migration

Hardware execution
  attention / GEMM / sampling / HCCL/NCCL / graph replay

Accelerator platform
  NPU/GPU cores / HBM / L2 / PCIe/NVLink/HCCS
```

### Figure 2: TraceLoom attribution pipeline

用于 Methodology / TraceLoom section。

```text
raw profiler tables
  -> normalized events
  -> semantic anchors
  -> auxiliary/prelude attribution
  -> repeated pattern discovery
  -> Pattern Compression Tree
  -> node cost table / SQL evidence / source-path review
```

### Figure 3: Design-to-evidence matrix

用于 Common Designs 或 Discussion。

```text
Design -> workload probe -> macro signal -> trace signal -> possible perturbation
```

这张图会让文章显得是一个完整 methodology，而不是几个孤立实验。

## 摘要草稿

```text
Modern LLM serving frameworks have evolved from simple inference wrappers into
online stateful runtime systems. Despite implementation differences, systems
such as vLLM and SGLang share several core designs: KV-cache state management,
iteration-level token scheduling, hardware-aware execution, and program-aware
reuse or constraint handling. However, emerging workloads such as long-context
RAG, structured generation, agentic programs, and multi-accelerator serving
stress these designs in ways that are difficult to explain with end-to-end
metrics or flat kernel tables alone.

This paper revisits the design of LLM serving frameworks through structured
trace attribution. We first formulate a set of mechanism-driven profiling
questions for common serving designs. We then present TraceLoom, an offline
trace analysis layer that reconstructs semantic execution skeletons from raw
accelerator profiler output, compresses repeated decode and layer structures,
attributes auxiliary runtime costs to stable anchors, and exposes the result
through queryable evidence tables. Using Ascend/CANN traces from vLLM-Ascend,
we show that low-level profiler rows can be reduced to compact repeated
execution structures that reveal communication neighborhoods, auxiliary
prelude costs, and cross-run optimization signals.

Our perturbation case studies show why serving optimization should be validated
at the trace-structure level: source-level plausible changes do not necessarily
produce stable reductions in the targeted runtime path. These results suggest
that future LLM serving frameworks need profiling interfaces aligned with their
own runtime abstractions: KV lifecycle, scheduler iterations, program
structure, and communication neighborhoods.
```

## 下一步写作路线

建议不要直接在当前 tex 上小修小补。更好的路线：

1. 新建一版 `traceloom_serving_design_study.tex` 或彻底替换当前 tex 的章节骨架。
2. 先写 Introduction + Background + Common Designs，把主语从 TraceLoom 改成 LLM serving framework。
3. 把当前 `Augmented Profile Database` 压缩成 `Structured Trace Attribution` 的一个 subsection。
4. 从已有 kickstart 和 decode-a2a reproduce 材料里抽两组最可靠的数据，支撑 Section 6/8。
5. 暂时不要承诺跨所有框架，除非后续确实补 vLLM/SGLang/TensorRT-LLM 的实验。

一句话：

```text
这篇文章应该是 design-level profiling study；TraceLoom 是它的证据引擎，不是唯一主角。
```
