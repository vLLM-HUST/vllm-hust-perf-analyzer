# TraceLoom Summary

- out_dir: `examples/kickstart_smoke/msprof_raw/traceloom`
- analyzed_devices: `2`

## Devices

| rank | db | device | augmented_db | anchors | used_total_us | prelude_gap_us | prelude_comm_us | prelude_idle_us |
| ---: | --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | db02 | 0 | db02.traceloom_augmented.db | 16 | 1835.619 | 2604681.02 | 0.48 | 2604677.22 |
| 2 | db01 | 1 | db01.traceloom_augmented.db | 16 | 253.98 | 2740648.94 | 0.44 | 2740645.76 |

## Top Loop Costs

| rank | device | node | repeat | occ | anchors/occ | avg_total_us | total_us | avg_compute_us | avg_comm_us | avg_idle_us |
| ---: | ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | N002 | x8 | 1 | 16.0 | 2730453.24 | 2730453.24 | 34.16 | 129.0 | 2730290.08 |
| 2 | 0 | N002 | x8 | 1 | 16.0 | 2600142.509 | 2600142.509 | 33.861 | 1709.532 | 2598399.116 |

## Checked-In Files

- `queries/*.sql`
- `tree-map.md`
- `summary.md`

## Generated On Re-Run

Running `traceloom analyze examples/kickstart_smoke/msprof_raw --devices 0,1`
also creates local `dbNN.traceloom_augmented.db`, `README.md`, and `meta.json`
files in this directory.
