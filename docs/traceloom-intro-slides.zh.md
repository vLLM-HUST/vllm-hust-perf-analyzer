---
marp: true
theme: default
paginate: true
---

# TraceLoom 是什么

TraceLoom 是一个离线 profiler 产物分析工具。

输入：

```text
Ascend/CANN msprof_raw
```

输出：

```text
增强 SQLite DB + 可读 tree-map + SQL 查询脚本
```

定位：不管理环境，不负责跑通 workload，只负责把已有性能产物变成可读、可查、可比较的证据。

---

# 它解决什么问题

原始 profiler 产物信息很多，但用户经常不知道从哪里看。

TraceLoom 做三件事：

1. 把算子、通信、等待整理成语义 anchor。
2. 从 timeline 里恢复循环和重复结构。
3. 生成一张 `tree-map.md`，再配一个增强 DB 让用户继续 SQL 深挖。

用户先看：

```text
summary.md
tree-map.md
```

再查：

```text
queries/node-events.sql
queries/node-cost-breakdown.sql
```

---

# 怎么用

开发模式安装：

```bash
cd traceloom
python3 -m pip install -e .
```

分析已有 `msprof` 产物：

```bash
traceloom analyze /path/to/msprof_raw
```

默认输出：

```text
/path/to/msprof_raw/traceloom/
  tree-map.md
  summary.md
  db01.traceloom_augmented.db
  queries/*.sql
```

推荐读法：从 `tree-map.md` 复制 node id，例如 `N060`，再用 SQL 查询对应区间里的事件和成本构成。
