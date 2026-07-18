# TraceLoom

[English](README.md)

TraceLoom 是一个原生 C++17 离线性能分析器。它读取 Ascend/CANN
`msprof` SQLite 数据库，把密集的底层时间线压缩成可阅读、可比较的
Pattern Compression Tree，并统计计算、通信、等待和辅助成本。

## 安装

在 Debian 或 Ubuntu 上构建并安装 `.deb`：

```bash
cmake -S native -B build/traceloom-native-package \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF
cmake --build build/traceloom-native-package -j "$(nproc)"
cpack --config build/traceloom-native-package/CPackConfig.cmake \
  -B build/traceloom-native-package
sudo apt install ./build/traceloom-native-package/traceloom-native_*.deb
```

安装后只有一个正式入口：

```bash
traceloom --version
traceloom --help
```

卸载：

```bash
sudo apt remove traceloom-native
```

也可以从源码安装到用户目录：

```bash
cmake --preset dev
cmake --build --preset dev -j "$(nproc)"
cmake --install build/native --prefix "$HOME/.local"
```

## 分析 Profile

直接传入一张 `msprof_*.db`：

```bash
traceloom /path/to/PROF_.../msprof_YYYYMMDDHHMMSS.db
```

或者传入包含多个 `PROF_*` 的 profiler 目录：

```bash
traceloom /path/to/msprof_output
```

TraceLoom 会自动发现数据库，并把报告写入相邻的 `traceloom/` 目录：

```text
/path/to/PROF_.../traceloom/loop_tree_v2.md
/path/to/msprof_output/traceloom/device0_loop_tree_v2.md
```

需要控制并行度时：

```bash
traceloom /path/to/msprof_output --threads 48
```

高级 JSON、grammar diagnostics 和 SQLite sidecar 选项可通过下面的命令查看：

```bash
traceloom --help-advanced
```

## 核心能力

- 将 profiler task、通信和 ACLGraph 证据归一化成统一事件模型。
- 从算子与 collective 时间线中抽取 semantic anchors。
- 自动发现重复执行结构并生成 Loop Tree。
- 对重叠 stream 使用不重复计算的 wall-clock 成本模型。
- 对 Repeat 节点按循环体迭代次数计算可比较的平均成本。
- 保留到原始数据库表和行的 provenance，支持 SQLite 下钻。

仓库内的 [kickstart profile](examples/kickstart_smoke/README.md) 可以直接用于
本地 smoke test：

```bash
traceloom examples/kickstart_smoke/msprof_raw
```

## 开发检查

```bash
cmake --preset dev-tests
cmake --build --preset dev-tests -j "$(nproc)"
ctest --preset dev-tests
```

## License

TraceLoom 使用 MIT License。
