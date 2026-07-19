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

- 自动发现 Ascend/CANN `msprof` 数据库和 profiler 目录；
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

TraceLoom 会自动发现 `PROF_*/msprof_*.db`，并为每张设备数据库生成报告：

```text
/path/to/msprof_output/traceloom/device0_loop_tree_v2.md
/path/to/msprof_output/traceloom/device1_loop_tree_v2.md
```

对于大型 trace，可以显式设置并行度：

```bash
traceloom /path/to/msprof_output --threads 48
```

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
- [Loop Tree 阅读指南](docs/tree-map-guide.zh.md)

## License

TraceLoom 使用 [MIT License](LICENSE)。
