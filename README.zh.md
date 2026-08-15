# TraceLoom

[English](README.md)

TraceLoom 是一个原生 C++17 离线性能分析器。它把原始 profiler 数据库
转换为自包含的 **queryable DB timeline（可查询数据库时间线）**：从粗到细的执行结构、成本分布、有证据支撑的
精确 replay 内部结构，以及返回内嵌原始行的链接。

![TraceLoom queryable DB timeline：横向证据下钻与纵向同构比较](docs/assets/queryable-db-timeline.svg)

每次分析先选择一个结构 scope，再组合需要的投影视图：选择某一次 occurrence
阅读真实行为，或放开 occurrence 得到统计总体；保持高层结构折叠，或展开 children
与 events；进入有证据支持的 host window，或切换兼容的成本 lens，而不重新构造
边界。横向下钻、纵向比较、层级导航和 cross-domain context 都是同一组结构坐标上
的投影。完整说明见
[可组合分析投影](docs/composable-analytical-projections.md)。

可以直接用仓库内的真实 profile 试玩
[`60 秒 DB timeline tour`](examples/db-timeline-tour)，不要求预先熟悉 SQL。

本仓库现在只保留原生实现。安装后的正式入口只有一个：

```bash
traceloom <profile.db-or-profile-dir>
```

## TraceLoom 做什么

```text
profiler SQLite
  -> 事件归一化与原始证据链接
  -> semantic anchor 抽取
  -> 重复模式发现
  -> 不重复计算重叠 stream 的时间线成本归因
  -> queryable DB timeline
```

核心能力：

- 自动发现 Ascend/CANN monolithic 与 split-SQLite profiler；当 monolithic
  `TASK` 不可用时自动切换 split fallback；
- 原生重建计算、HCCL、同步和 ACLGraph 执行语义；
- 在 semantic anchor 序列上发现重复的 decode/layer 结构；
- 对并发 stream 使用不重复计算的 wall-clock 统计；
- Repeat 节点的平均值按循环体迭代次数归一化；
- 一等的分析投影 recipe：自由组合结构 scope、occurrence population、层级深度、
  observation domain 与 measure lens；
- 用 provider correlation 把 host runtime call 显式关联回 device anchor、
  graph launch 与 auxiliary work，并保留一对多、歧义和未匹配结果；
- 查询相邻 device anchor 所关联 host endpoint 之间实际被 profiler 观察到的
  runtime 调用，而不把这些观测擅自解释成 idle 原因；
- 保留到原始数据库、表和行的 provenance。

## 在 Debian 或 Ubuntu 上安装

从仓库根目录构建 `traceloom-native` Debian 包：

```bash
cmake -S native -B build/traceloom-native-package \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF
cmake --build build/traceloom-native-package -j "$(nproc)"
cpack --config build/traceloom-native-package/CPackConfig.cmake \
  -B build/traceloom-native-package
sudo apt install ./build/traceloom-native-package/traceloom-native_*.deb
```

安装包只会安装 `/usr/bin/traceloom`。验证安装：

```bash
traceloom --version
traceloom --help
```

运行时依赖为 `libc6`、`libstdc++6` 和 `libsqlite3-0`。

卸载：

```bash
sudo apt remove traceloom-native
```

## 从源码安装

```bash
cmake --preset dev
cmake --build --preset dev -j "$(nproc)"
cmake --install build/native --prefix "$HOME/.local"
```

确认 `$HOME/.local/bin` 位于 `PATH` 中，然后运行 `traceloom --version`。

## 快速教程

### 1. 分析单张数据库

```bash
traceloom /path/to/PROF_.../msprof_YYYYMMDDHHMMSS.db
# 或华为官方 torch-npu profiler 产物：
traceloom /path/to/ASCEND_PROFILER_OUTPUT/ascend_pytorch_profiler_0.db
```

默认会在源数据库旁边生成一等产品——自包含的 queryable DB timeline：

```text
/path/to/PROF_.../traceloom/analysis.db
```

### 2. 分析 profiler 目录

```bash
traceloom /path/to/msprof_output
```

TraceLoom 会按兼容的 `TASK` schema 发现 monolithic Ascend profiler DB，
包括 `PROF_*/msprof_*.db` 和华为官方 torch-npu
`ASCEND_PROFILER_OUTPUT/ascend_pytorch_profiler_*.db`，也支持 split
`PROF_*/{host,device_*}/sqlite/*.db`。它会为每个发现到的分析输入生成数据库：

```text
/path/to/msprof_output/traceloom/analysis_db01.db
/path/to/msprof_output/traceloom/analysis_db02.db
```

对于大型 trace，可以显式设置并行度：

```bash
traceloom /path/to/msprof_output --threads 48
```

