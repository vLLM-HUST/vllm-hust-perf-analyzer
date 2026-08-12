# 如何阅读 TraceLoom 的 queryable DB timeline

TraceLoom 的一等产物是 `analysis.db`：一条把原始 profiler 行、规范化事件、
恢复结构、occurrence、成本和 provenance 放在同一份 SQLite 里的
**queryable DB timeline**。使用者不需要先读 Markdown 报告；直接从数据库的
自描述入口开始，就能横向下钻证据，也能纵向比较同构执行。

## 60 秒上手

```bash
traceloom /path/to/profile.db
sqlite3 -readonly /path/to/traceloom/analysis.db
```

进入 `sqlite>` 后：

```sql
.headers on
.mode box
SELECT surface_name, relation_name, purpose
FROM traceloom_analysis_surface
ORDER BY surface_name;
```

如果使用仓库自带 profile，可以直接执行完整 tour：

```sql
.read examples/db-timeline-tour/tour.sql
```

## 两个基本阅读方向

### 横向：从结构走到原始证据

```text
structural node
  -> one occurrence
  -> ordered anchor / exact graph member
  -> normalized event
  -> embedded profiler table + row
```

横向关系回答“这段成本具体由哪些可观测动作组成”。节点不是孤立标签；它保留
父子关系、顺序、occurrence 和原始行定位。最常用入口：

| 关系 | 用途 |
| --- | --- |
| `traceloom_v_tree_node` | 从粗到细浏览层级 timeline。 |
| `traceloom_tree_node_occurrence` | 展开一个结构的具体 occurrence。 |
| `traceloom_tree_node_anchor` | 读取 occurrence 内的有序 anchor。 |
| `traceloom_v_node_graph_body_member` | 展开 exact replay 内部成员。 |
| `traceloom_event` | 查看规范化事件及时间、stream、role。 |
| `traceloom_v_event_source_locator` | 定位内嵌原始表和原始 row。 |
| `traceloom_v_anchor_runtime_call` | 从 device anchor 反查关联的 host runtime call。 |
| `traceloom_v_node_runtime_call` | 在 tree node / occurrence / anchor order 中双向查询 runtime/device 关系。 |
| `traceloom_v_aux_runtime_call` | 从 device aux 反查关联的 host runtime call。 |
| `traceloom_v_anchor_host_activity` | 查看相邻 device anchors 对应的 host endpoints 之间，profiler 实际观察到的 runtime calls。 |

### 从 device 结构反看 host runtime 行为

TraceLoom 不把某段 device idle 直接命名成一个“原因”。它先保留更一般、也更
可复核的关系：device anchor/aux 对应哪些 host runtime calls，以及相邻 anchors
所对应的 host endpoints 之间观察到了什么。

```sql
SELECT anchor_idx, anchor_symbol, api_name, match_policy,
       support_state, cardinality
FROM traceloom_v_anchor_runtime_call
ORDER BY device_id, anchor_idx, runtime_start_ns;
```

如果起点是压缩结构，直接保留 node occurrence 与位置：

```sql
SELECT node_id, occurrence_idx, anchor_order, anchor_idx,
       api_name, device_symbol, support_state
FROM traceloom_v_node_runtime_call
WHERE node_id = 'node-42' AND coverage_kind = 'self'
ORDER BY occurrence_idx, anchor_order, runtime_start_ns;
```

反过来按 `runtime_call_id` 过滤同一视图，就能看到它对应的 device work
落在哪些 node occurrences 中，不需要重建 provider join。

然后查询两个相邻 anchors 之间的 host runtime activity：

```sql
SELECT left_anchor_id, right_anchor_id, support_state,
       observed_runtime_call_id, api_name,
       round(observed_dur_us, 3) AS runtime_us
FROM traceloom_v_anchor_host_activity
WHERE left_anchor_id = 'anchor-42'
ORDER BY device_id, left_anchor_id, observed_start_ns;
```

