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

## db01 device 0

- augmented_db: `db01.traceloom_augmented.db`

| node | label | depth | occ | avg_total_us | avg_aux_us | total_us |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| N001 | Seq[21] | 0 | 1 | 17128700.824 | 197334.833 | 17128700.824 |
| N002 | aclnnBatchMatMul_BatchMatMulNd_BatchMatMulV2 | 1 | 1 | 349606.025 | 31.82 | 349606.025 |
| N003 | aclnnBatchMatMul_BatchMatMulNd_BatchMatMulV2 | 1 | 1 | 300.086 | 48.222 | 300.086 |
| N004 | aclnnBatchMatMul_BatchMatMulNd_BatchMatMulV2 | 1 | 1 | 53.581 | 25.58 | 53.581 |
| N005 | aclnnBatchMatMul_BatchMatMulNd_BatchMatMulV2 | 1 | 1 | 39.141 | 37.203 | 39.141 |
| N006 | AddRmsNorm | 1 | 1 | 874672.076 | 799.034 | 874672.076 |
| N007 | AddRmsNorm | 1 | 1 | 8175.723 | 24.281 | 8175.723 |
| N008 | AddRmsNormBias | 1 | 1 | 16619.932 | 23.501 | 16619.932 |
| N009 | AddRmsNormBias | 1 | 1 | 8895.038 | 23.601 | 8895.038 |
| N010 | AddRmsNorm | 1 | 1 | 8101.022 | 20.02 | 8101.022 |
| N011 | AddRmsNorm | 1 | 1 | 8434.289 | 19.32 | 8434.289 |
| N012 | AddRmsNormBias | 1 | 1 | 8726.234 | 23.36 | 8726.234 |
| N013 | AddRmsNormBias | 1 | 1 | 8402.268 | 23.6 | 8402.268 |
| N014 | Repeat x36 | 1 | 1 | 15239862.744 | 99195.676 | 15239862.744 |
| N015 | hcom_allReduce__#_#_# | 2 | 36 | 65542.703 | 1370.836 | 2359537.309 |
| N016 | RmsNorm | 2 | 36 | 75.024 | 0.0 | 2700.876 |
| N017 | Repeat x24 | 2 | 36 | 357711.793 | 1384.6 | 12877624.559 |
| N018 | aclnnAddmm_MatMulCommon_MatMulV2 | 3 | 864 | 29.6 | 1.385 | 25574.261 |
| N019 | AtbRopeKernel | 3 | 864 | 348.779 | 41.325 | 301345.037 |
| N020 | aclnnMm_MatMulCommon_MatMulV2 | 3 | 864 | 11566.916 | 4.366 | 9993815.485 |
| N021 | hcom_allReduce__#_#_# | 3 | 864 | 2332.668 | 2.021 | 2015425.221 |
| N022 | AddRmsNormBias | 3 | 864 | 60.133 | 3.276 | 51954.891 |
| N023 | aclnnMm_MatMulCommon_MatMulV2 | 3 | 864 | 67.554 | 0.0 | 58367.074 |
| N024 | SwiGlu | 3 | 864 | 39.038 | 0.0 | 33729.049 |
| N025 | aclnnMm_MatMulCommon_MatMulV2 | 3 | 864 | 55.428 | 0.0 | 47889.816 |
| N026 | hcom_allReduce__#_#_# | 3 | 864 | 348.509 | 2.098 | 301112.128 |
| N027 | AddRmsNormBias | 3 | 864 | 56.032 | 3.222 | 48411.597 |
| N028 | PpMatmulAccumAtomicKernel | 1 | 1 | 581388.116 | 111.685 | 581388.116 |
| N029 | Repeat x9 | 1 | 1 | 14736.258 | 93314.539 | 14736.258 |
| N030 | aiv_all_reduce_bfloat16_t | 2 | 9 | 6.591 | 775.487 | 59.32 |
| N031 | RmsNorm | 2 | 9 | 6.725 | 0.0 | 60.521 |
| N032 | Repeat x24 | 2 | 9 | 1624.046 | 9592.795 | 14616.417 |
| N033 | aclnnAddmm_MatMulCommon_MatMulV2 | 3 | 216 | 9.903 | 1.393 | 2139.078 |
| N034 | AtbRopeKernel | 3 | 216 | 4.347 | 9.302 | 938.857 |
| N035 | aclnnMm_MatMulCommon_MatMulV2 | 3 | 216 | 9.419 | 52.602 | 2034.423 |
| N036 | aiv_all_reduce_bfloat16_t | 3 | 216 | 5.917 | 333.581 | 1278.026 |
| N037 | AddRmsNormBias | 3 | 216 | 2.635 | 1.123 | 569.111 |
| N038 | aclnnMm_MatMulCommon_MatMulV2 | 3 | 216 | 12.049 | 0.0 | 2602.536 |
| N039 | SwiGlu | 3 | 216 | 3.711 | 0.0 | 801.631 |
| N040 | aclnnMm_MatMulCommon_MatMulV2 | 3 | 216 | 9.92 | 0.0 | 2142.747 |
| N041 | aiv_all_reduce_bfloat16_t | 3 | 216 | 7.261 | 0.561 | 1568.336 |
| N042 | AddRmsNormBias | 3 | 216 | 2.508 | 1.139 | 541.672 |
| N043 | aiv_all_reduce_bfloat16_t | 1 | 1 | 5.88 | 269.426 | 5.88 |
| N044 | RmsNorm | 1 | 1 | 5.9 | 0.0 | 5.9 |
| N045 | Repeat x10 | 1 | 1 | 653.911 | 3292.865 | 653.911 |
| N046 | aclnnAddmm_MatMulCommon_MatMulV2 | 2 | 10 | 9.814 | 1.382 | 98.142 |
| N047 | AtbRopeKernel | 2 | 10 | 3.726 | 7.932 | 37.26 |
| N048 | aclnnMm_MatMulCommon_MatMulV2 | 2 | 10 | 9.468 | 38.206 | 94.68 |
| N049 | aiv_all_reduce_bfloat16_t | 2 | 10 | 5.68 | 278.998 | 56.801 |
| N050 | AddRmsNormBias | 2 | 10 | 2.15 | 1.102 | 21.5 |
| N051 | aclnnMm_MatMulCommon_MatMulV2 | 2 | 10 | 11.888 | 0.0 | 118.884 |
| N052 | SwiGlu | 2 | 10 | 3.38 | 0.0 | 33.8 |
| N053 | aclnnMm_MatMulCommon_MatMulV2 | 2 | 10 | 9.908 | 0.0 | 99.082 |
| N054 | aiv_all_reduce_bfloat16_t | 2 | 10 | 7.242 | 0.546 | 72.422 |
| N055 | AddRmsNormBias | 2 | 10 | 2.134 | 1.12 | 21.34 |
| N056 | aclnnAddmm_MatMulCommon_MatMulV2 | 1 | 1 | 9.66 | 1.4 | 9.66 |
| N057 | AtbRopeKernel | 1 | 1 | 3.64 | 8.3 | 3.64 |
| N058 | aclnnMm_MatMulCommon_MatMulV2 | 1 | 1 | 9.3 | 41.4 | 9.3 |

