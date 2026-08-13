---
marp: true
theme: default
paginate: true
---

# TraceLoom 是什么

TraceLoom 是一个离线 profiler **view analyzer**。

输入：

```text
Ascend/CANN、CUDA/Nsight 等 profiler SQLite
```

一等输出：

```text
queryable DB timeline (`analysis.db`)
```

它不管理 workload，也不猜模型 phase；它把已有性能证据变成可读、可查、可比较
且能回到原始行的运行时结构。

---

# 为什么是 DB timeline

原始 timeline 有细节，却需要反复缩放和人工维持上下文；top-k 有统计，却丢失
执行位置。TraceLoom 把二者之间缺少的结构直接 materialize 到数据库：

1. 规范化算子、通信、等待和保留的未知事件；
2. 恢复重复、嵌套与 replay 内部结构；
3. 把 hierarchy、occurrence、position、cost 和 provenance 放进同一条可查询
   timeline。

Markdown、JSON 和图只是它的投影，不是第二个产品。

---

# 两个阅读方向

**横向下钻**

```text
structure -> occurrence -> event -> embedded raw row
```

**纵向比较**

```text
same recovered structure -> equivalent occurrences -> cost distribution
```

结构定义统计总体；provenance 保证结论可审计。

---

# 怎么用

```bash
traceloom /path/to/msprof_raw
sqlite3 -readonly /path/to/msprof_raw/traceloom/analysis_db01.db
```

```sql
SELECT * FROM traceloom_projection_recipe ORDER BY display_order;
.read examples/db-timeline-tour/tour.sql
```

先选择结构 scope 与投影方式；需要查底层 relation 时，再读
`traceloom_analysis_surface`。同一个 scope 可以切 occurrence population、层级、
device/host context、graph member 和 raw-row audit。
