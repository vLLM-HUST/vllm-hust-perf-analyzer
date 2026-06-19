# TraceLoom

TraceLoom 是面向 Ascend 推理 runtime trace 的结构化 trace analysis framework。

它从底层 Ascend Profiling 数据中重建执行语义，把原始 `msprof` 数据库转换为可查询、可复查的性能诊断产物。TraceLoom 关心的不只是“哪个 kernel 最慢”，而是“这个 AI inference runtime 到底在做什么”：主要 kernel、重复执行模式、同步停顿、通信瓶颈，以及更接近源码算子的可疑原因。

在仓库内置的真实 vLLM-Ascend kickstart profile 上，TraceLoom 将 118 万+ 行
selected profiler records 压缩成 112 个结构节点，并在两张 Ascend 设备上恢复出同构的
`Repeat x36 -> Repeat x24` 嵌套执行模式。最终得到的是一棵紧凑的 Pattern
Compression Tree，让重复 runtime 结构、HCCL 通信和 kernel 级热点可以一眼看到。

核心主线是：

```text
trace compression -> structure recovery -> cost attribution -> evidence drill-down
```

**从原始时间线，到可行动的性能诊断。**

## 一眼看懂

```text
Raw Ascend msprof trace
    |
    v
Event Normalization
    |
    v
Semantic Anchor Extraction
    |
    v
Pattern Discovery
    |
    v
Pattern Compression Tree Construction
    |
    v
Anchor-Auxiliary Cost Attribution
    |
    v
Evidence-linked Diagnosis Artifact
(Tree Map / SQL Report / Augmented Profile DB)
```

TraceLoom 的目标是把密集、低层、难阅读的 profile 数据库，压缩成少量结构化问题：

- 这个 runtime 实际上在做什么？
- 主要执行骨架是什么？
- 哪些循环或片段反复出现？
- 哪些 kernel、通信或同步片段贡献了主要开销？
- 哪些 auxiliary / prelude 事件解释了 anchor 周围的隐藏成本？
- 哪些节点值得继续用 SQL 回到原始 profiler 事件里查？

## 真实 Kickstart Bundle

```text
examples/kickstart_smoke/msprof_raw/
  PROF_...device_0.../msprof_20260609064817.db
  PROF_...device_1.../msprof_20260609064834.db

真实 vLLM-Ascend 生成 profile
  模型: Qwen2.5-0.5B-Instruct
  硬件: Ascend 910B3
  并行: tensor_parallel_size=2
  请求: 1 个 prompt, 生成 12 token

原始 profile 规模
  device 0 DB: 84,928 行 TASK, 518,110 行 CANN_API, 1,775 行 HCCL task
  device 1 DB: 57,455 行 TASK, 518,489 行 CANN_API, 1,773 行 HCCL task

TraceLoom 压缩结果
  device 0: 33,964 个 normalized events -> 11,008 个 semantic anchors -> 58 个 tree nodes
  device 1: 32,220 个 normalized events -> 10,510 个 semantic anchors -> 54 个 tree nodes
  selected raw table rows -> structural nodes: 1.18M+ -> 112 (~10,500:1)
  恢复出的同构结构:
    外层 decode/warmup block: Repeat x36
      层内 block: Repeat x24
        MatMul / Rope / HCCL AllReduce / AddRmsNorm / SwiGlu
```

这份 bundle 已经放在 [examples/kickstart_smoke](examples/kickstart_smoke)。
它是在 Ascend 910B3 机器上采集的真实双卡 Ascend/CANN `msprof` profile：
用 vLLM-Ascend 加载 `Qwen2.5-0.5B-Instruct`，在两张 NPU 上做 tensor parallel，
对一个 prompt 生成 12 个 token。这个 profile 包含 engine 初始化、HCCL 初始化、
ACL graph capture/replay、prefill 和 decode，因此它的原始数据库形态更接近真实推理
profile，而不是整齐的人造 kernel 序列。

TraceLoom 将两张卡的 raw trace 压缩成可并排比较的 Pattern Compression Tree。
最适合展示的结果是：两个 device 都被恢复出同构的嵌套循环结构，外层是
`Repeat x36`，内部包含层级 `Repeat x24`，层内节点包括 `MatMul`、
`AtbRopeKernel`、`hcom_allReduce`、`AddRmsNormBias` 和 `SwiGlu`。
这就是推荐的第一步体验：先直接跑仓库里的真实 profile 数据库，看 TraceLoom
如何把 raw timeline 压成结构化诊断，再分析自己的 workload。

## 示例发现

对这份 kickstart bundle，TraceLoom 能直接给出几类判断：

- 主导执行结构是重复的 transformer-style 区域：`Repeat x36` 约占 device 0
  结构时间的 89%，约占 device 1 结构时间的 91%。
- 嵌套的 `Repeat x24` 是主要层内 block，约占两张卡结构时间的 75-77%。
- HCCL AllReduce 出现在层内关键路径里，和 `MatMul`、`AtbRopeKernel`、
  `AddRmsNormBias`、`SwiGlu` 一起构成重复层结构。
