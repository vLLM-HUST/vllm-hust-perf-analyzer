# TraceLoom

TraceLoom 是一个离线性能分析工具。它不负责安装 CANN、启动 vLLM、管理 Docker 或复现实验环境；它做一件事：读取已经采集好的 Ascend/CANN `msprof` 产物，把 profiler 里的事件整理成更容易理解和查询的性能地图。

面向用户：

- 做分布式推理、通信优化、算子优化的老师和同学。
- 已经有可运行的 Ascend 环境和 `msprof` 产物。
- 想快速知道热点循环在哪里、时间花在哪里，并能继续用 SQL 深挖。

## 安装

在仓库的 `traceloom/` 目录下开发模式安装：

```bash
python3 -m pip install -e .
```

安装后命令行入口是：

```bash
traceloom --help
```

不安装也可以本地运行：

```bash
PYTHONPATH="$PWD" python3 -m traceloom --help
```

## 最常用命令

把 `msprof` 原始产物目录交给 TraceLoom：

```bash
traceloom analyze /path/to/msprof_raw
```

输入目录可以是：

```text
<run_dir>/msprof_raw/PROF_*/msprof_*.db
<raw_dir>/PROF_*/msprof_*.db
```

默认情况下，TraceLoom 会把分析结果写回原始 profiler 目录：

```text
<raw_dir>/traceloom/
```

如果想写到别的目录：

```bash
traceloom analyze /path/to/msprof_raw --out-dir /path/to/analysis
```

如果只想分析部分卡：

```bash
traceloom analyze /path/to/msprof_raw --devices 3,4,5,6
```

## 输出产物

默认输出是一个小而清楚的 bundle：

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

核心产物有三类。

第一类是增强后的 SQLite DB：

- `dbNN.traceloom_augmented.db`
- 原始 `msprof` 表保留不动。
- TraceLoom 额外加入 `traceloom_*` 表和视图。
- 用户后续分析主要基于这些 DB 做 SQL 查询。

第二类是可读地图：

- `tree-map.md`
- 这是用户第一眼应该看的文件。
- 它把 profiler 事件整理成树状节点，突出热点循环和重复结构。

第三类是查询脚本：

- `queries/*.sql`
- 用来从某个 node 继续查事件、展开 occurrence、查看成本构成。

## 怎么读 `tree-map.md`

`tree-map.md` 是 TraceLoom 给用户的“性能地图”。表格列尽量保持少而有用：

| 列 | 含义 |
| --- | --- |
| `node` | 节点编号，例如 `N008`。后续 SQL 查询可以用它定位。 |
| `label` | 节点标签，通常是算子名、通信名或 `Repeat xN`。 |
| `depth` | 树深度。数字越大，越靠近内部循环或具体算子。 |
| `occ` | 这个 tree node 出现了多少次。`Repeat x47` 本身可能 `occ=1`，它的 body 节点可能 `occ=47`。 |
| `avg_total_us` | 每次出现的平均总成本。 |
| `avg_aux_us` | 归因到这个节点的辅助/前置成本，例如等待、准备、runtime 调用。 |
| `total_us` | 这个节点所有出现次数加起来的总成本。 |

典型读法：

1. 先看 `summary.md`，确认分析了哪些 DB、哪些 device、总事件规模是否合理。
2. 打开 `tree-map.md`，从 `total_us` 高的节点往下看。
3. 找到感兴趣的 `node`，例如 `N060`。
4. 用 `queries/node-events.sql` 或 `queries/node-cost-breakdown.sql` 继续查原始事件和成本组成。

更完整的阅读方法、真实表格示例、anchor/aux 成本模型和 agent 辅助分析建议见
[docs/tree-map-guide.zh.md](docs/tree-map-guide.zh.md)。

## 用 SQL 深挖

TraceLoom 的设计目标不是把所有细节塞进 Markdown 表，而是给出一张地图，然后让用户用 SQL 继续追。

运行内置 SQL：

```bash
cp /path/to/msprof_raw/traceloom/queries/node-events.sql /tmp/node-events.sql
# 把 /tmp/node-events.sql 里的 N027 改成你要查询的节点，例如 N060
traceloom report /path/to/msprof_raw/traceloom/db01.traceloom_augmented.db \
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

- `traceloom_v_tree_node`：树节点地图。适合做热点排序和节点筛选。
- `traceloom_tree_node_occurrence`：每个 node 的展开出现次数。
- `traceloom_tree_node_anchor`：node occurrence 到 anchor 事件的链接。
- `traceloom_anchor` / `traceloom_event`：更底层的语义事件和 profiler 事件。

常用 SQL 脚本：

- `queries/tree-map.sql`：生成 `tree-map.md` 的 SQL 版本。
- `queries/node-events.sql`：查某个 node 覆盖的具体事件。
- `queries/node-occurrences.sql`：展开某个 node 的所有 occurrence。
- `queries/node-cost-breakdown.sql`：查看 compute、communication、idle、aux 等成本构成。

## 推荐工作流

```text
1. 用户自己运行 workload，并用 msprof 采集。
2. TraceLoom analyze 读取 msprof_raw。
3. 用户先看 summary.md 和 tree-map.md。
4. 找到感兴趣的 node。
5. 用 queries/*.sql 继续追具体事件、区间和成本构成。
6. 对比两个实验时，按 tree path、label、repeat/occ 结构匹配热点节点。
```

例如 copy vs gather、优化前 vs 优化后、不同通信策略之间的比较，都可以先从 `tree-map.md` 找同形状热点循环，再进入 DB 查更细的事件。

## TraceLoom 不做什么

TraceLoom 不是 runtime 管理器：

- 不安装驱动、CANN、torch、vLLM。
- 不保证 workload 能复现。
- 不提交大型 profiler DB。
- 不把所有分析结论写死在一个报告里。

TraceLoom 的职责是把已有 profiler 产物变成可读、可查、可比较的分析产物。

## 开发检查

修改代码后可以运行：

```bash
python3 -m compileall traceloom reproduce
```

本地快速分析脚本：

```bash
scripts/traceloom-analyze.sh /path/to/msprof_raw
```

## License

TraceLoom 使用 MIT License。
