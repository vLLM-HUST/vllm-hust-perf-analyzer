# Loop Tree (v2)

- db: `<redacted-ascend-profiler-package>/db01/msprof.db`
- device_id: `2`
- stream_scope: `device_compute_sequence`
- db_idx: `1`
- global_rank: `2`
- report_view: `semantic_anchor_cost_tree`

## Root

```
node                          op                 cat    |  occ   total_us    avg_us  avg_idle  avg_aux avg_self
----------------------------  -----------------  -----  | ---- ---------- --------- --------- -------- --------
N001 Seq                                                |    1   17277247  17277247  15673049    23611  1544260
  [1] N002 Rep x4                                       |    1     108432    108432    108404    150.5     27.6
    [1] N003                  MatMul             exec   |    4     108432     27108     27101     37.6     6.91
  [2] N004 Rep x34                                      |    1    1305905   1305905   1305816     1393     89.0
    [1] N005                  RmsNorm            exec   |   34    1305905     38409     38406     41.0     2.62
  [3] N006 Rep x7                                       |    1     177673    177673    177587    458.9     85.3
    [1] N007                  Rope               exec   |    7     177245     25321     25318     1.17     3.11
    [2] N008 Rep x2                                     |    7      427.1      61.0      52.0     64.4     9.07
      [1] N009                RmsNorm            exec   |   14      427.1      30.5      26.0     32.2     4.53
  [4] N010                    Rope               exec   |    1       4116      4116      4113     2.58     2.82
  [5] N011 Rep x18                                      |    1   13350903  13350903  12650256    926.5   700644
    [1] N012                  AllReduce          comm   |   18     647906     35995     35681     7.60    314.3
    [2] N013                  RmsNorm            exec   |   18      541.2      30.1      2.06     0.00     28.0
    [3] N014                  MatMul             exec   |   18      21487      1194      1164     1.54     29.4
    [4] N015                  Rope               exec   |   18       8310     461.6     380.0     11.2     81.6
    [5] N016                  MatMul             exec   |   18    6230026    346113    346087     8.84     25.4
    [6] N017                  AllReduce          comm   |   18      12249     680.5     454.6     0.00    225.9
    [7] N018                  RmsNorm            exec   |   18      604.2      33.6      1.93     0.00     31.6
    [8] N019                  MatMul             exec   |   18       3082     171.2      2.78     0.00    168.5
    [9] N020                  SwiGlu             exec   |   18      558.7      31.0      0.95     0.00     30.1
    [10] N021                 MatMul             exec   |   18      23941      1330      1221     0.00    108.9
    [11] N022                 AllReduce          comm   |   18       5116     284.2      32.8     0.00    251.4
    [12] N023                 RmsNorm            exec   |   18      536.7      29.8      1.96     0.00     27.8
    [13] N024                 MatMul             exec   |   18      531.8      29.5      0.03     1.42     29.5
    [14] N025                 Rope               exec   |   18       3757     208.7     127.3     11.0     81.4
    [15] N026                 MatMul             exec   |   18    5710261    317237    317211     9.89     25.3
    [16] N027                 AllReduce          comm   |   18     675926     37551     421.0     0.00    37130
    [17] N028                 RmsNorm            exec   |   18      598.0      33.2      1.90     0.00     31.3
    [18] N029                 MatMul             exec   |   18       3022     167.9      0.02     0.00    167.8
    [19] N030                 SwiGlu             exec   |   18      500.2      27.8      0.02     0.00     27.8
    [20] N031                 MatMul             exec   |   18       1950     108.3      0.02     0.00    108.3
  [6] N032                    AllReduce          comm   |    1      254.1     254.1      2.24     0.00    251.8
  [7] N033                    RmsNorm            exec   |    1       29.8      29.8      1.72     0.00     28.1
  [8] N034                    MatMul             exec   |    1      601.7     601.7     296.6     9.90    305.1
  [9] N035                    AllGather          comm   |    1       3645      3645      3622     0.00     22.9
  [10] N036 Rep x18                                     |    1      52415     52415     38687    17790    13724
    [1] N037                  AllReduce          comm   |   18       9652     536.2     403.3    976.5    132.8
    [2] N038                  RmsNorm            exec   |   18       1242      69.0      66.4     0.00     2.59
    [3] N039                  MatMul             exec   |   18      286.3      15.9      6.57     1.42     9.34
    [4] N040                  Rope               exec   |   18       2601     144.5     142.4     2.92     2.14
    [5] N041                  MatMul             exec   |   18       3438     191.0     183.1     1.51     7.90
    [6] N042                  AllReduce          comm   |   18       8017     445.4     295.4     0.00    150.0
    [7] N043                  RmsNorm            exec   |   18       1062      59.0      56.5     0.00     2.41
    [8] N044                  MatMul             exec   |   18       1210      67.2      25.0     0.00     42.2
    [9] N045                  SwiGlu             exec   |   18      150.4      8.36      4.64     0.00     3.71
    [10] N046                 MatMul             exec   |   18      856.2      47.6      21.4     0.00     26.2
    [11] N047                 AllReduce          comm   |   18       6200     344.5     196.6     0.00    147.8
    [12] N048                 RmsNorm            exec   |   18       1118      62.1      59.5     0.00     2.54
    [13] N049                 MatMul             exec   |   18      311.0      17.3      8.36     1.45     8.92
    [14] N050                 Rope               exec   |   18       2337     129.8     127.7     3.00     2.09
    [15] N051                 MatMul             exec   |   18       3233     179.6     172.2     1.50     7.48
    [16] N052                 AllReduce          comm   |   18       7753     430.7     289.8     0.00    140.9
    [17] N053                 RmsNorm            exec   |   18       1127      62.6      60.1     0.00     2.45
    [18] N054                 MatMul             exec   |   18       1114      61.9      21.2     0.00     40.7
    [19] N055                 SwiGlu             exec   |   18       65.8      3.65      0.02     0.00     3.63
    [20] N056                 MatMul             exec   |   18      640.3      35.6      9.05     0.00     26.5
  [11] N057                   AllReduce          comm   |    1      261.5     261.5     243.5     0.00     18.0
  [12] N058                   RmsNorm            exec   |    1      114.3     114.3     111.9     0.00     2.44
  [13] N059 Rep x18                                     |    1      55156     55156     50484    380.4     4668
    [1] N060                  AllReduce          comm   |   18       4818     267.6     249.5     15.0     18.1
    [2] N061                  RmsNorm            exec   |   18       4192     232.9     230.0     0.00     2.83
    [3] N062                  MatMul             exec   |   18      412.4      22.9      13.3     1.44     9.66
    [4] N063                  Rope               exec   |   18       9703     539.0     536.8     0.00     2.26
    [5] N064                  MatMul             exec   |   18       3279     182.2     173.7     1.60     8.44
    [6] N065                  AllReduce          comm   |   18       5444     302.4     284.5     0.00     17.9
    [7] N066                  RmsNorm            exec   |   18       2057     114.3     111.7     0.00     2.53
    [8] N067                  MatMul             exec   |   18       1494      83.0      45.5     0.00     37.5
    [9] N068                  SwiGlu             exec   |   18      194.9      10.8      7.01     0.00     3.81
    [10] N069                 MatMul             exec   |   18       1049      58.3      32.3     0.00     26.0
    [11] N070                 AllReduce          comm   |   18       4754     264.1     246.2     0.00     17.9
    [12] N071                 RmsNorm            exec   |   18       2088     116.0     112.8     0.00     3.16
    [13] N072                 MatMul             exec   |   18      253.4      14.1      4.50     1.50     9.58
    [14] N073                 Rope               exec   |   18       2815     156.4     154.1     0.00     2.28
    [15] N074                 MatMul             exec   |   18       3136     174.2     165.7     1.64     8.57
    [16] N075                 AllReduce          comm   |   18       5382     299.0     281.1     0.00     17.9
    [17] N076                 RmsNorm            exec   |   18       2098     116.6     114.0     0.00     2.47
    [18] N077                 MatMul             exec   |   18       1307      72.6      34.1     0.00     38.5
    [19] N078                 SwiGlu             exec   |   18       98.9      5.49      1.63     0.00     3.87
    [20] N079                 MatMul             exec   |   18      582.0      32.3      6.27     0.00     26.1
  [14] N080                   AllReduce          comm   |    1      329.7     329.7     311.1     0.00     18.6
  [15] N081                   RmsNorm            exec   |    1       49.5      49.5      47.3     0.00     2.22
  [16] N082 Rep x18                                     |    1    1179332   1179332   1173120     1623     6208
    [1] N083                  MatMul             exec   |   18    1131941     62886     62861     11.3     24.8
    [2] N084                  AllReduce          comm   |   18       4369     242.7     219.1     5.20     23.6
    [3] N085                  RmsNorm            exec   |   18       2159     120.0     115.6     0.00     4.31
    [4] N086                  MatMul             exec   |   18      363.6      20.2      10.3     1.46     9.88
    [5] N087                  Rope               exec   |   18       2841     157.8     153.4     4.62     4.45
    [6] N088                  Attention          exec   |   18      364.4      20.2      0.00     30.8     20.2
    [7] N089                  MatMul             exec   |   18       3564     198.0     189.0     0.63     8.97
    [8] N090                  AllReduce          comm   |   18       5867     325.9     302.2     0.00     23.7
    [9] N091                  RmsNorm            exec   |   18       2733     151.8     147.3     0.00     4.52
    [10] N092                 MatMul             exec   |   18      851.0      47.3      9.14     0.00     38.1
    [11] N093                 SwiGlu             exec   |   18      329.6      18.3      8.09     0.00     10.2
    [12] N094                 MatMul             exec   |   18      846.1      47.0      21.2     0.00     25.8
    [13] N095                 AllReduce          comm   |   18       3894     216.3     192.8     0.00     23.5
    [14] N096                 RmsNorm            exec   |   18       2481     137.8     133.7     0.00     4.10
    [15] N097                 MatMul             exec   |   18      204.1      11.3      1.43     1.52     9.91
    [16] N098                 Rope               exec   |   18       2930     162.8     158.4     4.78     4.38
    [17] N099                 Attention          exec   |   18      337.5      18.8      0.00     29.2     18.8
    [18] N100                 MatMul             exec   |   18       3710     206.1     197.5     0.65     8.65
    [19] N101                 AllReduce          comm   |   18       5696     316.5     293.0     0.00     23.4
    [20] N102                 RmsNorm            exec   |   18       2727     151.5     147.0     0.00     4.49
    [21] N103                 MatMul             exec   |   18      727.4      40.4      1.53     0.00     38.9
    [22] N104                 SwiGlu             exec   |   18      396.7      22.0      12.0     0.00     10.1
  [17] N105                   MatMul             exec   |    1       61.8      61.8      36.3     0.00     25.5
  [18] N106                   AllReduce          comm   |    1      197.4     197.4     174.4     0.00     23.0
  [19] N107                   RmsNorm            exec   |    1      158.2     158.2     153.7     0.00     4.40
  [20] N108                   MatMul             exec   |    1      284.3     284.3      60.5     8.84    223.8
  [21] N109                   AllGather          comm   |    1       80.5      80.5      56.3     0.00     24.2
  [22] N110                   ACLGraphType G001  graph  |    1      30970     30970      0.00    254.6    30970
  [23] N111                   MatMul             exec   |    1      362.0     362.0     144.3     8.82    217.7
  [24] N112                   AllGather          comm   |    1      640.4     640.4     328.5     0.00    311.9
  [25] N113                   ACLGraphType G001  graph  |    1      27684     27684      0.00    330.3    27684
  [26] N114                   MatMul             exec   |    1      372.9     372.9     153.2     8.78    219.7
  [27] N115                   AllGather          comm   |    1      254.7     254.7     230.9     0.00     23.8
  [28] N116 Rep x28                                     |    1     976962    976962    158607    264.6   758433
    [1] N117                  ACLGraphType G001  graph  |   28     976962     34891      5665     9.45    27087
```

