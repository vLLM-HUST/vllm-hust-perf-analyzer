# 如何阅读 TraceLoom 的 queryable DB timeline

TraceLoom 的一等产物是 `analysis.db`：一条把原始 profiler 行、规范化事件、
恢复结构、occurrence、成本和 provenance 放在同一份 SQLite 里的
**queryable DB timeline**。使用者不需要先读 Markdown 报告；直接从 Position、
Occurrence 和有序执行树边开始，形成结构等价的统计人口，再切换 device、host、
replay、collective 或原始证据 lens。

## 60 秒上手

```bash
traceloom /path/to/profile.db
sqlite3 -readonly /path/to/traceloom/analysis.db
```

进入 `sqlite>` 后：

```sql
.headers on
.mode box
SELECT projection_name, population_mode, resolution,
       observation_domain, measure_lens, selector_parameters, purpose
FROM traceloom_projection_recipe
WHERE projection_name IN (
  'hpo_positions', 'tree_edge_roles', 'hpo_occurrences', 'tree_edges',
  'equivalent_tree_edges', 'edge_role_bubble_summary', 'edge_role_bubbles',
  'occurrence_host_windows',
  'occurrence_host_context', 'host_window_calls', 'event_audit')
ORDER BY display_order;
```

如果使用仓库自带 profile，可以直接执行完整 tour：

```sql
.read examples/db-timeline-tour/tour.sql
```

## 核心玩法：沿同一组结构坐标逐级释放证据

TraceLoom 不生成一张不可改变的报告。它把执行关系物化成稳定坐标，让使用者沿
五个互相独立的轴组合视图：

| 投影轴 | 常见选择 |
| --- | --- |
| scope | Position、Occurrence、ordered edge、device window |
| population | 某一次 occurrence、同一结构坐标下的全部 occurrences |
| resolution | edge roles、concrete edges、events、exact replay members、raw rows |
| observation domain | device、受支持的 host windows、内嵌 profiler evidence |
| measure lens | overlap-safe cost、occurrence cost、scheduled work、bubble、host API distribution |

默认调查路径是：

```text
Position
  -> contextual edge roles
  -> one concrete Occurrence edge stream
  -> one equivalent-edge population
  -> one unusual child Occurrence
  -> cost / host / replay / collective lens
  -> literal evidence
```

先从 `hpo_positions` 选择一个任意深度的结构坐标：

```sql
SELECT position_id, parent_position_id, preorder_idx, symbol, label,
       node_type, repeat_count, occurrence_count, total_us
FROM traceloom_v_position
ORDER BY total_us DESC, db_idx, tree_id, preorder_idx;

.parameter init
.parameter set :position_id 'node-N286'
.parameter set :occurrence_id NULL
```

不要先按算子名构造统计总体。查询该 Position 下已经过上下文区分的边角色：

```sql
SELECT edge_role_id, edge_label, first_edge_order,
       parent_occurrence_count, concrete_edge_count,
       edges_per_parent_min, edges_per_parent_max, population_support
FROM traceloom_v_tree_edge_role
WHERE parent_position_id = :position_id
ORDER BY parent_tree_path, first_edge_order;
```

同名算子可以是不同 `edge_role_id`；只有同一个 role 的 concrete edges 才是合法
统计人口。选择一个 role 后，用 `equivalent_tree_edges` 比较它在各个 parent
Occurrences 中的 child cost，并从返回的 `child_occurrence_id` 选择异常实例。
需要读某一次真实展开时，先从 `hpo_occurrences` 取得 `occurrence_id`，再运行：

```sql
SELECT edge_order, edge_ordinal_in_role, edge_role_id,
       edge_label, child_occurrence_id
FROM traceloom_v_tree_edge
WHERE parent_occurrence_id = :occurrence_id
ORDER BY edge_order;
```

每一行都在同一个 concrete order 平面上；`edge_ordinal_in_role` 是从这个顺序派生的
第几次同类边，不是要求用户另行组合的坐标轴。

