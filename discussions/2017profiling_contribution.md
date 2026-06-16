可以，而且这篇文章的构思对我们写 **LLM serving framework profiling** 很有启发。它最值得借鉴的地方不是具体的 Storm/Flink 结论，而是它的**研究姿势**：

> 不做“系统 A 比系统 B 快多少”的跑分文章，而是抽象出现有系统共享的核心设计，再用 profiling 证明这些设计在新硬件/新 workload 下哪里失效，最后给出一两个小而有力的优化来证明诊断是对的。

这正好适合我们写 vLLM、SGLang、TensorRT-LLM、TGI、LMDeploy 这类 LLM serving framework。

---

# 1. 这篇文章的核心构思是什么？

这篇文章的主线非常清楚：

```text
旧系统设计假设
        ↓
硬件环境变化
        ↓
选择代表性系统
        ↓
抽象共同设计
        ↓
设计 benchmark
        ↓
做硬件级 profiling
        ↓
发现瓶颈
        ↓
提出验证性优化
        ↓
归纳未来系统设计方向
```

它开篇先说，现代 DSP 系统虽然架构不同，但大多共享三类设计：**pipelined processing with message passing、on-demand data parallelism、JVM based implementation**。然后指出硬件已经变化：更多 core、更复杂 cache hierarchy、NUMA 效应更明显，所以需要重新审视这些设计。文章不是只比较 Storm 和 Flink 谁快，而是把 Storm/Flink 当成代表，研究这些共同设计如何与现代多核多 socket 处理器相互作用。

这对我们非常关键。我们也不应该写成：

```text
vLLM vs SGLang vs TensorRT-LLM 谁更快？
```

而应该写成：

```text
现有 LLM serving frameworks 共享了哪些核心设计？
这些设计在现代 GPU/NPU、长上下文、RAG、agent、多租户、结构化输出 workload 下是否仍然成立？
它们真正的瓶颈在哪里？
```

也就是说，文章的野心不是 benchmark leaderboard，而是 **design revisiting**。

---

# 2. 最值得借鉴的写法：先抽象 common designs

这篇 DSP 文章最漂亮的一刀，是先把一堆系统压缩成三个共同设计：

```text
1. pipelined processing with message passing
2. on-demand data parallelism
3. JVM based implementation
```

然后全文都围绕这三个设计展开。它不是散弹枪式 profiling，而是每一个实验都在回答：

> 这个共同设计在现代硬件上到底造成了什么后果？

我们写 LLM serving profiling，也应该先归纳共同设计。我建议把现有 LLM serving framework 抽象为四个 common designs：

```text
1. KV State Management
   KV cache paging / block allocation / prefix cache / eviction / migration

2. Online Token Scheduling
   continuous batching / chunked prefill / preemption / priority / fairness

3. Heterogeneous Execution Lowering
   prefill-decode execution, CUDA kernels, tensor parallel, pipeline parallel, expert parallel, quantization

4. Program-aware Serving
   prefix sharing, radix tree, structured output, speculative decoding, multi-turn / RAG / agent DAG
```

文章就可以叫：

> **Revisiting the Design of LLM Serving Frameworks on Modern Accelerators**

或者更 profiling 味一点：

> **Where Does LLM Serving Time Go? A Profiling Study of Modern LLM Serving Frameworks**

但我更喜欢第一个。它有一点“旧王冠上的灰尘被风吹开”的味道，适合作为系统论文标题。

---

# 3. 不要写“谁更快”，要写“为什么快/为什么慢”

这篇 DSP 文章明确说，它的目标不是比较不同系统的绝对性能，而是评价 DSP 系统的共同设计在 scale-up architecture 上的问题，使结论可以推广到更多系统。

这点对我们尤其重要。

LLM serving 文章如果只写：

```text
vLLM 在 workload A 上比 SGLang 快 1.3x
SGLang 在 workload B 上比 vLLM 快 1.5x
TensorRT-LLM 在 batch C 上最快
```

很容易变成一张会过期的榜单。框架版本一更新，结论就像雨中的粉笔字。

更好的写法是：

