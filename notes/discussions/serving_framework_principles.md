可以。我们可以把 vLLM、SGLang、TensorRT-LLM、TGI、LMDeploy、Sarathi、Orca 类系统全部压缩成 **四个不可逃避的 design principle**。

总判断是：

> **大模型推理引擎的核心不是“模型 forward”，而是围绕 KV 状态、在线调度、硬件映射、语义复用构建的一套 token 操作系统。**

我建议归纳为这四个 design：

```text
1. State / Memory Design：KV cache 是第一公民
2. Scheduling Design：请求不是 batch，而是在线 token 流
3. Execution / Hardware Design：把 ragged workload 降维成硬件友好的 kernel
4. Semantic / Program Design：利用 prompt、DAG、grammar、agent 结构做复用和约束
```

前三个是物理定律，第四个是应用形态带来的系统定律。少了前三个，引擎跑不稳；少了第四个，引擎会越来越不适合真实 LLM 应用。

---

# 1. State / Memory Design：KV cache 是第一公民

这是第一性原理。

LLM decode 的状态不是一个小变量，而是每个请求不断增长的 **KV cache**。每生成一个 token，系统都要为这个请求追加新的 K/V，并且未来每一步 attention 都会读它。

所以任何推理引擎都必须回答：

```text
KV cache 放在哪里？
怎么分配？
怎么释放？
怎么复用？
怎么换出？
怎么跨卡 / 跨机迁移？
怎么避免碎片？
```

这就是 vLLM 的核心贡献。PagedAttention 把 KV cache 切成固定大小 block，让逻辑上连续的上下文可以存放在物理上不连续的显存块里。vLLM 的相关文档也明确描述，PagedAttention 的核心是把每个请求的 KV cache 划分成 KV blocks，并允许这些 blocks 存储在非连续物理内存中，从而按需分配、减少碎片。([vLLM][1])

这个设计本质上是：

```text
token sequence logical address
        ↓
block table / page table
        ↓
physical KV cache blocks
        ↓
attention kernel reads by indirection
```

所以 vLLM 不是简单做了一个 attention kernel，而是把 KV cache 从普通 tensor 变成了 **虚拟内存对象**。

SGLang 的 RadixAttention 则是同一个原则的另一种展开：不是只解决 KV 的物理碎片，而是进一步解决 **KV 的语义复用**。SGLang 论文摘要说，SGLang runtime 用 RadixAttention 做 KV cache reuse，并用 compressed finite state machines 加速结构化输出 decoding。([神经信息处理系统会议论文集][2])

所以第一个不可逃避的 design 是：

> **推理引擎必须把 KV cache 作为系统级状态管理，而不是作为模型 forward 的副产品。**

它可以叫 PagedAttention，可以叫 vAttention，可以叫 RadixAttention，可以叫 hierarchical KV cache，可以做 GPU/CPU/NVMe/offload，也可以做 KV quantization，但逃不掉这个问题。

核心指标也因此不是单纯 TFLOPS，而是：

```text
KV cache hit rate
KV fragmentation
tokens per GB
cache eviction cost
cache migration cost
prefix reuse ratio
memory bandwidth utilization
```

一句话：

> **LLM serving 的显存不是仓库，而是活水系统。KV cache 是水流，engine 是水利工程。** 🌊

---

# 2. Scheduling Design：请求不是 batch，而是在线 token 流

第二个原则是调度。

传统推理 batch 是这样的：

```text
collect batch
run batch
return batch
```

但 LLM serving 不是这样。因为每个请求都有不同长度、不同 prompt、不同输出长度、不同停止时刻。一个请求可能刚开始 prefill，另一个请求已经 decode 到第 200 个 token，还有一个请求刚结束释放 KV cache。

所以真实 workload 是：

```text
request A: prefill 8000 tokens
request B: decode token #37
request C: decode token #2
request D: waiting for KV blocks
request E: finished
request F: grammar-constrained decoding
```

这就决定了任何推理引擎都必须做 **online scheduling**，也就是按 iteration / token 粒度不断重新组织 batch。

这就是 continuous batching 的本质：

```text
每一轮 forward 之前：
  1. 看哪些 decode 请求要继续
  2. 看哪些 prefill 请求可以加入
  3. 看 KV cache 是否够
  4. 看 max batched tokens 是否够
  5. 看 latency / throughput / priority 怎么权衡
```

vLLM 的文档里，chunked prefill 被描述为把大的 prefill 拆成小块，并与 decode 请求一起 batch，以平衡 compute-bound 的 prefill 和 memory-bound 的 decode；其调度策略会优先 decode，再用剩余 token budget 安排 prefill。([vLLM][3])

这个细节非常关键。它说明推理引擎的 scheduler 不是普通队列，而是在解一个动态优化问题：

```text
maximize throughput
minimize inter-token latency
minimize time-to-first-token
avoid KV cache overflow
respect priority / fairness
avoid decode starvation
avoid long-prefill blocking
```

