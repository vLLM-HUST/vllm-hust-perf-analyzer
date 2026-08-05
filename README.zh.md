# TraceLoom

[English](README.md)

TraceLoom 是一个原生 C++17 离线性能分析器。它读取 Ascend/CANN
`msprof` SQLite 时间线，将密集的底层事件压缩成紧凑、可比较的 Pattern
Compression Tree，并统计计算、通信、等待、辅助和节点自身成本。

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
  -> Loop Tree 报告和可选 SQLite/JSON 证据
```

核心能力：

- 自动发现 Ascend/CANN monolithic 与 split-SQLite profiler；当 monolithic
  `TASK` 不可用时自动切换 split fallback；
- 原生重建计算、HCCL、同步和 ACLGraph 执行语义；
- 在 semantic anchor 序列上发现重复的 decode/layer 结构；
- 对并发 stream 使用不重复计算的 wall-clock 统计；
- Repeat 节点的平均值按循环体迭代次数归一化；
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
```

默认报告会写在数据库旁边：

```text
/path/to/PROF_.../traceloom/loop_tree_v2.md
```

### 2. 分析 profiler 目录

```bash
traceloom /path/to/msprof_output
```

TraceLoom 会自动发现 monolithic `PROF_*/msprof_*.db`、
`torch_npu.profiler` 生成的
`*/ASCEND_PROFILER_OUTPUT/ascend_pytorch_profiler[_<rank>].db`（多 rank
输出会带数字后缀），以及 split
`PROF_*/{host,device_*}/sqlite/*.db`，并为每张设备数据库生成报告：

```text
/path/to/msprof_output/traceloom/device0_loop_tree_v2.md
/path/to/msprof_output/traceloom/device1_loop_tree_v2.md
```

对于大型 trace，可以显式设置并行度：

```bash
traceloom /path/to/msprof_output --threads 48
```

同一个 `PROF_*` 中，非空的 monolithic `TASK` 表优先；如果它不存在或不可用，
TraceLoom 会给出 warning，并从 split `AscendTask`、`TaskInfo`、`HostTask`、
`ApiData` 构建基础时间线。split 的细粒度通信、Graph Replay 和 PMU 归因仍按
增量阶段继续完善。

### 3. 阅读 Loop Tree

先从最外层的 `Repeat xN` 开始，再比较它的子节点。最常用的列是：

- `total_us`：互不相交的 wall-clock 区间并集，stream 重叠不会重复计算；
- `avg_total_us`：普通节点按 occurrence 平均，Repeat 节点按循环体迭代平均；
- `avg_compute_us`、`avg_comm_us`、`avg_idle_us`：可以直接比较的平均成本；
- `avg_aux_us`、`avg_self_us`：归因到节点的辅助成本和节点自身成本。

### 4. 生成高级证据

普通流程只生成 Loop Tree。单张数据库需要更多证据时，可以使用：

```bash
traceloom /path/to/msprof.db \
  --loop-tree-out /tmp/loop_tree_v2.md \
  --compat-db-out /tmp/traceloom-sidecar.db \
  --out /tmp/native_result.json
```

运行 `traceloom --help-advanced` 可以查看 grammar diagnostics 和辅助归因
materialization 选项。

## 典型用法：阅读 Graph Replay 之间的工作

不要预设两个 graph unit 之间只是 idle 或 overhead。服务、训练和
pipeline trace 都可能在受保护的 graph replay 之间插入大段非 graph
的 productive sequence。TraceLoom 会把这些任务保留在全局结构里，
并压缩其内部重复。在当前报告中，应当从根序列里查看
`graph_unit` 之间是否存在非空的 `Seq`/`Repeat` 结构；只读 graph
reconstruction count 不等于读完整条执行时间线。

最高层结构适合先整理成大横表阅读。下表的中性
`structural_unit` wrapper 是待实现的读者界面；当前报告可能会把
同一 body 展开成包含 `Repeat x47` 的 sequence：

| order | node | kind | run | structural fingerprint | task count | shape signature | total_us | evidence |
| ---: | --- | --- | ---: | --- | ---: | --- | ---: | --- |
| 0 | `G1` | `graph_unit` | 1 | `graph:T1/body:B1` | 1024 | `S-graph-1` | measured | `exact` |
| 1 | `U7` | `structural_unit` | 1 | `H7 (contains Repeat x47)` | 2106 | `S471` | measured | `complete` |
| 2 | `G1` | `graph_unit` | 3 | `graph:T1/body:B1` | 1024 each | `S-graph-1` | measured | `exact` |
| 3 | `U8` | `structural_unit` | 1 | `H8 (contains Repeat x47)` | 2105 | `S472` | measured | `complete` |

这是 observation format，不是 workload-semantic classifier。TraceLoom 可以报告
profiler 明示的 graph identity、具体算子、原始 shape、cardinality、
timing、repeat 和 provenance；它不会自行判定 `G1` 是 decode，`U7`
是 prefill，也不会声称某个 node 导致了端到端变化。

应当结合 Loop Tree 和 evidence database 使用：展开 `U7`，核对其
`Repeat x47` body 和原始行，再与外部 workload metadata 组合。人类或
agent 负责提出并验证解释，同时使解释与 TraceLoom 的结构观测
保持清晰分层。本案例与实现 TODO 见
[`notes/interleaved-structural-units-milestone.md`](notes/interleaved-structural-units-milestone.md)。

## 仓库内置 Kickstart Profile

仓库在 [`examples/kickstart_smoke`](examples/kickstart_smoke) 中提供了一份真实的
双卡 vLLM-Ascend profile：

```bash
traceloom examples/kickstart_smoke/msprof_raw
```

这份 capture 包含超过 118 万行选定 profiler 记录。TraceLoom 将它们压缩成
112 个结构节点，并在两张设备上恢复出相同的 `Repeat x36 -> Repeat x24`
transformer 嵌套结构。

## 小型论文 Artifact

仓库还在
[`examples/paper_artifacts/ascend_interleaved`](examples/paper_artifacts/ascend_interleaved)
内置了一对总计 3.55 MiB 的 Ascend profiler 缩减数据库，便于快速审阅精确重建
能力。它保留了四个精确 graph unit，以及穿插其间的一大两小完整 productive
sequence，同时保留原始行 provenance 和相对完整 profile 的等价性清单。验证器会
重新分析两份输入，检查结构审计与隐私边界，并把生成报告留在 Git 之外：

```bash
examples/paper_artifacts/tools/verify_ascend_interleaved.py \
  --traceloom build/native-tests/native/traceloom
```

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
- [Loop Tree 阅读指南](docs/tree-map-guide.zh.md)
- [论文可发表性路线图](notes/publication-readiness-roadmap.md)
- [CUDA 真实模型 Graph 交接说明](notes/cuda-real-model-graph-handoff.md)

## License

TraceLoom 使用 [MIT License](LICENSE)。