同一个 `PROF_*` 中，非空的 monolithic `TASK` 表优先；如果它不存在或不可用，
TraceLoom 会给出 warning，并从 split `AscendTask`、`TaskInfo`、`HostTask`、
`ApiData` 构建基础时间线。split 的细粒度通信、Graph Replay 和 PMU 归因仍按
增量阶段继续完善。

split `PROF_*` 的多张原始 SQLite 会用无冲突表名打包进同一份可移动
artifact；普通数据库则完整快照原始 schema。

### 3. 直接阅读 DB timeline

```bash
sqlite3 /path/to/traceloom/analysis.db \
  'SELECT surface_name, relation_name, purpose FROM traceloom_analysis_surface;'
sqlite3 -header -column /path/to/traceloom/analysis.db \
  'SELECT local_node_id, label, depth, occurrence_count, avg_total_us FROM traceloom_v_tree_node ORDER BY display_order;'
```

先选择最外层的 `Repeat xN` 或其他结构 scope。数据库自己携带可以复用的投影
recipe：

```sql
SELECT projection_name, population_mode, resolution,
       observation_domain, measure_lens, selector_parameters, purpose
FROM traceloom_projection_recipe
ORDER BY display_order;
```

`traceloom_projection_parameter` 还会以规范化关系给出每个 selector 的类型、
可空性、坐标类型、用途，以及候选坐标来自哪张 relation/column。
`traceloom_projection_coordinate` 列出每个 recipe 返回的可复用坐标；
`traceloom_v_projection_continuation` 则直接说明这些返回列能够喂给哪些下一步
recipe：

```sql
SELECT source_column, target_projection, target_parameter
FROM traceloom_v_projection_continuation
WHERE source_projection = 'position_population'
ORDER BY target_projection, source_column;
```

因此 agent 可以先看总体、再选异常 occurrence、继续展开并走到原始证据，
不必通过解析 SQL 文本猜测哪些 identifier 还能继续复用。

在 `sqlite3` 中只绑定一次 scope，就能在不同投影之间复用：

```sql
.parameter init
.parameter set :node_id 'node-N006'
.parameter set :occurrence_idx NULL
```

`:occurrence_idx = NULL` 表示查看全部 occurrence 的总体；换成数字则查看某一次
真实执行。同一个 `:node_id` 可以保持折叠、展开 children/events、进入带类型的
host windows，或切换成本 lens。缺少端点或 host 顺序不单调的 interval 仍会作为
明确状态返回，而不会因为无法形成 API 分布就静默消失。用户框选的 device window 也可以作为查询 scope，
但框选本身不会把它提升成 recovered pattern。详见
[可组合分析投影](docs/composable-analytical-projections.md)。

最常用的成本列是：

- `total_us`：互不相交的 wall-clock 区间并集，stream 重叠不会重复计算；
- `avg_total_us`：普通节点按 occurrence 平均，Repeat 节点按循环体迭代平均；
- `avg_compute_us`、`avg_comm_us`、`avg_idle_us`：可以直接比较的平均成本；
- `avg_aux_us`、`avg_self_us`：归因到节点的辅助成本和节点自身成本。

#### 审计稀疏 event reconciliation

部分 profiler schema 会用两条记录描述同一个 device 动作：一条提供完整时间包络，
另一条提供具体算子身份。TraceLoom 只会归并规则明确支持且能够唯一配对的记录；
无法配对、有多个候选或证据冲突时，原始候选继续作为彼此独立的事件进入时间线。

不要从一张大 join 表盲看全部行。推荐按下面的审计链逐层下钻：

| 关系 | 回答的问题 |
| --- | --- |
| `traceloom_event_reconciliation_policy` | 这份 `analysis.db` 实际使用了哪个 policy 和 digest？ |
| `traceloom_event_reconciliation_rule` | 哪些 provider 证据和阈值允许归并？规则来自哪里？ |
| `traceloom_event_reconciliation_decision` | 哪些候选组被归并、保持独立、判为多义或冲突？ |
| `traceloom_event_reconciliation_member` | 每条原始 observation 分别贡献 timing、symbol 还是 cost？ |
| `traceloom_v_event_reconciliation` | 把 member、decision、规范化 event 与 canonical anchor 一次连起来。 |

先看结果分布，再选一条决定下钻：

```sql
SELECT status, reason_code, count(*) AS decision_count
FROM traceloom_event_reconciliation_decision
GROUP BY status, reason_code
ORDER BY status, reason_code;

SELECT decision_id, status, reason_code, event_id, member_role,
       contributes_timing, contributes_symbol, contributes_cost,
       canonical_anchor_id, observed_symbol, canonical_symbol,
       source_table, source_key
FROM traceloom_v_event_reconciliation
WHERE decision_id = 'reconciliation-decision-0'
ORDER BY member_order;
```

