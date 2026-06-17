结论先放桌上：**这版论文设计草案已经从“工具论文”长出了“系统论文”的骨架，而且你这四个 design principle 是对的。** 但还需要一次“主语归位”和“证据收口”：论文主语应该是 **modern LLM serving frameworks 的共同设计如何在真实 workload / accelerator trace 中显形、失效、被验证**，TraceLoom 是证据引擎，不是唯一主角。

我会把现在的最佳定位定成：

> **Revisiting the Design of LLM Serving Frameworks through Structured Trace Attribution**

这比单纯 “TraceLoom: an augmented profile database” 强很多。材料里已经明确指出，当前 tex 最大问题不是内容错，而是把 TraceLoom 写成 queryable database / SQLite schema，导致文章容易掉到工具说明和 case-study demo 层级；更强的写法应把文章主语从 TraceLoom 改成 modern LLM serving frameworks，并把 TraceLoom 降一层放到 structured trace attribution layer。

## 1. 四个 design principle 成立，但要从“分类表”升级成“研究对象”

你现在这四层：

```text
1. State / Memory Design
2. Scheduling Design
3. Execution / Hardware Design
4. Semantic / Program Design
```

很稳。它们不是随手分类，而是能和三份材料完全合拢。

DSP profiling 那份材料给你的大范式是：**不要写谁比谁快，而要抽象 common designs，再用 profiling 证明这些 designs 在新硬件 / 新 workload 下暴露什么隐藏成本**。这正好适合 vLLM、SGLang、TensorRT-LLM、TGI、LMDeploy 这类 serving framework。 材料里也已经把 LLM serving 应该研究的问题写得很准：不是 vLLM vs SGLang 谁更快，而是这些框架共享哪些核心设计，这些设计在 GPU/NPU、长上下文、RAG、agent、多租户、结构化输出 workload 下是否仍然成立。

所以，四原则不要只放在 Background 里当 taxonomy。它们应该直接变成论文的 **四组 profiling questions**：

| Design principle         | 论文里应该问的问题                                                                  | 证据应该长什么样                                                                                                   |
| ------------------------ | -------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| **State / Memory**       | KV / prefix / block 管理是否真的减少了浪费，还是引入 lookup、churn、eviction、preemption 成本？  | long-context、shared-prefix、多轮 workload；KV block churn、hit/miss、preemption、saved tokens、TraceLoom node cost |
| **Scheduling**           | continuous batching / chunked prefill 是否把吞吐提升换成 TTFT、ITL、tail latency 的抖动？ | mixed online workload；decode loop repeat、queue wait、prefill wait、ITL variance                              |
| **Execution / Hardware** | 时间到底流向 compute、memory bandwidth、communication、sync、runtime overhead 哪里？    | distributed decode trace；communication neighborhood；Pattern Compression Tree；HCCL/NCCL share               |
| **Semantic / Program**   | prefix、DAG、grammar、agent / structured output 是否改变了调度单位和瓶颈位置？               | shared-prefix、structured JSON、parallel sampling、agent-like DAG；grammar/FSM time、prefix reuse、branch reuse  |

这张表就是论文的脊椎。不要让它躺在 Related Work 里睡觉，要让它在 Section 3 里发号施令。

## 2. 这套草案最强的中心论点已经出现了

我建议保留并强化这句话：

> **Modern LLM serving frameworks are no longer simple inference wrappers. They are online, stateful runtime systems whose performance is governed by the interaction among KV-cache state, token-level scheduling, accelerator execution, and application-level program structure.**

这句话在材料里已经被标成中心论点，并且后面还接上了 TraceLoom 的方法论论点：end-to-end metrics 和 flat kernel tables 不够，profiler 必须恢复 repeated execution structure，把 auxiliary costs 归因到 stable semantic anchors，并连接 runtime changes 和 source-level optimization hypotheses。

我会把中文 punchline 稍微磨尖一点：

> **LLM serving engine 不是模型 forward 的外壳，而是一个围绕 KV 状态、token 调度、accelerator lowering 和程序语义复用运转的在线运行时系统。**

这句话和你开头那句“token 操作系统”非常搭。可以放在 Introduction 第一页偏前的位置。它有钩子，有抽象，也有系统味。

## 3. TraceLoom 的角色：不是论文主角，是“证据层”

这点非常关键。TraceLoom 不应该被写成：

```text
We build an augmented profile database.
```

而应该被写成：

```text
We build a structured trace attribution layer for design-level profiling.
```

毕业设计那份材料明确说明，它的价值是在 raw timeline 和 top-k kernel table 之间补一层结构化中间表示：原始 trace 太碎，top-k 表太扁，无法回答某个开销属于哪个 decode loop、是否每层都出现、是否靠近 collective、是否对应源码里的 buffer reuse / all-to-all path；TraceLoom 通过 key-event skeleton、auxiliary window、repeated fragment compression、node aggregation 来补这个缺口。

所以整篇文章的方法链应该写成：

```text
mechanism-driven workload
  → macro metric symptom
  → runtime breakdown
  → structured trace attribution
  → source-level hypothesis
  → perturbation / ablation validation
```