如果问题是“同一个结构位置前面的 device 空档怎样分布”，不要把所有 bubbles
全局展开后再关联。将选中的 `edge_role_id` 传给 `edge_role_bubble_summary`，先看该
等价边人口中正 bubble、无正 bubble、host 可观察性以及 `bubble_us` 分布；再用
`edge_role_bubbles` 展开具体 child Occurrences。它保留每一条 concrete edge，且用
`positive_bubble` / `no_positive_bubble` 明确区分是否存在正的未覆盖区间。返回的
`child_occurrence_id` 可切换到 occurrence host lens，`host_interval_id` 可直接进入
`host_window_calls`。这里的 host 行为只是相邻 anchors 提供的上下文证据，不是对
device bubble 原因的自动归因。
summary 中的 `positive_bubble_host_observation_coverage` 只以正 bubble 为分母；
当一个 role 没有正 bubble 时，零表示“不适用”，不表示其普通 host windows 全部
不可观察。

这条 recipe 会先锁定一个 role，再借助已有 structural-position 索引选择 bubbles；
不会物化全局 edge×bubble 或 interval×runtime-call 关系。旧的全局 bubble hotspot
和 API-family aggregate 暂时保留，因为它们仍提供尚未完全迁移的分析动作。

选出异常 `child_occurrence_id` 后，可以直接绑定为 `:occurrence_id` 并运行
`occurrence_host_windows` 或 `occurrence_host_context`。它们保留原 host 投影中缺端点、
host 顺序不单调、受支持但没有观察到 runtime call 等 typed 结果，却不再要求用户
把 Occurrence 手工拆回旧的 `node_id + occurrence_idx`。返回的 `interval_id` 还能继续
进入 `host_window_calls` 和 `runtime_call_audit`。

每份 `analysis.db` 都用 `traceloom_projection_recipe` 自描述这些组合。需要让 agent
自动选择下一步时，查询 `traceloom_projection_parameter`、
`traceloom_projection_coordinate` 和 `traceloom_v_projection_continuation`：

```sql
SELECT source_column, target_projection, target_parameter
FROM traceloom_v_projection_continuation
WHERE source_projection = 'equivalent_tree_edges'
ORDER BY target_projection, source_column;
```

`scope_*`、anchor-order `position_*` 和 Pattern-named replay recipes 暂时仍是兼容入口，
但不再是默认教学模型。它们中好的 host、bubble、replay 和 evidence 方法会逐条迁到
Position/Occurrence/edge 坐标上；物理表只有在失去独有证据、消费者和物化价值后
才会删除。迁移边界见
[`augdb-sql-ux-migration.md`](augdb-sql-ux-migration.md)，完整投影契约见
[`composable-analytical-projections.md`](composable-analytical-projections.md)。

如果 profile 中存在受支持的 exact replay，数据库还直接给出 replay 边界诱导的
完整 device 成本分区，不需要在客户端重新拼接区间：

```sql
SELECT tree_id, device_id, segment_order, coordinate_kind,
       coordinate_index, segment_label, anchor_count, total_us
FROM traceloom_v_exact_replay_partition
ORDER BY db_idx, device_id, tree_id, segment_order;
```

结果依次包含开放前缀 `X1`、replay `R1...`、replay 间段 `U1...` 和开放后缀
`X2`。先查询 `traceloom_v_exact_replay_partition_status`；只有 exact replay
边界完整、有效、有序且互不重叠时才会发布分区行。不满足契约时，status 会保留
typed reason，而不是让客户端从时间戳猜测成员关系。这个关系只表达可观察的
replay 分区，不推断它对应 prefill、decode 或任何模型语义。

需要进入 replay 内部读成本时，不必背诵底层表名。三条 recipe 组成一条自描述
下钻路径：

```text
replay_cost_units
  -> replay_cost_launches (:cost_unit_id, optional :slot_order)
  -> replay_cost_members  (:launch_id)
  -> event_audit          (:event_id)
  \\-> replay_structural_placements (:replay_unit_id)
      -> scope_host_context (:node_id, :occurrence_idx)
```

