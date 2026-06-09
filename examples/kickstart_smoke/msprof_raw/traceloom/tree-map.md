# TraceLoom Tree Map

This file is the readable map for SQL drill-down. Use `node` values such as `N027` with
`traceloom_v_tree_node.local_node_id`, then join through occurrence, anchor, and event views.

## SQL Drill Down

```sql
-- Read or filter the map.
select *
from traceloom_v_tree_node
where local_node_id = 'N027';

-- Expand a node into repeated occurrences.
select *
from traceloom_tree_node_occurrence
where local_node_id = 'N027'
order by occurrence_idx;

-- Drill from a node to concrete profiler events.
select
  a.anchor_idx, e.label, e.stream_id, e.start_ns, e.end_ns, e.dur_us
from traceloom_tree_node_anchor na
join traceloom_anchor a on a.anchor_id = na.anchor_id
join traceloom_event e on e.event_id = a.event_id
where na.local_node_id = 'N027'
order by na.occurrence_idx, na.anchor_order;
```

## db01 device 1

- augmented_db: `db01.traceloom_augmented.db`

| node | label | depth | occ | avg_total_us | avg_aux_us | total_us |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| N001 | Seq[1] | 0 | 1 | 2730453.24 | 85.9 | 2730453.24 |
| N002 | Repeat x8 | 1 | 1 | 2730453.24 | 85.9 | 2730453.24 |
| N003 | aclnnMatmul_MatMulCommon_MatMulV2 | 2 | 8 | 50092.835 | 7.22 | 400742.68 |
| N004 | hcom_allReduce__#_#_# | 2 | 8 | 291213.82 | 3.518 | 2329710.56 |

## db02 device 0

- augmented_db: `db02.traceloom_augmented.db`

| node | label | depth | occ | avg_total_us | avg_aux_us | total_us |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| N001 | Seq[1] | 0 | 1 | 2600142.509 | 86.085 | 2600142.509 |
| N002 | Repeat x8 | 1 | 1 | 2600142.509 | 86.085 | 2600142.509 |
| N003 | aclnnMatmul_MatMulCommon_MatMulV2 | 2 | 8 | 41995.161 | 7.13 | 335961.292 |
| N004 | hcom_allReduce__#_#_# | 2 | 8 | 283022.652 | 3.63 | 2264181.217 |