## Macro Subtrees

No macro definitions in readable view; macro refs were inlined.

## Graph Types

| graph | occurrences | total_us | body_hash | envelope_hash | control_variants | noise_variants | controls | body_noise | top_ops |
| --- | ---: | ---: | --- | --- | ---: | ---: | --- | --- | --- |
| G001 | 30 | 817087.76 | `6e7b1fc07d810c00` | `6f4036f660dd5df7` | 2 | 30 | `Notify Wait:4380; MODEL_EXECUTE:1110; NOTIFY_WAIT:1110; NOTIFY_RECORD:1102` | `control_or_transfer:capture_wait:66474; control_or_transfer:sdma:8760; control_or_transfer:capture_record:6570; control_or_transfer:mem_write_value:6570; control_or_transfer:notify_wait:4380; control_or_transfer:write_value:4380; control_or_transfer:ai_core:2190; control_or_transfer:notify_record:787; control_or_transfer:memcpy_async:30` | `CAPTURE_WAIT:66474; SDMA:8760; CAPTURE_RECORD:6570; MEM_WRITE_VALUE:6570; Write Value:4380; Notify Wait:4380; MatMulV2:4320; AI_CORE:2190; AddRmsNormBias:2160; Cast:1230` |

## Compute Prelude Summary

- main_events: `1583`
- projected_main_events: `1583`
- transparent_main_events: `0`
- exec_us: `1042666.06`
- data_move_us: `29891.98`
- collective_anchor_us: `696569.9`

