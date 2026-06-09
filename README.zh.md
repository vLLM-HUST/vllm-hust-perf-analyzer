# TraceLoom

TraceLoom 从底层 Ascend Profiling 数据中重建执行语义，并将原始 profiler 数据库转换为可查询、可复查的性能诊断产物。

它把原始 profiler 数据库中的低层事件、kernel 时间线和通信记录，整理成可读、可查、可复现的性能诊断结果，帮助开发者更容易地观察硬件真实行为、定位热点循环、识别通信/同步瓶颈，并把可疑开销回溯到更接近源码算子的位置。

**从原始时间线，到可行动的性能诊断。**

## 一眼看懂

```text
Ascend msprof database
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
Loop Tree Construction
    |
    v
Anchor-Auxiliary Cost Attribution
    |
    v
Tree Map / SQL Report / Augmented Profile DB
```

TraceLoom 的目标是把密集、低层、难阅读的 profile 数据库，压缩成少量结构化问题：

- 主要执行骨架是什么？
- 哪些循环或片段反复出现？
- 哪些 kernel、通信或同步片段贡献了主要开销？
- 哪些 auxiliary / prelude 事件解释了 anchor 周围的隐藏成本？
- 哪些节点值得继续用 SQL 回到原始 profiler 事件里查？

## 真实 Kickstart Bundle

```text
examples/kickstart_smoke/msprof_raw/
  PROF_...device_0.../msprof_20260609062629.db
  PROF_...device_1.../msprof_20260609062627.db

原始 CANN profile 数据库
  每张卡: 82 行 TASK
  每张卡: 8 行 COMMUNICATION_TASK_INFO
  每张卡: 8 行 COMMUNICATION_OP

TraceLoom 输出
  每张卡: 36 个 normalized events
  每张卡: 16 个 semantic anchors
  恢复出的结构: Repeat x8
    aclnnMatmul_MatMulCommon_MatMulV2
    hcom_allReduce__#_#_#
```

这份 bundle 已经放在 [examples/kickstart_smoke](examples/kickstart_smoke)。
它是在 Ascend 910B3 机器上采集的真实双卡 Ascend/CANN `msprof` smoke：一个很小的
torch-npu 分布式 workload 反复执行 `matmul -> gelu -> all_reduce`，因此 raw
数据库里同时包含计算 kernel 和 HCCL 通信行为。

TraceLoom 将两张卡的 raw trace 压缩成同构的 `MatMulV2 -> AllReduce` 循环结构，
便于并排比较。这就是推荐的第一步体验：先直接跑我们放进仓库的 profiler 数据库，
看 TraceLoom 如何把 raw trace 变成结构化诊断，再去分析自己的 workload。

安装后可以直接试一下：

```bash
traceloom analyze examples/kickstart_smoke/msprof_raw --devices 0,1
```

分析结果会写回：

```text
examples/kickstart_smoke/msprof_raw/traceloom/
```

打开里面的 `tree-map.md`，应该能看到两张卡都被重建成 `Repeat x8` 执行模式，
主体节点是 `MatMulV2` 和 `hcom_allReduce`。

## 核心方法

TraceLoom 在原始 profile 数据库之上增加一层可分析的结构：

- 读取 Ascend/CANN `msprof_*.db`，保留原始 profiler 表作为证据。
- 规范化低层事件，建立统一的 event、anchor、symbol 和 source link。
- 从 compute kernel、HCCL/collective、数据搬运和同步事件中抽取语义 anchor。
- 在 anchor 序列上发现重复执行模式，压缩成 loop tree / repeat tree。
- 区分主干 anchor kernel 与 auxiliary / prelude 事件，把等待、准备、通信碎片等成本归因到相邻结构。
- 生成增强 SQLite 数据库、可读 Markdown 报告和可复用 SQL 查询。

这样，开发者不需要手动翻大量 `msprof` 表和 Perfetto 时间线，就可以先看到“哪些模式重复出现、哪些节点最贵、哪些通信或同步片段夹在关键路径附近”，再用 SQL 继续下钻到原始事件。

## TraceLoom 术语

TraceLoom 使用一组稳定概念描述 profile 数据库中的执行语义：

- Semantic Execution Skeleton：由关键 compute、collective、data movement 和 sync anchor 组成的执行主干。
- Semantic Anchor：代表主要计算、通信或同步行为的语义锚点，是 pattern mining 的基本符号。
- Anchor-Auxiliary Attribution Model：把等待、准备、runtime 调用和通信碎片等 auxiliary/prelude 成本归因到相邻 anchor 或 loop node。
- Pattern Compression Tree：把重复 anchor 序列压缩成可阅读的 loop tree / repeat tree。
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
python3 -m compileall traceloom reproduce
```

本地快速分析脚本：

```bash
scripts/traceloom-analyze.sh /path/to/msprof_output
```

## License

TraceLoom 使用 MIT License。