默认规则位于
[`native/data/default_event_reconciliation_rules.tsv`](native/data/default_event_reconciliation_rules.tsv)。
`--event-reconciliation-rules PATH` 完整替换默认策略；
`--extend-event-reconciliation-rules PATH` 按稳定 `rule_id` 叠加规则，同名 ID 覆盖
默认项。无论采用哪种方式，effective policy、规则来源、每次决定和成员贡献都会写回
产物。完整的 policy → rule → decision → member 使用流程见
[event reconciliation 审计指南](docs/event-reconciliation-audit.md)。

#### 推荐的 host/device 分析路径

不要从 provider 原始表起步。先在恢复结构中选一个高成本或重复 node，再带着
occurrence 与 anchor 坐标进入 runtime/device 证据：

```sql
SELECT node_id, local_node_id, label, occurrence_count, avg_total_us
FROM traceloom_v_tree_node
WHERE occurrence_count > 1
ORDER BY total_us DESC
LIMIT 20;

SELECT occurrence_idx, anchor_order, anchor_idx, api_name, device_symbol,
       match_policy, support_state, cardinality
FROM traceloom_v_node_runtime_call
WHERE node_id = 'node-N006' AND coverage_kind = 'self'
ORDER BY occurrence_idx, anchor_order, runtime_start_ns;

SELECT occurrence_idx, anchor_order, right_anchor_symbol, host_interval_us,
       api_name,
       count(*) AS observed_calls,
       round(sum(observed_overlap_us), 3) AS scheduled_overlap_us
FROM traceloom_v_node_host_activity
WHERE node_id = 'node-N006' AND coverage_kind = 'self'
GROUP BY occurrence_idx, anchor_order, right_anchor_symbol,
         host_interval_us, api_name
ORDER BY occurrence_idx, anchor_order, scheduled_overlap_us DESC;
```

把 `node-N006` 换成第一条查询返回的 `node_id`。第一条 runtime 视图报告有
provider 证据的提交/关联关系；第二条报告每个 node-owned anchor **之后**的 host
区间里 profiler 可见的 runtime calls。后者是结构上下文，不是归属于该 node 的
CPU 成本，也不是 idle 因果。审计时保留 `support_state`、`cardinality` 与原始
source key。仓库还提供了有界投影
`docs/report-sql/node-host-activity.sql`；它默认选择成本最高的重复 atom，并说明
如何替换成指定 `node_id`。runtime calls 彼此可能嵌套或重叠；即使
按 interval 裁剪后求和，得到的也只是 scheduled-call time，不是 overlap-safe
的 host busy union。

体验上图完整的可组合投影路径：

```bash
sqlite3 -readonly \
  examples/kickstart_smoke/msprof_raw/traceloom/analysis_db01.db
```

然后在 `sqlite>` 提示符后运行：

```sql
.read examples/db-timeline-tour/tour.sql
```

### 4. 生成给人阅读的投影或调试证据

Markdown 不再是默认产物；需要时显式导出。`--output` 设置一等数据库路径：

```bash
traceloom /path/to/msprof.db \
  --loop-tree-out /tmp/loop_tree_v2.md \
  --output /tmp/traceloom-analysis.db
```

外围调试导出器应读取这个自包含分析数据库；生产 CLI 不再维护第二套内存 JSON
模型。

运行 `traceloom --help-advanced` 可以查看 grammar diagnostics 和辅助归因
materialization 选项。

## 仓库内置 Kickstart Profile

仓库在 [`examples/kickstart_smoke`](examples/kickstart_smoke) 中提供了一份真实的
双卡 vLLM-Ascend profile：

```bash
traceloom examples/kickstart_smoke/msprof_raw
```

这份 capture 包含超过 118 万行选定 profiler 记录。TraceLoom 将它们压缩成
112 个结构节点，并在两张设备上恢复出相同的 `Repeat x36 -> Repeat x24`
transformer 嵌套结构。

## 开发检查

```bash
cmake --preset dev-tests
cmake --build --preset dev-tests -j "$(nproc)"
ctest --preset dev-tests
```

部分深度回归测试依赖研究工作区的外部 fixture。独立 clone 缺少这些 fixture 时会
跳过对应测试，普通 native unit tests 仍然会运行。

## 文档

- [原生分析器与打包指南](native/README.md)
- [分步工作流](docs/workflow.md)
- [输入布局](docs/input-profiles.md)
- [输出约定](docs/output-schema.md)
- [原生架构](docs/architecture.md)
- [Queryable DB timeline 阅读指南](docs/db-timeline-guide.zh.md)

## License

TraceLoom 使用 [MIT License](LICENSE)。