```text
在 decode-heavy workload 中，瓶颈来自 KV cache read bandwidth 和 scheduler iteration overhead。

在 long-context prefill-heavy workload 中，瓶颈来自 prefill compute、attention kernel、chunked prefill 策略和 KV allocation pressure。

在 prefix-sharing workload 中，性能差异主要来自 prefix cache granularity、cache lookup overhead、cache eviction policy。

在 structured-output workload 中，瓶颈可能从 GPU kernel 转移到 CPU-side grammar/FSM checking 或 logits masking。
```

这样文章就不是“排行榜”，而是“解剖图”。

---

# 4. Benchmark 设计可以直接模仿它的哲学

DSP 文章不是随便挑几个程序，而是按照 Jim Gray 的 benchmark 原则设计了七个 stream applications，并强调 relevance、portability、scalability、simplicity 四个标准。它的 benchmark 覆盖不同 CPU/memory 行为和不同 topology complexity，比如 WC/FD/SD/TM 是 single-chain topology，LG/VS/LR 是复杂 topology；图 5 还把七个应用的拓扑结构画出来了。

我们写 LLM serving profiling，也应该设计一组“不是 leaderboard，而是机制探针”的 workload。

可以这样设计：

| Workload                          | 目的                                             | 对应 design                     |
| --------------------------------- | ---------------------------------------------- | ----------------------------- |
| **Short chat**                    | 测基础 serving overhead、scheduler overhead、ITL    | online scheduling             |
| **Long-context RAG**              | 测 prefill、KV allocation、chunked prefill        | KV state + prefill scheduling |
| **Long output generation**        | 测 decode bandwidth、KV read、inter-token latency | KV read path                  |
| **Shared-prefix multi-query**     | 测 prefix cache / radix cache 效果                | semantic reuse                |
| **Multi-turn conversation**       | 测历史 KV 增长、cache eviction、reuse                 | state lifecycle               |
| **Parallel sampling / Best-of-N** | 测分支复用、batching、scheduler fairness              | program runtime               |
| **Structured JSON output**        | 测 grammar/FSM/logits masking overhead          | constrained decoding          |
| **Mixed online workload**         | 测 tail latency、preemption、fairness             | production scheduling         |
| **Multi-GPU serving**             | 测 TP/PP/EP communication、KV transfer           | distributed execution         |

这组 benchmark 的核心不是“覆盖所有应用”，而是覆盖 LLM serving 的几个物理瓶颈：

```text
prefill-heavy
decode-heavy
KV-memory-heavy
cache-reuse-heavy
scheduler-heavy
communication-heavy
grammar-heavy
mixed-online-heavy
```

这比只用 ShareGPT 或固定 prompt/output 长度强很多。

---

# 5. Profiling 维度也可以学它：从系统指标走向硬件分解

DSP 文章很重视 profiling methodology。它不是只看 throughput/latency，而是进一步把执行时间拆成 computation time、branch misprediction stall、front-end stall、back-end stall，并继续细分 ITLB、L1-I cache、L1-D、L2、LLC local/remote 等硬件事件。表 I、表 II、表 III 分别列出 JVM profiling 工具、processor measurement components 和实验机器配置。

我们写 LLM serving profiling，也应该有类似层次：

```text
End-to-end metrics
  ├── request throughput
  ├── token throughput
  ├── TTFT
  ├── ITL / TPOT
  ├── E2E latency
  └── P50 / P95 / P99 tail latency

Serving runtime breakdown
  ├── queueing time
  ├── tokenization time
  ├── scheduling time
  ├── prefill time
  ├── decode time
  ├── KV allocation/free time
  ├── sampling time
  ├── structured decoding overhead
  └── network / RPC overhead

GPU/NPU breakdown
  ├── SM utilization
  ├── Tensor Core utilization
  ├── HBM bandwidth
  ├── L2 hit rate
  ├── kernel launch overhead
  ├── attention kernel time
  ├── GEMM time
  ├── NCCL communication time
  └── CPU-GPU synchronization time

KV-cache breakdown
  ├── allocated KV blocks
  ├── KV fragmentation
  ├── prefix cache hit rate
  ├── eviction count
  ├── preemption/recompute count
  ├── KV bytes read per token
  ├── KV migration bytes
  └── tokens per GB
```