`replay_cost_units` 先显式返回每个 exact ReplayUnit 的 support 状态；支持的 unit
再展开为有序 launch slots，以及 task sum、busy union、envelope 和类别成本。
选中一个 `launch_id` 后，`replay_cost_members` 从
`traceloom_v_replay_position_realization_member` 返回同一个 Position 平面上的精确
成员、时长、scheduled-work share 与 `event_id`。其中 `observed_order` 按时间戳把
不同 stream 的算子、collective 与辅助事件交错显示，同时保留
`lane_ordinal/task_ordinal`；它只是观测坐标，不代表依赖或因果。`policy_role` 保留
内部成员原本的 anchor/auxiliary 身份，`final_role` 则保留外层 replay 作为
`protected_boundary` 的扁平 grammar 契约。`scope_exact_replay_members` 也会返回同一组
`cost_unit_id`、`launch_id` 和 `slot_order` 坐标，因此可以从一个树结构 occurrence
自然分叉到 replay cost 或 host context，而不用重新猜 replay 边界。
反过来，如果分析者先从成本总体选中了一个 `replay_unit_id`，
`replay_structural_placements` 会返回它出现的所有 `node_id/occurrence_idx`，不会
武断假设 replay 与 graph-unit occurrence 一一对应；ancestor、template 和 member
scope 不会冒充 placement，返回坐标可以继续进入 host context。

captured topology 中没有 scheduled member 的空 lane 仍然保留为 topology，不会
制造零成本 member。只有 task-kind 成员完整、非空 stream/lane 一一对应且 lane
落在声明拓扑内时，replay cost unit 才是 `supported`。

以下横向、纵向、层级和 cross-domain 阅读方式不是四套模型，而是这组坐标上的
不同投影。

`hpo_positions` 保留 `preorder_idx`、父 Position、结构路径、symbol 与成本，因此
“位于某个 replay 区域之前的重复结构”这类带时间线位置约束的选择仍能从统一入口
完成。需要一次真实展开时再进入 `tree_edges`；不要为了读取顺序退回底层表重建
另一套坐标。

## 审计 TraceLoom 自己做出的变换

`analysis.db` 不只保存分析结果，也保存产生结果的决策链。遇到“为什么这条 event
没有单独成为 anchor”“为什么两个 backend label 可以比较”或“为什么两条
profiler row 只计了一次成本”时，不要重新猜实现逻辑；从对应的 policy/rule 开始，
再下钻 decision、placement/member 与原始行。

| 变换 | 配置与规则 | 单条决定与下钻 | 典型问题 |
| --- | --- | --- | --- |
| evidence-role projection | `traceloom_evidence_role_policy`, `traceloom_evidence_role_rule` | `traceloom_v_evidence_role_decision`, `traceloom_v_evidence_role_placement`, `traceloom_evidence_role_issue` | event 为什么成为 anchor、aux、transparent 或 unknown anchor？它被放进了哪个结构？ |
| sparse event reconciliation | `traceloom_event_reconciliation_policy`, `traceloom_event_reconciliation_rule` | `traceloom_event_reconciliation_decision`, `traceloom_event_reconciliation_member`, `traceloom_v_event_reconciliation` | 多条 observation 为什么合成一个 canonical anchor？谁贡献 timing、symbol 与 cost？ |
| structural-symbol normalization | `traceloom_symbol_normalization_policy`, `traceloom_symbol_normalization_rule` | `traceloom_v_anchor_symbol_lineage`, `traceloom_v_symbol_normalization_placement`, `traceloom_v_symbol_variant_cost` | observed backend label 为什么映射到某个 structural symbol？同一结构位置有哪些具体 lowering？ |

三类审计都遵循同一种 UX：**先确认有效策略，再统计决定分布，然后只对一个 event、
anchor、decision 或结构位置做有界下钻，最后用 source locator 回到内嵌 profiler
row**。数据库的 `traceloom_analysis_surface` 会分别列出这些入口及可运行 SQL；
`docs/report-sql/` 提供现成的窄查询。event reconciliation 的完整示例见
[`event-reconciliation-audit.md`](event-reconciliation-audit.md)。