vLLM 甚至在 KV cache 不足时支持 preemption，通过暂停部分请求释放 KV cache 空间，之后再 recompute。官方优化文档明确说，当 KV cache 空间不足以处理所有 batched requests 时，vLLM 可以 preempt requests 来释放 KV cache。([vLLM][3])

所以第二个不可逃避的 design 是：

> **推理引擎必须把请求调度从“batch-level”提升到“token-level / iteration-level”。**

不同系统可以有不同策略：

```text
FIFO
priority scheduling
shortest remaining processing time
decode-first
prefill-first
chunked prefill
prefill-decode disaggregation
speculative decoding scheduling
KV-aware routing
multi-tenant fairness
```

但它们都在回答同一个问题：

> **下一次 forward，到底应该算哪些 token？**

这是推理引擎真正的“内核调度器”。

---

# 3. Execution / Hardware Design：把不规则 token 流降维成硬件友好的 kernel

第三个原则是硬件映射。

GPU / NPU / TPU 喜欢的是：

```text
dense matrix
regular shape
large batch
high arithmetic intensity
predictable memory access
```

但 LLM serving 给它的是：

```text
ragged sequences
dynamic batch
variable prompt length
variable decode length
non-contiguous KV blocks
mixed prefill/decode
grammar constraints
LoRA adapters
MoE routing
```

这就是一场系统级翻译：
**把不规则的在线 token 流，翻译成硬件能高效执行的 kernel。**

这包括：

```text
attention kernel design
paged attention kernel
flash attention backend
CUDA graph / graph capture
operator fusion
quantization
tensor parallelism
pipeline parallelism
expert parallelism
data parallelism
prefill-decode disaggregation
communication overlap
speculative decoding verification
```

SGLang 官方文档列出的 fast runtime 特性里，就包括 RadixAttention、zero-overhead CPU scheduler、prefill-decode disaggregation、speculative decoding、continuous batching、paged attention、tensor/pipeline/expert/data parallelism、structured outputs、chunked prefill、quantization 和 multi-LoRA batching。([SGLang Documentation][4])

vLLM 的优化文档也显示，tensor parallelism 可以把模型参数切到多张 GPU 上，pipeline parallelism 可以把模型层分布到多张 GPU 上；这些设计会改变每张 GPU 上模型权重和 KV cache 的空间压力，但也会引入同步或延迟成本。([vLLM][3])

所以第三个不可逃避的 design 是：

> **推理引擎必须做 hardware-aware lowering：把动态请求状态降到静态或半静态的高效计算形态。**

这里的核心矛盾是：

```text
prefill:
  compute-bound
  大 GEMM
  更像训练里的 forward

decode:
  memory-bound
  小 batch
  KV cache 读带宽敏感
  latency-sensitive
```

因此，一个好引擎一定要在这两个阶段之间做资源平衡。

这也是为什么现在越来越多系统会做：

```text
prefill worker
decode worker
KV transfer layer
router
cache-aware placement
```

因为 prefill 和 decode 根本不是同一种硬件 workload。把它们混在一起能简单部署；把它们拆开，才可能在大规模服务里榨出极限。

一句话：

> **scheduler 决定算什么，KV manager 决定数据在哪里，execution backend 决定怎么把它喂给硬件。**

---

# 4. Semantic / Program Design：LLM 应用不是单请求，而是程序

前三个原则已经可以解释 vLLM 的大部分设计。
但要解释 SGLang，还必须加入第四个原则：**语义层 runtime**。

真实 LLM 应用越来越不是：

```text
user prompt → model → answer
```

而是：

```text
system prompt
+ RAG context
+ tool call
+ multi-turn state
+ parallel candidates
+ verifier
+ JSON schema
+ retry
+ branch
+ agent loop
```

这已经不是单条请求，而是一个 **language model program**。

SGLang 论文正是从这个角度出发：它指出 LLM 越来越用于需要多次 generation call、高级 prompting、control flow、structured inputs/outputs 的复杂任务；SGLang 由 frontend language 和 runtime 组成，用来高效执行复杂 language model programs。([神经信息处理系统会议论文集][2])

这时，推理引擎就不能只看 token 序列，还要看请求背后的语义结构：

```text
哪些 prompt prefix 是共享的？
哪些 branch 可以复用 KV？
哪些 generation 可以并行？
哪些输出必须满足 JSON / regex / EBNF？
哪些 tool call 可以提前 speculative execution？
哪些子任务组成 DAG？
```

SGLang 的 structured outputs 文档也说明，它可以用 JSON schema、regular expression 或 EBNF 来约束模型输出，并保证输出遵循给定约束。([SGLang 文档][5])

所以第四个不可逃避的 design 是：

> **推理引擎必须从“token serving runtime”进化为“LLM program runtime”。**

这个原则不一定每个简单 chat server 都立刻需要，但一旦进入 RAG、agent、workflow、多轮对话、structured extraction，它就会变成核心。