这样文章才像真正的 profiling paper，而不是“我跑了几个命令，然后画柱状图”。

---

# 6. Finding 的组织方式非常值得抄骨架

DSP 文章的 Section V 叫 **Study the Impact of Common Designs**。每一节都是一个 finding：

```text
Finding (1): 大多数应用约 70% 时间花在 processor stalls 上。
Finding (2): massively parallel threading model 导致高 front-end stalls。
Finding (3): message passing + stream partition 在多 socket 上导致 remote memory access 和负载不均。
Finding (4): JVM runtime overhead 存在但相对 moderate，GC 只有 1–3%。
```

其中第一、第二个 finding 直接说明复杂线程模型造成 front-end stalls 和 L1 instruction cache 问题；第三个 finding 说明 message passing 在多 socket 上造成 remote memory access，四 socket 吞吐甚至只比单 socket 略高或更低；第四个 finding 则很有意思，它反而推翻了“GC 一定是大锅”的常识，指出 GC overhead 只有 1–3%。

我们的 LLM serving profiling 也应该用这种格式。比如可以设计成：

```text
Finding 1:
Continuous batching improves throughput, but tail latency is dominated by scheduler-induced waiting under mixed prefill/decode workloads.

Finding 2:
Decode is not compute-bound; it is KV-memory-bound. High SM utilization does not necessarily imply high serving efficiency.

Finding 3:
Paged KV cache reduces fragmentation, but block-level allocation introduces hidden costs under long-context and highly dynamic workloads.

Finding 4:
Chunked prefill protects decode latency, but aggressive chunking can reduce prefill efficiency and increase TTFT.

Finding 5:
Prefix caching is workload-sensitive. It is highly beneficial under shared system/RAG prompts but can become lookup/eviction overhead under low-reuse workloads.

Finding 6:
Structured decoding shifts part of the bottleneck from GPU kernels to CPU-side constraint management and logits masking.

Finding 7:
Multi-GPU scaling is limited less by FLOPS and more by communication, KV placement, and request-level load imbalance.
```

注意这里我们不要提前假装这些都是已经测出来的结论。写论文时，它们应该是 profiling 后的 findings；现在可以把它们作为**实验假设**和**章节设计靶心**。

---

# 7. 最强的启发：profiling 后必须给出“验证性优化”

这篇 DSP 文章不是只停在诊断。它发现问题后，提出两个小优化：

```text
1. non-blocking tuple batching
   用于减少 context switch 和 instruction cache miss

2. NUMA-aware executor placement
   用于减少 remote memory access
```

文章还分别评估单独优化和组合优化。non-blocking tuple batching 通过批处理多个 tuple 来减少上下文切换，但也讨论了 throughput 与 latency 的 trade-off；NUMA-aware executor placement 则把 executor placement 形式化成跨 socket communication cost 最小化问题，并映射到 minimum k-cut。 

这对我们写 LLM serving 非常重要。一个 profiling paper 如果最后只有“瓶颈在这里”，会显得像显微镜观光。更强的是给出一两个 **small but surgical** 的优化，让读者相信我们的诊断不是幻觉。

我们可以考虑下面几类验证性优化：

## 优化 A：KV-aware admission / routing

如果 profiling 发现 long-context 请求导致 KV cache pressure 和 preemption，那么可以做：

```text
根据当前 KV block availability、prefix cache hit probability、expected output length
决定请求是否进入当前 batch，或者路由到哪个 worker。
```

目标不是发明完整调度器，而是证明：

> KV cache pressure 是 serving tail latency 的核心变量。

## 优化 B：prefill/decode budget auto-tuning

如果发现 chunked prefill 的 token budget 对 TTFT/ITL 影响巨大，可以做一个简单策略：

```text
根据 decode queue length 和 prefill queue length 动态调整 prefill chunk size。
```