- 通信成本不会被藏在另一个报告里：重复 block 自带 communication 成本列，
  外层 block 的通信占比在 device 0 上约 15%，在 device 1 上约 9%。
- 最终阅读面很小：仅 `TASK`、`CANN_API` 和 HCCL task 这几类表就有
  118 万+ 行 profiler 记录，TraceLoom 将它们压缩成两张卡共 112 个结构节点。

安装后可以直接试一下：

```bash
traceloom analyze examples/kickstart_smoke/msprof_raw
```

分析结果会写回：

```text
examples/kickstart_smoke/msprof_raw/traceloom/
```

打开里面的 `tree-map.md`，应该能看到两个 device section。device 0 中有
`N014 Repeat x36` 和嵌套的 `N017 Repeat x24`；device 1 中有
`N010 Repeat x36` 和嵌套的 `N013 Repeat x24`。这就是 TraceLoom 的价值：
把数十万行低层 profiler 事件压缩成少量可对比、可下钻的执行结构。

## 核心方法

TraceLoom 在原始 profile 数据库之上增加一层可分析的结构：

- 读取 Ascend/CANN `msprof_*.db`，保留原始 profiler 表作为证据。
- 规范化低层事件，建立统一的 event、anchor、symbol 和 source link。
- 从 compute kernel、HCCL/collective、数据搬运和同步事件中抽取语义 anchor。
- 在 anchor 序列上发现重复执行模式，压缩成 Pattern Compression Tree。
- 区分主干 anchor kernel 与 auxiliary / prelude 事件，把等待、准备、通信碎片等成本归因到相邻结构。
- 生成增强 SQLite 数据库、可读 Markdown 报告和可复用 SQL 查询。

这样，开发者不需要手动翻大量 `msprof` 表和 Perfetto 时间线，就可以先看到“哪些模式重复出现、哪些节点最贵、哪些通信或同步片段夹在关键路径附近”，再用 SQL 继续下钻到原始事件。

## TraceLoom 术语

TraceLoom 使用一组稳定概念描述 profile 数据库中的执行语义：

- Semantic Execution Skeleton：由关键 compute、collective、data movement 和 sync anchor 组成的执行主干。
- Semantic Anchor：代表主要计算、通信或同步行为的语义锚点，是 pattern mining 的基本符号。
- Anchor-Auxiliary Attribution Model：把等待、准备、runtime 调用和通信碎片等 auxiliary/prelude 成本归因到相邻 anchor 或 loop node。
- Pattern Compression Tree：TraceLoom 的核心结构产物。它不是原始执行树，
  而是把重复 anchor 序列压缩成可阅读 runtime structure 的压缩树。
- Tree Map：面向人类阅读和 SQL 下钻的节点成本地图。

## 项目贡献

TraceLoom 关注 profile 数据库分析层，提供几个核心能力：

- 将原始 `msprof` 产物转换为可查询的增强 profile 数据库。
- 用 semantic anchor 表示分布式推理中的主要计算、通信和同步行为。
- 自动发现重复执行结构，突出 decode 循环、关键算子序列和热点模式。
- 建立 anchor 与 auxiliary/prelude 成本之间的联系，帮助解释隐藏在 kernel 之间的等待和准备开销。
- 用 `tree-map.md`、`summary.md` 和 SQL 报告，把复杂 trace 压缩成适合人工阅读和复现实验的诊断产物。

一句话：TraceLoom 把“人工翻 trace、猜热点、对照源码”的过程，变成一套自动化、可解释、可复查的 profile 数据库分析流程。

## 最常用命令

安装：

```bash
python3 -m pip install -e .
```

如果旧系统 Python 上 editable install 报 `build_editable` 相关错误，先升级用户态
构建工具再重试：

```bash
python3 -m pip install --user --upgrade pip setuptools wheel
python3 -m pip install --user -e .
```

把 `msprof` 产物目录交给 TraceLoom：

```bash
traceloom analyze /path/to/msprof_output
```

大多数情况下只需要这一条命令。TraceLoom 会自动发现目录下的 `PROF_*/msprof_*.db`，完成分析，并把结果写回原始 profile 目录：

```text
/path/to/msprof_output/traceloom/
```

支持的输入布局：

```text
<run_dir>/msprof_raw/PROF_*/msprof_*.db
<raw_dir>/PROF_*/msprof_*.db
```

如果想把结果写到别的位置：

```bash
traceloom analyze /path/to/msprof_output --out-dir /path/to/analysis
```

对团队共享的大实验仓库，尤其是原始 `msprof` 产物通过 Git LFS 同步时，建议显式把
TraceLoom 输出写到原始 artifact 目录之外：

```bash
git lfs pull
git lfs fsck --objects
traceloom analyze experiments/profiler/exp_001/profiler/msprof \
  --out-dir /tmp/traceloom-exp001
```