它的典型设计包括：

```text
prefix tree / radix tree
request DAG
shared KV cache
grammar-constrained decoding
structured output FSM
parallel sampling
tool-call runtime
speculative execution
semantic cache
cache-aware router
```

这也是 vLLM 和 SGLang 的气质差异：

```text
vLLM 更像：
  token OS kernel

SGLang 更像：
  LLM program runtime
```

但未来两者会收敛。vLLM 会越来越语义化，SGLang 也必须越来越像高性能内核。

---

# 四个 design 放在一起

可以压成这张表：

| Core Design                     | 它解决什么              | 典型问题                         | 代表机制                                                                   |
| ------------------------------- | ------------------ | ---------------------------- | ---------------------------------------------------------------------- |
| **State / Memory Design**       | KV cache 生命周期      | 显存碎片、KV 复用、KV 迁移、KV offload  | PagedAttention、RadixAttention、prefix cache、KV quantization             |
| **Scheduling Design**           | 在线 token 流调度       | TTFT、ITL、吞吐、公平性、preemption   | continuous batching、chunked prefill、decode-first、PD disaggregation     |
| **Execution / Hardware Design** | 不规则 workload 的硬件映射 | kernel efficiency、带宽、通信、并行策略 | paged attention kernel、FlashAttention、TP/PP/EP、quantization、CUDA graph |
| **Semantic / Program Design**   | 复杂 LLM 应用执行        | agent、RAG、多分支、结构化输出、共享前缀     | request DAG、Radix tree、grammar/FSM、tool runtime                        |

如果强行归纳成三个，也可以把第 3 和第 4 合并成 **Execution Design**。
但我更建议保留四个，因为它们代表四层不同抽象：

```text
Memory layer      : KV state
Scheduler layer   : token flow
Execution layer   : hardware lowering
Program layer     : semantic structure
```

这四层组合起来就是：

```text
LLM Inference Engine
= KV State Manager
+ Online Token Scheduler
+ Hardware Execution Backend
+ Semantic Program Runtime
```

---

# 为什么说“任何大模型推理引擎都逃不出这些规律”

因为它们不是 vLLM 或 SGLang 的偶然实现，而是 autoregressive Transformer serving 的结构性约束。

## 规律一：只要有 autoregressive decoding，就必须管理 growing state

每个 token 都依赖之前的 token。
所以 KV cache 不会消失。最多是被压缩、分页、换出、裁剪、量化、分层存储。

逃不掉：

```text
KV cache memory management
```

---

## 规律二：只要有在线请求，就必须做 dynamic scheduling

真实用户不会整齐到达，也不会等长结束。
所以静态 batch 一定低效。

逃不掉：

```text
continuous / iteration-level scheduling
```

---

## 规律三：只要用加速器，就必须做 hardware-aware lowering

硬件喜欢规整矩阵，LLM serving 是不规则流。
中间必须有一层把不规则变成规整。

逃不掉：

```text
specialized kernels + parallel placement
```

---

## 规律四：只要 LLM 应用复杂化，就必须利用语义结构

RAG、agent、多轮、JSON extraction、tool use 都有共享上下文和控制流。
不利用这些结构，就会重复 prefill、重复 decode、重复验证，成本会爆。

逃不掉：

```text
prefix / DAG / grammar / program-aware runtime
```

---

# 更抽象一点：四个问题

任何推理引擎最后都会被拷问这四个问题：

```text
1. State:
   我的历史 token 状态在哪里？怎么复用？怎么搬？

2. Schedule:
   下一轮应该算哪些 token？谁先谁后？谁被暂停？

3. Execute:
   这些 token 怎么变成高效 kernel？怎么跨 GPU / NPU / 节点？

4. Semantics:
   这些请求之间有没有共享前缀、结构约束、程序依赖？
```

vLLM 的强项主要在 1、2、3。
SGLang 的强项是把 4 拉到台前，同时也覆盖 1、2、3。

所以最后的归纳可以非常锋利：

> **未来所有高性能 LLM 推理引擎，本质上都会变成“KV cache 操作系统 + token scheduler + accelerator compiler + LLM program runtime”。**

或者再短一点：

> **LLM 推理引擎的核心设计规律：管状态、排 token、贴硬件、吃语义。**

[1]: https://docs.vllm.ai/en/v0.6.2/automatic_prefix_caching/details.html?utm_source=chatgpt.com "Implementation - vLLM Documentation"
[2]: https://papers.nips.cc/paper_files/paper/2024/hash/724be4472168f31ba1c9ac630f15dec8-Abstract-Conference.html "SGLang: Efficient Execution of Structured Language Model Programs"
[3]: https://docs.vllm.ai/en/stable/configuration/optimization/ "Optimization and Tuning - vLLM"
[4]: https://sgl-project.github.io/ "SGLang Documentation — SGLang"
[5]: https://docs.sglang.ai/advanced_features/structured_outputs.html "Structured Outputs - SGLang Documentation"