材料里已经把这条 pipeline 写得很清楚：macro benchmark 到 runtime breakdown，再到 repeated decode loop / model layer loop / communication neighborhood，最后到 source-level hypothesis 和 ablation / patch validation。

一句话：**TraceLoom 是显微镜，但论文不是“显微镜说明书”，而是“用显微镜重访 LLM serving 解剖学”。** 🔬

## 4. 现在最危险的地方：证据覆盖不够时过度承诺

这版草案最容易被审稿人戳的地方是：标题和贡献说的是 modern LLM serving frameworks，但实验可能主要是 Ascend / vLLM-Ascend trace。材料里已经提醒过，如果实验规模暂时不够跨 vLLM/SGLang/TensorRT-LLM，就不要在贡献里承诺 representative frameworks，而应该写成：

```text
We instantiate the study on vLLM-Ascend and discuss how the methodology extends to other serving engines.
```

这个处理是对的，很稳。

因此我建议最终 contribution 写法分两档。

**如果实验主要是 vLLM-Ascend：**

```text
C1. We identify four recurring design principles in modern LLM serving:
KV state management, online token scheduling, hardware-aware execution,
and program-aware serving.

C2. We formulate a mechanism-driven profiling methodology that maps each
design principle to workload probes, macro symptoms, trace signals, and
perturbation tests.

C3. We present TraceLoom, a structured trace attribution layer that compresses
raw accelerator timelines into semantic anchors, repeated execution structures,
auxiliary-cost windows, and queryable evidence tables.

C4. We instantiate this methodology on Ascend/vLLM-Ascend traces and show
how repeated decode/layer structures, communication neighborhoods, and
auxiliary costs can be recovered from low-level profiler output.

C5. Through perturbation case studies, including HCCL/AIV configuration and
Decode All-to-All Buffer Reuse, we show that source-plausible optimizations
do not necessarily change the targeted runtime path beyond run-to-run variance.
```

**如果后面补了 vLLM/SGLang/TensorRT-LLM/TGI/LMDeploy 的实测：**

再把 C4 改成 representative frameworks。现在不要急着吹号角，不然号角会反咬人。

## 5. 四原则中，前三个可以做 finding，第四个先当 implication 更安全

你的四原则理论上都成立，但证据强度不一样。

**Execution / Hardware** 是目前最强的主菜。TraceLoom 已经能处理 repeated decode/layer structure、communication neighborhood、auxiliary window、AIV / all-to-all 周边路径，这一块最容易出硬 finding。材料里也明确说，Hardware Execution and Communication 是 TraceLoom 目前最强的地方，可以围绕 distributed decode 的重复 layer 结构和 communication anchors 写 finding。

**Scheduling** 可以做中强 finding，但需要显式 stage marker 或至少能可靠恢复 decode loop / prefill wait / queue wait。否则容易变成“我们推测 scheduler 造成了 ITL variance”。

**State / Memory** 很有价值，但如果没有 KV block lifecycle、prefix cache hit/miss、preemption/recompute、eviction 等信号，就只能写 workload-sensitive hypothesis，不能写成硬结论。材料里对 KV finding 的证据要求也写得很明确：需要 prefix/shared workload 或 long-context workload、KV allocation / block churn / cache hit signals、TraceLoom node-level cost 变化。

**Semantic / Program** 是理论上最漂亮、未来感最强的一层，但如果没有 structured output、shared-prefix、agent / DAG、grammar/FSM 实验，就不要硬写 Finding 5。材料里也已经警告：Program-aware Serving 如果暂时没有足够实验，应作为 discussion-backed finding 或 future-facing analysis，不要写成已验证结论。

所以我建议 Section 6 这样安排：

```text
6. Study the Impact of Common Designs

6.1 State / Memory Design
    finding candidate, 需要 KV 相关证据支撑

6.2 Scheduling Design
    finding candidate, 需要 mixed workload + ITL/TTFT + loop evidence

6.3 Execution / Hardware Design
    hard finding, 当前最强

6.4 Auxiliary Runtime Cost
    hard finding, TraceLoom 独特贡献

6.5 Semantic / Program Design
    implication / future-facing analysis，除非补 structured output / prefix reuse 实验
```

这会让论文看起来诚实、有层次，不会像把所有愿望都塞进一个热气球里。

## 6. Negative case 是亮点，不是弱点

Decode All-to-All Buffer Reuse 那个结果非常值得保留。它不是“优化没成功所以尴尬”，而是可以变成全文最有研究味的证据：

> **LLM serving profiling 的任务不是证明每个优化都有效，而是判断一个优化假设是否真的改变了目标运行路径。**

材料里已经给出很好的负例：Buffer Reuse 从源码上看合理，但结构化 profile 发现关键事件成本稳定，第一处 AIV 通信前置辅助窗口方差很高，baseline 和 Buffer Reuse 的均值差异小于跨 run / 跨设备波动；宏观 request throughput 从 17.38±0.96 rps 到 17.85±0.78 rps，generation time 从 0.923±0.050s 到 0.898±0.040s，变化和 run-to-run variance 同量级，因此不能作为性能提升证据。

这个 negative case 可以让论文从“我们做了个 profiler”升到：