这样原始 LFS artifact 目录可以保持只读，也能避免把临时生成的 `traceloom/`
分析 bundle 误提交进实验仓库。需要沉淀到仓库里的通常是小体积摘要、关键 SQL
查询结果，或者指向生成 bundle 的路径；除非团队明确决定把增强分析 DB 也纳入
artifact policy。

如果只想分析部分卡：

```bash
traceloom analyze /path/to/msprof_output --devices 3,4,5,6
```

## 输出产物

默认输出是一个紧凑清晰的分析 bundle：

```text
traceloom/
  README.md
  summary.md
  tree-map.md
  db01.traceloom_augmented.db
  db02.traceloom_augmented.db
  queries/
    tree-map.sql
    node-events.sql
    node-occurrences.sql
    node-cost-breakdown.sql
  meta.json
```

核心产物分为三类。

### 增强 Profile 数据库

- `dbNN.traceloom_augmented.db`
- 原始 `msprof` 表保留不动。
- TraceLoom 额外加入 `traceloom_*` 表和视图。
- 后续复查、对比和自动化报告都可以基于这些 DB 做 SQL 查询。

### 可读性能地图

- `summary.md`：概览分析了哪些 DB、哪些 device，以及最高成本结构。
- `tree-map.md`：第一眼应该看的性能地图，突出重复结构、热点节点和成本列。

`tree-map.md` 中的 `node` 编号可以直接用于 SQL 下钻，例如 `N027`、`N060`。

### 查询脚本

- `queries/tree-map.sql`：生成 tree map 的 SQL 版本。
- `queries/node-events.sql`：查看某个 node 覆盖的具体 profiler 事件。
- `queries/node-occurrences.sql`：展开某个 node 的所有 occurrence。
- `queries/node-cost-breakdown.sql`：查看 compute、communication、idle、aux 等成本构成。

## 怎么读 `tree-map.md`

`tree-map.md` 是 TraceLoom 给用户的“性能地图”。它保留最适合第一轮判断的列：

| 列 | 含义 |
| --- | --- |
| `node` | 节点编号，例如 `N008`。后续 SQL 查询可以用它定位。 |
| `label` | 节点标签，通常是算子名、通信名或 `Repeat xN`。 |
| `depth` | 树深度。数字越大，越靠近内部循环或具体算子。 |
| `occ` | 这个 tree node 出现了多少次。 |
| `avg_total_us` | 每次出现的平均总成本。 |
| `avg_aux_us` | 归因到这个节点的辅助/前置成本，例如等待、准备、runtime 调用。 |
| `total_us` | 这个节点所有出现次数加起来的总成本。 |

典型读法：

1. 先看 `summary.md`，确认分析范围和事件规模。
2. 打开 `tree-map.md`，从 `total_us` 高的节点往下看。
3. 找到感兴趣的 `node`，例如 `N060`。
4. 用 `queries/node-events.sql` 或 `queries/node-cost-breakdown.sql` 继续查原始事件和成本组成。

更完整的阅读方法、真实表格示例、anchor/aux 成本模型和 agent 辅助分析建议见 [docs/tree-map-guide.zh.md](docs/tree-map-guide.zh.md)。

## 用 SQL 深挖

TraceLoom 给出一张可读地图和一个可查询数据库：先用 Markdown 快速定位，再用 SQL 进入原始事件和成本构成。

运行内置 SQL：

```bash
cp /path/to/msprof_output/traceloom/queries/node-events.sql /tmp/node-events.sql
# 把 /tmp/node-events.sql 里的 N027 改成你要查询的节点，例如 N060
traceloom report /path/to/msprof_output/traceloom/db01.traceloom_augmented.db \
  --sql /tmp/node-events.sql \
  --format md \
  -o /tmp/N060-events.md
```

也可以直接写 inline SQL：

```bash
traceloom report /path/to/db01.traceloom_augmented.db \
  --query "select node_id, label, depth, occurrence_count, total_us from traceloom_v_tree_node order by total_us desc limit 20" \
  --format md
```

常用视图：

- `traceloom_v_tree_node`：树节点地图，适合做热点排序和节点筛选。
- `traceloom_tree_node_occurrence`：每个 node 的展开出现次数。
- `traceloom_tree_node_anchor`：node occurrence 到 anchor 事件的链接。
- `traceloom_anchor` / `traceloom_event`：更底层的语义事件和 profiler 事件。

## 推荐工作流

```text
1. 用户自己运行 workload，并用 msprof 采集 profile。
2. traceloom analyze 读取 msprof 产物目录。
3. 用户先看 summary.md 和 tree-map.md。
4. 找到感兴趣的 node。
5. 用 queries/*.sql 继续追具体事件、区间和成本构成。
6. 对比两个实验时，按 tree path、label、repeat/occ 结构匹配热点节点。
```

例如 copy vs gather、优化前 vs 优化后、不同通信策略之间的比较，都可以先从 `tree-map.md` 找同形状热点循环，再进入 DB 查更细的事件。

## 开发检查

修改代码后可以运行：

```bash
python3 -m compileall traceloom
```

## License

TraceLoom 使用 MIT License。
