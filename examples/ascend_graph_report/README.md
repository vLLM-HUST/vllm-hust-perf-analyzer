# Ascend Graph Report Example

This directory contains a sanitized TraceLoom readable report from an Ascend
graph-mode TP2 run.

- `report_dev2.md`: compact loop tree plus aligned cost columns for one device.

The report demonstrates ordinary device events, ACLGraph replay nodes,
communication operators, semantic operator labels, occurrence-normalized cost
columns, and graph metadata. Local collection paths and raw profiler package
names are redacted in the report header.

The complete profiler package and generated artifacts are intentionally kept in
the repository data plane because they are larger than this lightweight example.