## db02 device 1

- augmented_db: `db02.traceloom_augmented.db`

| node | label | depth | occ | avg_total_us | avg_aux_us | total_us |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| N001 | Seq[17] | 0 | 1 | 16869784.06 | 132567.02 | 16869784.06 |
| N002 | AddRmsNorm | 1 | 1 | 852299.38 | 947.86 | 852299.38 |
| N003 | AddRmsNorm | 1 | 1 | 7605.32 | 22.74 | 7605.32 |
| N004 | AddRmsNormBias | 1 | 1 | 15765.9 | 23.04 | 15765.9 |
| N005 | AddRmsNormBias | 1 | 1 | 8682.42 | 23.1 | 8682.42 |
| N006 | AddRmsNorm | 1 | 1 | 8248.1 | 19.48 | 8248.1 |
| N007 | AddRmsNorm | 1 | 1 | 7860.26 | 18.92 | 7860.26 |
| N008 | AddRmsNormBias | 1 | 1 | 8553.54 | 22.26 | 8553.54 |
| N009 | AddRmsNormBias | 1 | 1 | 8341.36 | 23.22 | 8341.36 |
| N010 | Repeat x36 | 1 | 1 | 15353997.88 | 98027.46 | 15353997.88 |
| N011 | hcom_allReduce__#_#_# | 2 | 36 | 67603.537 | 1372.059 | 2433727.32 |
| N012 | RmsNorm | 2 | 36 | 132.782 | 0.0 | 4780.16 |
| N013 | Repeat x24 | 2 | 36 | 358763.622 | 1350.926 | 12915490.4 |
| N014 | aclnnAddmm_MatMulCommon_MatMulV2 | 3 | 864 | 30.931 | 1.386 | 26724.08 |
| N015 | AtbRopeKernel | 3 | 864 | 325.25 | 40.336 | 281015.7 |
| N016 | aclnnMm_MatMulCommon_MatMulV2 | 3 | 864 | 12648.269 | 4.171 | 10928104.32 |
| N017 | hcom_allReduce__#_#_# | 3 | 864 | 1329.843 | 1.898 | 1148983.96 |
| N018 | AddRmsNormBias | 3 | 864 | 51.308 | 3.281 | 44330.46 |
| N019 | aclnnMm_MatMulCommon_MatMulV2 | 3 | 864 | 72.132 | 0.0 | 62321.8 |
| N020 | SwiGlu | 3 | 864 | 40.32 | 0.0 | 34836.5 |
| N021 | aclnnMm_MatMulCommon_MatMulV2 | 3 | 864 | 54.576 | 0.0 | 47153.9 |
| N022 | hcom_allReduce__#_#_# | 3 | 864 | 347.283 | 1.981 | 300052.12 |
| N023 | AddRmsNormBias | 3 | 864 | 48.574 | 3.236 | 41967.56 |
| N024 | PpMatmulAccumAtomicKernel | 1 | 1 | 586146.04 | 112.0 | 586146.04 |
| N025 | Repeat x7 | 1 | 1 | 11653.88 | 25867.22 | 11653.88 |
| N026 | aiv_all_reduce_bfloat16_t | 2 | 7 | 7.129 | 370.049 | 49.9 |
| N027 | RmsNorm | 2 | 7 | 6.683 | 0.0 | 46.78 |
| N028 | Repeat x24 | 2 | 7 | 1651.029 | 3325.269 | 11557.2 |
| N029 | aclnnAddmm_MatMulCommon_MatMulV2 | 3 | 168 | 9.844 | 1.383 | 1653.72 |
| N030 | AtbRopeKernel | 3 | 168 | 4.608 | 9.524 | 774.14 |
| N031 | aclnnMm_MatMulCommon_MatMulV2 | 3 | 168 | 9.753 | 59.472 | 1638.42 |
| N032 | aiv_all_reduce_bfloat16_t | 3 | 168 | 6.369 | 65.319 | 1070.06 |
| N033 | AddRmsNormBias | 3 | 168 | 2.766 | 1.135 | 464.64 |
| N034 | aclnnMm_MatMulCommon_MatMulV2 | 3 | 168 | 12.131 | 0.0 | 2038.06 |
| N035 | SwiGlu | 3 | 168 | 3.731 | 0.0 | 626.78 |
| N036 | aclnnMm_MatMulCommon_MatMulV2 | 3 | 168 | 10.059 | 0.0 | 1689.96 |
| N037 | aiv_all_reduce_bfloat16_t | 3 | 168 | 6.832 | 0.564 | 1147.84 |
| N038 | AddRmsNormBias | 3 | 168 | 2.7 | 1.156 | 453.58 |
| N039 | aiv_all_reduce_bfloat16_t | 1 | 1 | 7.24 | 322.62 | 7.24 |
| N040 | RmsNorm | 1 | 1 | 6.32 | 0.0 | 6.32 |
| N041 | Repeat x9 | 1 | 1 | 593.22 | 7088.16 | 593.22 |
| N042 | aclnnAddmm_MatMulCommon_MatMulV2 | 2 | 9 | 9.853 | 1.436 | 88.68 |
| N043 | AtbRopeKernel | 2 | 9 | 3.82 | 7.844 | 34.38 |
| N044 | aclnnMm_MatMulCommon_MatMulV2 | 2 | 9 | 9.724 | 41.676 | 87.52 |
| N045 | aiv_all_reduce_bfloat16_t | 2 | 9 | 6.071 | 733.822 | 54.64 |
| N046 | AddRmsNormBias | 2 | 9 | 2.193 | 1.116 | 19.74 |
| N047 | aclnnMm_MatMulCommon_MatMulV2 | 2 | 9 | 12.218 | 0.0 | 109.96 |
| N048 | SwiGlu | 2 | 9 | 3.373 | 0.0 | 30.36 |
| N049 | aclnnMm_MatMulCommon_MatMulV2 | 2 | 9 | 10.182 | 0.0 | 91.64 |
| N050 | aiv_all_reduce_bfloat16_t | 2 | 9 | 6.296 | 0.544 | 56.66 |
| N051 | AddRmsNormBias | 2 | 9 | 2.182 | 1.136 | 19.64 |
| N052 | aclnnAddmm_MatMulCommon_MatMulV2 | 1 | 1 | 9.62 | 1.6 | 9.62 |
| N053 | AtbRopeKernel | 1 | 1 | 3.76 | 7.42 | 3.76 |
| N054 | aclnnMm_MatMulCommon_MatMulV2 | 1 | 1 | 9.82 | 39.92 | 9.82 |