把示例中的 `anchor-42` 换成当前结构下钻得到的 left anchor；不要无条件打印一份
大 profile 的全部 interval/call links。

这里的结论严格限于 profiler 可见的 runtime API；API 之间没有记录的空白不等于
CPU 没有工作。`support_state`、`cardinality` 和原始 row locator 应与结果一起读，
不要把缺失或多义关系误解为“没有 host call”。

这条查询路径不是每次临时做区间连接。augDB 会一次性物化
`traceloom_anchor_runtime_relation`、`traceloom_anchor_host_interval` 和
`traceloom_anchor_host_activity` 三层窄关系，并持久化索引与 planner statistics。
对于 runtime calls 很密集的大 profile，这会让分析数据库明显大于原始运输包；
取舍是有意的：TraceLoom 生成一次，然后人和 agents 反复做普通关系查询，而不再
各自重建昂贵且容易漂移的 host/device 关系。

### 纵向：固定结构，比较同构 occurrence

```text
one structural family / position
  -> all equivalent occurrences
  -> comparable cost population
  -> outlier or skew
```

纵向关系回答“同样结构在不同 occurrence、位置或 rank 上成本如何分布”。结构
本身定义统计总体，避免把不同阶段里同名算子粗暴混在一起。

```sql
SELECT occurrence_idx,
       round(total_us, 3) AS total_us,
       round(compute_us, 3) AS compute_us,
       round(comm_us, 3) AS comm_us,
       round(aux_us, 3) AS aux_us
FROM traceloom_tree_node_occurrence
WHERE local_node_id = 'N027'
ORDER BY total_us DESC;
```

## 从粗到细定位热点

先查询层级 timeline，而不是先做全库 top-k：

```sql
SELECT local_node_id, label, depth, occurrence_count,
       round(avg_total_us, 3) AS avg_total_us,
       round(total_us, 3) AS total_us
FROM traceloom_v_tree_node
ORDER BY total_us DESC
LIMIT 30;
```

找到感兴趣的 `local_node_id` 后，继续查询 occurrence、children、anchor、replay
member 和 raw row。仓库里的 `docs/report-sql/*.sql` 提供可执行的常见路径，
`traceloom_analysis_surface.example_sql` 则让数据库自己说明推荐入口。

## 成本口径

- `total_us = compute_us + comm_us + idle_us`：三者是互斥 wall-clock 桶；
  stream 重叠不会重复计时。
- 普通节点的平均值除以 `occurrence_count`。
- Repeat 节点的平均值除以 `occurrence_count * repeat_count`，因此可与一次
  body iteration 直接比较。
- `aux_us` 和 `self_us` 是证据/归因 lens，不是 `total_us` 的额外加数。
- replay 的 task sum、busy union、envelope 和类别成本是不同 lens，不应互相
  替代。

## 给 agent 的任务模板

只需要交给 agent 一份 `analysis.db` 和问题，不必再配一份 Markdown 地图：

```text
这是 TraceLoom 生成的 queryable DB timeline。先查询
traceloom_analysis_surface，找到层级、occurrence、cost 和 provenance 入口。
从总成本最高的 repeated region 开始：
1. 比较同构 occurrences 的成本分布；
2. 下钻最慢 occurrence 的有序 children / anchors；
3. 把关键 event 解析到内嵌 profiler 表和 row；
4. 输出所用 SQL、结构坐标、统计总体和证据边界。
不要根据名字推断 workload phase 或因果关系。
```

做 A/B 时，先用稳定结构坐标对齐 family、occurrence 或 position，再比较成本；
不要先按算子名全局汇总。SQL 与结果表应和 `analysis.db` 的 SHA-256 一起保留，
让诊断可复核。

## Markdown 和 JSON 的位置

`--loop-tree-out` 与 `--out` 仍可生成面向人或调试的投影，但它们不是另一套
产品，也不是分析前置条件。网页图、论文图和 Markdown 报告都应当由 queryable
DB timeline 的查询结果确定性生成。