## 基本阅读方向

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
| `traceloom_v_sync_runtime_call` | 查询 profiler 明确标记的同步动作及其 runtime/device 关系。 |
| `traceloom_v_anchor_host_activity` | 查看相邻 device anchors 对应的 host endpoints 之间，profiler 实际观察到的 runtime calls。 |
| `traceloom_v_node_host_interval` | 在 node occurrence / anchor position 中保留每个带类型的 host interval，包括缺端点与不支持状态。 |
| `traceloom_v_node_host_activity` | 按 node occurrence 与 anchor position 比较 anchor 之后的 host runtime 分布。 |
| `traceloom_v_structure_bubble_position` | 按结构位置比较 bubble population 与 host 支持覆盖率，不因缺少 API 统计而丢行。 |

`resolution_status = 'embedded_raw'` 是逐行承诺，不只是“原始表已经复制进来”：
analysis DB 发布前会验证 event、runtime call 与 device work 的每个 literal
`source_key` 都能命中声明的内嵌原始行。过期或格式错误的 key 会使写入原子失败，
不会留下看似可下钻、实际悬空的 locator。

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

如果问题专门针对 profiler 已明确标记的同步动作，使用窄视图，而不是重新解释
所有 aux：

```sql
SELECT sync_action_id, sync_kind, api_name, runtime_dur_us,
       match_policy, evidence_level, support_state, cardinality
FROM traceloom_v_sync_runtime_call
WHERE support_state IN ('supported_exact', 'supported_deterministic')
ORDER BY device_start_ns;
```

CUDA 的 `sync_kind` 会被解码成 `EVENT_SYNCHRONIZE`、
`STREAM_WAIT_EVENT` 或 `STREAM_SYNCHRONIZE` 等 CUPTI 类型；Ascend 当前只纳入
有直接 `connectionId` 关系的 `EVENT_RECORD`/`EVENT_WAIT` device tasks。
这个视图表示“同步动作与哪个 runtime call 有可审计关系”，不表示某个 wait 一定
由哪个 record 触发，也不把它解释成 idle 原因。若 Nsight 在同一 DB 里复用了
`correlationId`，TraceLoom 只会在该 ID 已选出的候选内，用唯一的 interval
containment 消歧；结果标成 `supported_deterministic`，被排除的候选也仍保留。

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

如果目标是比较同一个结构位置在各 occurrence 后面的 host 行为，不必自己把 node、
anchor 和 interval 重新连接：

```sql
SELECT occurrence_idx, anchor_order, interval_id, support_state,
       host_interval_us, left_endpoint_count, right_endpoint_count
FROM traceloom_v_node_host_interval
WHERE node_id = 'node-42' AND coverage_kind = 'self'
ORDER BY occurrence_idx, anchor_order;
```

先读这张 typed interval 视图，才能区分“受支持但没有观察到 call”和“缺少端点、
host 顺序不单调或证据不支持”。需要 API 分布时再查询 activity：

```sql
SELECT occurrence_idx, anchor_order, right_anchor_symbol, host_interval_us,
       api_name,
       count(*) AS observed_calls,
       round(sum(observed_overlap_us), 3) AS scheduled_overlap_us
FROM traceloom_v_node_host_activity
WHERE node_id = 'node-42' AND coverage_kind = 'self'
GROUP BY occurrence_idx, anchor_order, right_anchor_symbol,
         host_interval_us, api_name
ORDER BY occurrence_idx, anchor_order, scheduled_overlap_us DESC;
```

这个视图显式携带 `placement_semantics = 'after_anchor_interval'`。因此它描述的是
“这个结构位置之后观察到了什么”，不能被读成“这段 CPU cost 归该 node 所有”。
比较 occurrence 时应同时保留 `right_anchor_symbol` 和 `host_interval_us`：右侧结构
邻居或 interval 宽度变化，本身就是需要解释的 runtime 行为差异。
`observed_overlap_us` 已把跨界 call 裁剪到当前 interval，但 runtime calls 彼此仍
可能嵌套或重叠；其和不是 overlap-safe host busy time。

`docs/report-sql/node-host-activity.sql` 是这条路径的有界现成版本：它默认下钻
成本最高的重复 atom；把文件顶部的 selector 换成指定 `node_id` 即可检查任意
热点。不要删除 selector 后直接对整库 occurrence 做排序聚合：在大型产物上，
那会把一个交互式下钻误写成数百万关系行的全库报表。

