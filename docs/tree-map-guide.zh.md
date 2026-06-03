# 如何使用 tree-map.md

`tree-map.md` 是 TraceLoom 分析产物里最适合人工阅读的入口。它不是最终报告，而是一张性能地图：先帮助用户定位值得看的结构节点，再把这些节点编号带回增强数据库，继续查具体事件、区间和成本构成。

推荐顺序：

1. 先打开 `summary.md`，确认分析的是哪些 DB 和 device。
2. 再打开 `tree-map.md`，按 `total_us`、`Repeat xN`、算子标签和 `depth` 找热点。
3. 记下感兴趣的 `node`，例如 `N027`。
4. 使用 `queries/*.sql` 或自己写 SQL，从 `dbNN.traceloom_augmented.db` 继续展开。

## 关键事件和辅助事件模型

TraceLoom 先把 profiler 里的原始事件整理成两类语义事件。

**关键事件 anchor**

anchor 是构成 timeline tree 的主体事件，通常是具体计算算子、通信事件或数据搬运事件。树结构、重复循环和节点覆盖范围主要基于 anchor 序列生成。`tree-map.md` 里的每个 leaf node 最终都能追到一个或多个 anchor。

**辅助事件 aux**

aux 是被归因到后续 anchor 的前置或辅助成本，例如等待、runtime 调用、launch 前后的准备开销等。它们默认不直接出现在 `tree-map.md` 的表格里，否则地图会很碎；但它们会进入 `avg_aux_us` 和更细的 SQL 报告。

这个模型的含义是：

- `tree-map.md` 负责展示主要结构：循环、重复 body、关键算子和总成本。
- aux 成本不会丢失，只是默认折叠到 anchor 或 node 的成本统计里。
- 当某个 node 的 `avg_aux_us` 明显偏高时，应该继续查 `traceloom_v_node_aux_cost` 或 `queries/node-cost-breakdown.sql`。

## 表格列怎么理解

`tree-map.md` 的列刻意保持精简。

| 列 | 含义 | 怎么用 |
| --- | --- | --- |
| `node` | 可查询的节点编号，例如 `N027`。 | 复制到 SQL 里的 `local_node_id` 条件。 |
| `label` | 节点标签。可能是算子名、通信名、`Seq[...]` 或 `Repeat xN`。 | 用来识别热点结构和算子类型。 |
| `depth` | 树深度。0 是根，数字越大越靠近内部循环或具体算子。 | 结合 `Repeat xN` 判断循环层级。 |
| `occ` | 这个 tree node 在展开 timeline 里出现了多少次。 | 估计该节点是单个结构节点还是循环 body 中反复出现的节点。 |
| `avg_total_us` | 单次 node occurrence 的平均总成本。 | 看一次执行有多贵。 |
| `avg_aux_us` | 单次 occurrence 平均辅助成本。 | 判断 launch、等待、前置准备等是否值得继续查。 |
| `total_us` | 该 node 所有 occurrence 的总成本。 | 用来排序热点，决定优先分析对象。 |

注意 `Repeat x47` 这种节点本身通常 `occ=1`，表示它是一个被压缩后的循环结构节点；它下面的 body 节点可能 `occ=47`，表示这个算子在展开执行中出现了 47 次。也就是说，`occ` 统计的是当前 node 的 occurrence，不是 anchor 数量。

## 示例片段

下面截取自一个真实分析产物：

```text
msprof_raw/traceloom/tree-map.md
```

| node | label | depth | occ | avg_total_us | avg_aux_us | total_us |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| N001 | Seq[29] | 0 | 1 | 1706399.257 | 71757.275 | 1706399.257 |
| N006 | RmsNorm | 1 | 1 | 595626.261 | 877.342 | 595626.261 |
| N008 | Repeat x47 | 1 | 1 | 223881.752 | 18977.237 | 223881.752 |
| N009 | aclnnMatmul_MatMulV3Common_MatMulV3 | 2 | 47 | 451.18 | 403.771 | 21205.483 |
| N011 | aclnnMatmul_MatMulV3Common_MatMulV3 | 2 | 47 | 2301.957 | 0.0 | 108192.002 |
| N023 | PpMatmulAccumAtomicKernel | 1 | 1 | 219263.541 | 1353.925 | 219263.541 |
| N026 | Repeat x47 | 1 | 1 | 248044.059 | 40709.511 | 248044.059 |
| N027 | FusedInferAttentionScore | 2 | 47 | 492.258 | 838.158 | 23136.125 |
| N030 | aclnnMatmul_MatMulV3Common_MatMulV3 | 2 | 47 | 2322.199 | 0.0 | 109143.344 |