## Graph Replay Nodes

| node | provider | replay | depth | anchors | start_ns | end_ns | device_us | raw_tasks | visible_events | controls | top_ops |
| --- | --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| N110 | aclgraph | 1 | 1 | 1550..1550 | 1782628682156939000 | 1782628682187909460 | 30970.46 | 3754 | 666 | `Notify Wait:146; MODEL_EXECUTE:37; NOTIFY_WAIT:37; NOTIFY_RECORD:37` | `CAPTURE_WAIT:2297; SDMA:292; CAPTURE_RECORD:219; MEM_WRITE_VALUE:219; Write Value:146; Notify Wait:146; MatMulV2:144; AI_CORE:73; AddRmsNormBias:72; Cast:41` |
| N113 | aclgraph | 2 | 1 | 1553..1553 | 1782628682193712520 | 1782628682221396860 | 27684.34 | 5453 | 666 | `Notify Wait:146; MODEL_EXECUTE:37; NOTIFY_WAIT:37; NOTIFY_RECORD:37` | `CAPTURE_WAIT:3991; SDMA:292; CAPTURE_RECORD:219; MEM_WRITE_VALUE:219; Write Value:146; Notify Wait:146; MatMulV2:144; AI_CORE:73; AddRmsNormBias:72; Cast:41` |
| N117 | aclgraph | 3..30 | 2 | 1556..1583 | 1782628682226619040 | 1782628683201044500 | 976961.8 | 101104 | 360 | `Notify Wait:4088; MODEL_EXECUTE:1036; NOTIFY_WAIT:1036; NOTIFY_RECORD:1028` | `CAPTURE_WAIT:60186; SDMA:8176; CAPTURE_RECORD:6132; MEM_WRITE_VALUE:6132; Write Value:4088; Notify Wait:4088; MatMulV2:4032; AI_CORE:2044; AddRmsNormBias:2016; Cast:1148` |