```text
我们能判断一个源码级优化假设有没有真的改变目标执行结构。
```

这比一个小幅正优化更高级。它像一枚冷静的银针，扎破“理论上应该更快”的气球。

## 7. Workload 设计要变成“机制探针”，不要只用 ShareGPT

DSP 文章的启发不是“找几个 benchmark 跑一跑”，而是 benchmark 要服务于 common design。材料里已经明确建议 LLM serving workload 应覆盖 short chat、long-context RAG、long output、shared-prefix、multi-turn、parallel sampling、structured JSON、mixed online、multi-GPU serving，用来探测 scheduler、KV、semantic reuse、grammar、communication 等不同机制。

我建议最小可行实验集先不要贪大，先做 4 个：

```text
W1. Long output decode
    主打 Execution / KV read / repeated decode loop

W2. Multi-GPU decode
    主打 communication neighborhood / all-to-all / HCCL-AIV

W3. Mixed prefill + decode
    主打 scheduling / ITL variance / chunked prefill

W4. Shared-prefix or structured JSON
    二选一，支撑 Program-aware 或 State reuse
```

如果时间紧，W4 可以先弱化成 discussion，不要硬塞进主 finding。

## 8. 最终论文骨架我建议这样定

```text
1. Introduction
   - LLM serving 从 inference wrapper 变成 online stateful runtime
   - 现有 profiling 不够：macro 太粗，kernel top-k 太扁，timeline 太碎
   - 本文目标：revisit common designs through structured trace attribution
   - contributions

2. Background: LLM Serving as an Online Stateful Runtime
   - prefill / decode
   - KV cache lifecycle
   - continuous batching
   - hardware lowering
   - program-aware serving

3. Common Designs and Profiling Questions
   - D1 State / Memory
   - D2 Scheduling
   - D3 Execution / Hardware
   - D4 Semantic / Program
   - Design → workload → macro signal → trace signal → perturbation matrix

4. Methodology
   - mechanism-driven workloads
   - metrics: TTFT, ITL, throughput, P95/P99, variance
   - trace collection and normalization
   - perturbation design

5. TraceLoom: Structured Trace Attribution
   - semantic anchors
   - auxiliary/prelude attribution
   - repeated pattern discovery / Pattern Compression Tree
   - node cost table
   - trace-to-source hypothesis loop

6. Study the Impact of Common Designs
   - Findings 1–4, 按证据强度排列
   - Program-aware serving 如果证据不足，放 implication

7. Perturbation Case Studies
   - HCCL / AIV perturbation
   - Decode All-to-All Buffer Reuse negative case

8. Implications
   - future profilers need KV lifecycle markers
   - scheduler iteration IDs
   - prefix hit/miss counters
   - grammar/FSM cost markers
   - communication neighborhood IDs
   - trace-level optimization gates

9. Related Work

10. Conclusion
```

这和材料里建议的章节方向一致：方法节要对齐 profiling methodology，而不是过早讲 augmented profile database；Overall Performance 只报告宏观症状，不做排行榜；Study the Impact of Common Designs 才是主菜。

## 9. 我对当前草案的评分式判断

**论文主线：8.5/10。**
四原则很强，TraceLoom 的位置也找对了。最重要的是它不是“又一个 profiling tool”，而是“LLM serving design-level profiling 的证据层”。

**新颖性：7.5/10 到 8.5/10，取决于实验。**
如果只做 vLLM-Ascend + TraceLoom demo，新颖性会被压低。如果能把 perturbation、negative case、repeated structure、optimization gate 讲透，就会明显升上去。

**系统论文味：已经有了。**
尤其是“不做 leaderboard，而做 design revisiting”这一点非常正确。材料里也把最后 punchline 说得很好：高性能 LLM serving framework 本质上是 KV-cache operating system、token scheduler、accelerator runtime、language-program executor，profiling 必须同时测四层，而不是只报 tokens/sec。

**最大风险：overclaim。**
不要让标题、摘要、贡献暗示你已经完整研究了 vLLM / SGLang / TensorRT-LLM / TGI / LMDeploy 的所有实现。如果当前证据主要来自 Ascend/vLLM-Ascend，就写成 instantiated study + methodology extension。这样更像成熟系统论文，不像烟花自述。

## 10. 最后给一个更锋利的论文定位

我建议最终一句话定位成：

> **This paper is a design-level profiling study of modern LLM serving runtimes. It identifies four recurring design principles, maps them to mechanism-driven workloads and trace-level signals, and uses TraceLoom to test whether serving optimizations actually change the intended repeated execution structures.**

中文版本：

> **这篇论文不是 TraceLoom 工具说明，而是一篇现代 LLM serving runtime 的 design-level profiling study。它把 serving engine 的共同设计压缩成 KV 状态、在线 token 调度、硬件执行、程序语义四层，再用 TraceLoom 把宏观性能变化追到重复执行结构和源码级优化假设上。**

这一版就很能打。它有大问题，有方法，有证据链，也有冷静的负例。现在要做的是收口：**少许愿，多落证；少排行，多归因；少说“我们更快”，多问“目标路径真的变了吗”。**