从这段可以直接读出几件事：

- `N001` 是根序列，总成本约 1.7 秒。
- `N008` 和 `N026` 是两个被压缩的 `Repeat x47` 循环结构。
- `N011` 和 `N030` 是循环 body 内部反复出现的 matmul，`occ=47`，总成本超过 100 ms。
- `N027` 的 `avg_aux_us` 高于 `avg_total_us`，说明 attention anchor 周围有较多前置/辅助成本，应继续查 aux 和事件明细。

## 从 node 回查增强数据库

`tree-map.md` 里的 `node` 对应增强数据库视图 `traceloom_v_tree_node.local_node_id`。用户可以先确认节点：

```bash
traceloom report /path/to/traceloom/db01.traceloom_augmented.db \
  --query "select * from traceloom_v_tree_node where local_node_id = 'N027'" \
  --format md
```

展开某个 node 的每次出现：

```bash
cp /path/to/traceloom/queries/node-occurrences.sql /tmp/node-occurrences.sql
# 把 SQL 文件里的 N027 改成你关心的 node
traceloom report /path/to/traceloom/db01.traceloom_augmented.db \
  --sql /tmp/node-occurrences.sql \
  --format md \
  -o /tmp/N027-occurrences.md
```

查看 node 覆盖的具体 profiler 事件：

```bash
cp /path/to/traceloom/queries/node-events.sql /tmp/node-events.sql
# 把 SQL 文件里的 N027 改成你关心的 node
traceloom report /path/to/traceloom/db01.traceloom_augmented.db \
  --sql /tmp/node-events.sql \
  --format md \
  -o /tmp/N027-events.md
```

查看计算、通信、idle、aux 等构成：

```bash
cp /path/to/traceloom/queries/node-cost-breakdown.sql /tmp/node-cost-breakdown.sql
# 把 SQL 文件里的 N027 改成你关心的 node
traceloom report /path/to/traceloom/db01.traceloom_augmented.db \
  --sql /tmp/node-cost-breakdown.sql \
  --format md \
  -o /tmp/N027-cost.md
```

如果想自己写 SQL，常用入口是：

| 视图 | 用途 |
| --- | --- |
| `traceloom_v_tree_node` | 和 `tree-map.md` 对应的节点地图。 |
| `traceloom_tree_node_occurrence` | 展开 node 的每次 occurrence。 |
| `traceloom_tree_node_anchor` | 从 node occurrence 连接到 anchor。 |
| `traceloom_anchor` | anchor 序列和 anchor 标签。 |
| `traceloom_event` | 归一化后的 profiler 事件。 |
| `traceloom_v_node_cost` | node 的计算、通信、idle、aux 成本汇总。 |
| `traceloom_v_node_aux_cost` | node 关联的 aux 成本明细。 |

## 让 agent 帮忙做后续分析

TraceLoom 的输出适合和 Codex 等代码/数据分析 agent 配合使用。推荐把 `tree-map.md`、`docs/augmented-db-schema.md`、`queries/*.sql` 和目标 DB 路径交给 agent，让它自动生成 SQL、跑查询、导出更多 Markdown/CSV 表格。

可以给 agent 这样的任务：

```text
请阅读 tree-map.md，找出 total_us 最高的 Repeat 节点和它们的 body 节点。
然后基于 db01.traceloom_augmented.db 写 SQL：
1. 展开这些节点的 occurrence；
2. 汇总每个节点的 compute/comm/idle/aux 占比；
3. 对 avg_aux_us 较高的节点列出具体 aux 事件标签和耗时；
4. 输出 Markdown 表格。
```

也可以让 agent 辅助做对比实验：

```text
这里有 baseline 和 optimized 两个 traceloom bundle。
请用 tree-map.md 先匹配相同 label 和相似 depth/Repeat 结构的热点节点，
再查询两个增强 DB，对比每个匹配节点的 total_us、avg_total_us、avg_aux_us 和成本构成。
最后生成一张优化前后对比表。
```

使用 agent 时建议明确三点：

- 只把 `tree-map.md` 当地图，不要只凭 Markdown 下结论。
- 需要结论时必须回查 `dbNN.traceloom_augmented.db`。
- 生成的 SQL 和输出表格要保留下来，方便复核和放进实验记录。