## Detected Loop Costs

| rank | node | depth | repeat | occ | anchors/occ | avg_total_us | total_us | avg_compute_us | avg_comm_us | avg_idle_us | comm% | idle% | label |
| ---: | --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | N011 | 1 | x18 | 1 | 360.0 | 13350903.48 | 13350903.48 | 18047.92 | 682600.06 | 12650255.5 | 5.11 | 94.75 | Repeat x18 |
| 2 | N004 | 1 | x34 | 1 | 34.0 | 1305905.24 | 1305905.24 | 89.02 | 0.0 | 1305816.22 | 0.0 | 99.99 | Repeat x34 |
| 3 | N082 | 1 | x18 | 1 | 396.0 | 1179331.94 | 1179331.94 | 4509.88 | 1702.18 | 1173119.88 | 0.14 | 99.47 | Repeat x18 |
| 4 | N116 | 1 | x28 | 1 | 28.0 | 976961.8 | 976961.8 | 777366.22 | 40988.54 | 158607.04 | 4.2 | 16.23 | Repeat x28 |
| 5 | N006 | 1 | x7 | 1 | 21.0 | 177672.54 | 177672.54 | 85.26 | 0.0 | 177587.28 | 0.0 | 99.95 | Repeat x7 |
| 6 | N002 | 1 | x4 | 1 | 4.0 | 108431.66 | 108431.66 | 27.64 | 0.0 | 108404.02 | 0.0 | 99.97 | Repeat x4 |
| 7 | N059 | 1 | x18 | 1 | 360.0 | 55155.86 | 55155.86 | 3375.96 | 1296.16 | 50483.74 | 2.35 | 91.53 | Repeat x18 |
| 8 | N036 | 1 | x18 | 1 | 360.0 | 52415.22 | 52415.22 | 3435.4 | 10292.82 | 38687.0 | 19.64 | 73.81 | Repeat x18 |
| 9 | N008 | 2 | x2 | 7 | 2.0 | 61.017 | 427.12 | 9.066 | 0.0 | 51.951 | 0.0 | 85.14 | Repeat x2 |

## Anchor View Summary

- view: `hybrid_anchor_sequence`
- lookup: `Nxxx` node ids join through `*.anchor.node_metrics.csv`, `*.anchor.node_anchor_links.csv`, and `*.anchor.steps.csv` for symbols and raw labels.
- anchor_events: `1583`
- aux_slots: `1583`
- symbol_roles: `anchor:8 aux:26 transparent:1`