这里的结论严格限于 profiler 可见的 runtime API；API 之间没有记录的空白不等于
CPU 没有工作。`support_state`、`cardinality` 和原始 row locator 应与结果一起读，
不要把缺失或多义关系误解为“没有 host call”。

这条查询路径只物化 anchor/runtime endpoint 关系与 typed host interval，**不会**
物化全局 `host interval × runtime call` 关系。`traceloom_runtime_call` 上的复合时间
索引和 TraceLoom 提供的 activity view / projection recipe 会在选定 interval、node
或 occurrence 后按需完成区间连接。这样既保留关系查询能力，也避免密集 runtime
profile 把一个多对多投影膨胀成巨型持久化产物。空的兼容 activity/summary 表不代表
“没有 host call”；应查询带 selector 的 view 或 `host_window_calls` recipe。

### 纵向：固定结构，比较同构 occurrence

```text
one contextual edge role
  -> all equivalent concrete edges
  -> comparable cost population
  -> outlier or skew
```

纵向关系回答“同一条结构边在不同 occurrence 或 rank 上成本如何分布”。
`edge_role_id` 定义统计总体，避免把不同上下文里的同名算子粗暴混在一起。

```sql
SELECT child_occurrence_id, parent_occurrence_idx,
       edge_order, edge_ordinal_in_role,
       round(child_total_us, 3) AS total_us,
       round(child_compute_us, 3) AS compute_us,
       round(child_comm_us, 3) AS comm_us,
       round(child_uncovered_us, 3) AS uncovered_us,
       round(child_aux_us, 3) AS aux_us
FROM traceloom_v_tree_edge_cost
WHERE edge_role_id = :edge_role_id
ORDER BY child_total_us DESC;
```

## 从粗到细定位热点

先查询 Position catalog，而不是先按算子名做全库 top-k：

```sql
SELECT position_id, parent_position_id, preorder_idx,
       label, node_type, repeat_count, occurrence_count,
       round(total_us, 3) AS total_us
FROM traceloom_v_position
ORDER BY total_us DESC, db_idx, tree_id, preorder_idx
LIMIT 30;
```

找到感兴趣的 `position_id` 后，继续查询 edge roles、某次 concrete edge stream、
等价边 population 和专业 lens。仓库里的 `docs/report-sql/*.sql` 仍提供审计与专业
关系的有界查询，
`traceloom_projection_recipe.example_sql` 说明组合路径；
`traceloom_projection_coordinate` 和 `traceloom_v_projection_continuation`
说明查询返回了哪些可继续复用的坐标、下一步可以进入哪些 recipe。
`traceloom_analysis_surface.example_sql` 说明底层 relation 入口。

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
traceloom_projection_recipe、traceloom_projection_coordinate、
traceloom_v_projection_continuation 与 traceloom_analysis_surface。从
traceloom_v_position 选择一个 position_id：
1. structure：读取 contextual edge roles，不用算子名定义等价性；
2. population：固定一个 edge_role_id，或选择某一次 concrete edge stream；
3. occurrence：从结果返回的 child_occurrence_id 选择异常实例；
4. resolution：保持折叠，或展开 direct members / events / exact replay members；
5. domain：需要时进入受支持的 host-window context；
6. lens：明确使用的成本或行为统计；
7. continuation：每次优先复用结果返回的 coordinate，再决定下一步投影；
8. audit：把关键 event/runtime call 解析到内嵌 profiler 表和 row，或者停在明确的 typed boundary。
输出所用 SQL、scope、population、resolution、domain、lens 和证据边界。
不要根据名字推断 workload phase 或因果关系。
```

做 A/B 时，先用稳定 Position 路径和 contextual edge role 对齐结构，再比较
Occurrence population；不要先按算子名全局汇总。SQL 与结果表应和 `analysis.db`
的 SHA-256 一起保留，
让诊断可复核。

## Markdown 和外围导出器的位置

`--loop-tree-out` 仍可生成面向人的 Markdown 投影；外围调试导出器则直接读取
自包含的 analysis DB。它们都不是另一套产品，也不是分析前置条件。网页图、
论文图、Markdown 报告和调试快照都应当由 queryable DB timeline 的查询结果
确定性生成。