这对应 DSP 文章里的 tuple batching：
都是在吞吐和延迟之间调旋钮。一个是 tuple batch size，一个是 prefill chunk size。

## 优化 C：prefix-cache-aware batching

如果发现 shared-prefix workload 中 prefix cache 命中率决定性能，可以做：

```text
把共享 prefix 或相似 prefix 的请求尽量放到同一时间窗口 / 同一 worker。
```

这相当于 LLM serving 版的 NUMA-aware placement：
DSP 是让 producer/consumer 靠近；LLM serving 是让 prefix/KV reuse 靠近。

## 优化 D：grammar-aware scheduler

如果 structured output 的 CPU constraint checking 造成 decode loop 抖动，可以做：

```text
将 grammar complexity 作为 scheduling feature，
避免把大量复杂 grammar 请求塞进同一个 decode iteration。
```

这会把“语义复杂度”变成调度对象。这个方向很新，也很像 SGLang 的舒适区。

---

# 8. 我们可以照着它写一篇文章的结构

我建议文章结构如下：

## Title

**Revisiting the Design of LLM Serving Frameworks on Modern Accelerators**

## Abstract 逻辑

```text
LLM serving frameworks have rapidly evolved to support high-throughput online inference.
Despite implementation differences, modern frameworks share several core designs:
KV cache management, continuous batching, prefill/decode scheduling, and program-aware reuse.

However, LLM workloads and hardware platforms are changing:
long context, RAG, agents, structured outputs, multi-GPU serving, and heterogeneous accelerators.
This paper profiles representative serving frameworks under a set of mechanism-driven workloads.
Rather than comparing absolute performance, we study how common serving designs interact with modern accelerators.

Our profiling reveals ...
Based on these findings, we propose ...
```

这基本就是 DSP 文章的范式迁移。它原文也是先抽象 common designs，再强调硬件变化，再用代表系统 profiling，最后给出 bottleneck 和优化。

---

# 9. 建议的核心章节

```text
1. Introduction
   - LLM serving is becoming infrastructure.
   - Existing frameworks share common designs.
   - Hardware/workload assumptions are shifting.
   - We revisit these designs through profiling.

2. Background and Common Designs
   - Autoregressive serving lifecycle: prefill + decode.
   - KV cache as persistent state.
   - Continuous batching and scheduler.
   - Prefix caching / radix cache / structured generation.
   - Multi-GPU execution.

3. Methodology
   - Frameworks: vLLM, SGLang, TensorRT-LLM, TGI/LMDeploy.
   - Models: small, medium, large; dense and maybe MoE.
   - Hardware: single GPU, multi-GPU, multi-node if possible.
   - Workloads: short chat, long-context RAG, long output, shared-prefix, structured output, mixed online.
   - Metrics: TTFT, ITL, throughput, tail latency, GPU utilization, HBM bandwidth, KV stats.

4. Overall Performance
   - Show throughput/latency across workloads.
   - Do not over-interpret absolute ranking.

5. Study the Impact of Common Designs
   5.1 KV State Management
   5.2 Online Token Scheduling
   5.3 Prefill/Decode Interaction
   5.4 Hardware Execution and Communication
   5.5 Program-aware Serving

6. Towards More Efficient LLM Serving
   - KV-aware routing
   - adaptive chunked prefill
   - prefix-aware batching
   - grammar-aware scheduling

7. Discussion
   - What should future LLM serving frameworks expose?
   - What metrics are missing?
   - Why benchmark-only evaluation is insufficient?

8. Conclusion
```

这个结构会比普通 benchmark paper 更有“系统论文”的筋骨。

---

# 10. 可以提出一个非常有力的中心论点

我建议我们把文章的 central thesis 写成：

> **Modern LLM serving frameworks are no longer simple inference wrappers. They are online stateful systems whose performance is governed by the interaction among KV-cache state, token-level scheduling, accelerator kernels, and application-level program structure.**

中文就是：

> **现代 LLM serving framework 已经不是模型推理封装器，而是在线有状态系统。它的性能由 KV 状态、token 调度、硬件 kernel 和应用语义结构共同决定。**

这个论点非常强，因为它把 vLLM/SGLang 等系统放到了一个更抽象的坐标系里。

---

# 11. 和 DSP 文章的一一映射

可以这样对应：

| DSP 文章                         | 我们的 LLM serving 文章                                                              |
| ------------------------------ | ------------------------------------------------------------------------------- |
| Data stream processing systems | LLM serving frameworks                                                          |
| Storm / Flink                  | vLLM / SGLang / TensorRT-LLM / TGI                                              |
| Pipelined message passing      | Prefill/decode pipeline, request lifecycle, KV movement                         |
| On-demand data parallelism     | Continuous batching, TP/PP/EP, multi-GPU placement                              |
| JVM runtime                    | Python/C++/CUDA runtime, scheduler overhead, tokenizer/sampler/grammar overhead |
| NUMA remote memory access      | GPU-GPU/NIC/KV transfer, HBM locality, interconnect bottleneck                  |
| Tuple batching                 | Chunked prefill / scheduler token budget / decode batching                      |
| NUMA-aware executor placement  | KV-aware routing / prefix-aware placement / PD disaggregation placement         |
| Front-end/back-end stalls      | CPU scheduler overhead / GPU compute vs memory vs communication breakdown       |

这个映射非常自然。甚至可以把我们的文章写成某种精神续作：

```text
Revisiting Stream Processing on Multi-Core CPUs
        ↓
Revisiting LLM Serving on Modern Accelerators
```

---

# 12. 最后建议：文章应该有三个“杀招图”

这类 profiling paper 最怕图很多但没有灵魂。我们需要三张核心图：

## 图 1：LLM serving framework 的 common design stack

```text
Application / Program layer
  RAG, agent, structured output, shared prefix

Serving runtime layer
  scheduler, continuous batching, prefix cache, grammar runtime

State layer
  KV cache blocks, radix tree, eviction, migration

Execution layer
  prefill kernels, decode kernels, sampling, NCCL

Hardware layer
  GPU SM, HBM, L2, NVLink/PCIe/NIC
```

这张图负责奠定抽象。

## 图 2：Request lifecycle breakdown

```text
arrival
  → queue
  → tokenize
  → prefill wait
  → prefill compute
  → KV allocation
  → decode iterations
  → sampling / grammar
  → detokenize
  → response
```

然后每个阶段可以被 profiling。

## 图 3：Design-to-bottleneck matrix

| Design                | Benefit            | Hidden cost                  | Profiling signal                |
| --------------------- | ------------------ | ---------------------------- | ------------------------------- |
| Paged KV              | less fragmentation | block lookup, cache pressure | KV block churn, HBM BW          |
| Continuous batching   | high throughput    | queueing/tail latency        | ITL variance, batch occupancy   |
| Chunked prefill       | protects decode    | TTFT/prefill inefficiency    | prefill wait, decode starvation |
| Prefix cache          | avoids recompute   | lookup/eviction overhead     | hit rate, saved prefill tokens  |
| Structured decoding   | valid output       | CPU/GPU sync, mask overhead  | sampling time, grammar time     |
| Multi-GPU parallelism | larger model       | communication                | NCCL time, imbalance            |

这张图会让读者觉得：这不是“测了一堆”，而是“剖了一套系统”。

---

# 结论

这篇 DSP 文章对我们的最大启发是：

> **不要把 profiling 写成性能比较，而要写成 design revisiting。**

我们应该学习它的四步刀法：

```text
1. 先抽象现有系统的共同设计。
2. 再说明硬件和 workload 已经变化。
3. 用 profiling 证明共同设计的隐藏成本。
4. 最后用小优化验证诊断，并指出未来 framework 方向。
```

对于 LLM serving，我们的文章可以围绕这四个 design principle 展开：

```text
KV State Management
Online Token Scheduling
Hardware-aware Execution
Program-aware Serving
```

最后文章的 punchline 可以是：

> **Any high-performance LLM serving framework is ultimately a KV-cache operating system, a token scheduler, an accelerator runtime, and a language-program executor. Profiling it requires measuring all four layers together, not merely reporting tokens per second.**
