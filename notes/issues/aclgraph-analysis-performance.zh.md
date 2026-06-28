# TraceLoom ACLGraph 分析效率优化笔记

状态：草案

优先级：高。这个问题直接影响后续研究结果能不能快速产出。

## 一句话结论

TraceLoom 现在慢，不是因为“数据库大所以没办法”，而是因为 ACLGraph
重建里有一些很朴素的重复扫描：

```text
每个 graph 片段都去扫一遍所有语义任务；
每个 capture stream 都去扫一遍所有 TASK；
每个 graph replay 都去扫一遍所有 visible step。
```

这些算法在小 trace 上没问题，但在百万级 TASK 的 msprof 包上会被放大。

好消息是：这个问题可以先靠原始算法优化解决，不必一上来就引入 Spark
之类的大框架。

## 现在到底慢在哪

这次收集 follow-up 报告时，几个 trace 的规模差异很明显：

| profile | TASK rows | capture streams | semantic tasks | MODEL_EXECUTE | mapped timed tasks | activity segments |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| P0 graph | 13,990 | 58 | 1,823 | 569 | 5,398 | 3 |
| exp_001 overload | 415,516 | 319 | 37,095 | 11,565 | 152,166 | 469 |
| exp_002 overload | 325,889 | 319 | 32,297 | 10,111 | 130,651 | 401 |
| exp010 hot profiler | 2,098,879 | 87 | 276,399 | 92,075 | 1,052,745 | 326 |

P0 graph 只有 3 个 graph activity segment，所以当前算法跑得很快。

但 exp010 hot profiler 有 326 个 segment 和 276,399 条 semantic task。当前
`_model_execute_controls_for_segment()` 的逻辑近似是：

```python
for segment in segments:
    for task in semantic_tasks:
        if task 是 MODEL_EXECUTE 且和 segment 时间重叠:
            收进来
```

这就是：

```text
326 x 276,399 = 90,106,074 次检查
```

而且每次检查还会重新做字符串 normalize，比如把 task label 变成
`MODEL_EXECUTE` 这种 key。这个在内层循环里非常亏。

还有几个类似问题：

- `_summarize_model_streams()` 对每个 capture stream 都扫一遍全部 task。
  exp010 hot 大概就是 `87 x 2.1M` 次检查。
- `_build_replay_rows()` 为了统计 graph controls，又对每个 replay 扫
  semantic tasks。
- `_build_envelope_rows()` 为了找 graph 和 visible step 的关系，对每个
  replay 扫 visible steps。

所以问题不是“Python 慢”这么简单，而是算法形状不对。

## 不能简单按 stream 分完就结束

这里有个关键约束：TraceLoom 最终分析的不是一堆独立 stream，而是一条
全局扁平时间线。

我们最后想看到的是类似：

```text
MatMul, RmsNorm, AllReduce, ACLGraph G001, MatMul, ACLGraph G001, ...
```

也就是说，虽然原始事件来自不同 stream，但 TraceLoom 要把它们投影成一个
全局有序的 device timeline，再在这个 timeline 上做 repeat/loop tree。

ACLGraph 也天然是跨 stream 的：

- graph 内部 kernel 在 model streams 上；
- `MODEL_EXECUTE` / `NOTIFY_WAIT` 这些控制信号在控制 stream 上；
- graph 区间可能覆盖或吞掉普通 main event；
- 最后 tree compression 看的是全局事件序列，不是某个 stream 的局部序列。

所以正确思路不是：

```text
stream 1 自己分析完，stream 2 自己分析完，然后结束
```

而是：

```text
先按 stream/table 做局部整理，
再按全局时间做 merge/reduce，
最后产出一条稳定的 flat timeline。
```

换句话说，stream 分区只能是加速手段，不能成为最终语义。

## 人话版优化方案

### 1. 所有 task 只扫一遍，顺手分桶

现在有些函数会反复扫 `task_rows`。

应该改成读入 TASK 时就顺手建几个桶：

```text
rows_by_stream[stream_id] = 这个 stream 上的 task 列表
semantic_by_key["MODEL_EXECUTE"] = 所有 MODEL_EXECUTE 任务
semantic_by_key["NOTIFY_WAIT"] = 所有 NOTIFY_WAIT 任务
mapped_model_tasks = 所有 graph model stream 上的 timed task
```

这样后面谁需要某个 stream、某类 semantic task，就直接拿桶，不再全表扫。

这一步不会改变结果，只是少做重复工作。

### 2. 找 graph activity segment 仍然要全局合并

graph 内部 kernel 分布在多个 model stream 上。我们不能只在每个 stream
单独切段，因为 graph replay 是跨 stream 的。

正确做法是：

```text
每个 model stream 先保留自己的有序 task 列表；
然后做一次按 start_ns 的全局 k-way merge；
merge 过程中按 gap 阈值切 activity segment。
```

这仍然是全局时间线算法，只是实现上不再粗暴地反复排序/扫描。

### 3. 用 sweep join 找控制事件，不要 segment x semantic 全扫

现在找 `MODEL_EXECUTE` 控制事件的方式太笨：

```text
每个 segment 都扫所有 semantic tasks
```

应该改成两个有序列表一起走：

```text
segments 按 start_ns 排序
MODEL_EXECUTE tasks 按 start_ns 排序

对每个 segment：
  扔掉已经在 segment.start 之前结束的 control
  加入 start <= segment.end 的 control
  当前还活着的 control 就是重叠候选
```

这类算法通常叫 sweep line / interval join。人话就是：

```text
不要每次从头翻通讯录；
把两边都按时间排好，一边往前走一边配对。
```

复杂度从：

```text
segments x semantic_tasks
```

变成大约：

```text
segments + semantic_tasks + 实际重叠数量
```

这对 exp010 hot 这种 profile 会差很多。

### 4. 切小 graph 时复用已经找到的 MODEL_EXECUTE

我们之前认为 `MODEL_EXECUTE` 可以作为 graph replay 的边界信号，这个判断
还是成立。

但现在代码里，推断 wave size 时会找一遍 `MODEL_EXECUTE`，真正 split
segment 时又找一遍。

应该改成：

```text
先一次性算出每个 segment 对应哪些 MODEL_EXECUTE；
wave-size inference 用这份结果；
segment split 也用这份结果；
replay row 里的 control counter 也用这份结果。
```

不要同一个 overlap 关系算三遍。

### 5. graph body 统计应该在分配 task 时顺手做

现在 `_build_replay_rows()` 会拿到 segment 里的所有 child task，然后再遍历
它们统计：

- top ops；
- task type；
- body hash；
- noise signature；
- kernel count；
- kernel time；
- stream duration。

这个事情可以在“把 task 分配到 graph segment”的时候顺手完成。

也就是说，segment 不一定要一直保存很大的 child task 列表。它可以维护一个
累加器：

```text
GraphSegmentAccumulator:
  start_ns / end_ns
  streams
  top_op_counter
  task_type_counter
  body_counter
  noise_counter
  kernel_count
  kernel_us
  child_task_count
```

最后生成 graph replay row 的时候，直接从 accumulator 里拿统计值。

注意：这不是丢信息。必要的 raw evidence 仍然可以通过 source key、step row
或可选 child link 保留下来。只是默认报告不用反复携带和扫描全量 child rows。

### 6. graph envelope 也要做 interval join

现在 `_build_envelope_rows()` 大概是：

```text
for replay in replay_rows:
    for visible_step in visible_step_rows:
        如果时间重叠，就建 envelope link
```

这个也应该改成按时间排序后的 interval join。

因为 graph replay 是全局时间区间，visible step 也是全局时间区间。我们要找
的是两组区间之间的 overlap，不需要每个 replay 都从头扫所有 step。

### 7. 最后仍然投影成一条全局 flat timeline

所有优化之后，最终还是要走这一步：

```text
normal main events + graph atom events
  -> 如果选择 graph-as-atom，就移除完全被 graph 覆盖的内部普通事件
  -> 按 (start_ns, end_ns, stream_id, symbol/source) 稳定排序
  -> 交给 loop tree / readable report
```

这一步是 TraceLoom 的正确性边界。

也就是说，前面的 map/reduce、stream bucket、sweep join 都只是为了更快地产生
同一批事件和关系，不应该改变最终 timeline 的语义。

## 第一阶段怎么做最稳

我建议第一阶段不要上 DuckDB/Polars，也不要大改 IR。先做一个小而硬的 Python
优化补丁：

1. `_semantic_task_rows()` 里加入 `task_key`，避免内层循环重复 normalize。
2. 在 `analyze_aclgraph_for_device()` 里一次性建：
   - `rows_by_stream`
   - `semantic_by_key`
   - `mapped_model_tasks`
3. 写一个通用 overlap helper：
   - 输入：segments + sorted control rows
   - 输出：`controls_by_segment`
4. wave-size inference、segment split、replay control counter 都复用
   `controls_by_segment`。
5. `_summarize_model_streams()` 改成吃 `rows_by_stream`。
6. `_build_envelope_rows()` 改成 interval join。

这个阶段的目标不是“架构漂亮”，而是：

```text
让 exp010 hot/cold 和 exp012 mixed 这种大包能稳定产出现代 report。
```

## 第二阶段再考虑大数据框架

如果第一阶段还不够，再考虑把中间表做成列式/懒执行：

- DuckDB：适合 SQLite 导出后的列式扫描、group by、interval join、Parquet cache。
- Polars：适合 lazy dataframe pipeline 和分区处理。
- Spark/Ray/Dask：只有当单机列式方案不够时再考虑。

但我倾向于先别急着上重框架。TraceLoom 现在的问题很大一部分是算法重复扫描，
先把这个打掉，收益会非常直接。

更长期的形态可以是：

```text
msprof sqlite
  -> 抽取 TraceLoom columnar cache
  -> map/reduce 得到 normalized event / graph segment / evidence link
  -> 生成 augmented sqlite DB
  -> 生成 readable report
```

这里的核心仍然不是“为了大数据而大数据”，而是服务一个目标：

```text
快速、可复用、可审计地产出全局扁平 device timeline。
```

## 判断优化是否成功

验收标准很简单：

- exp010 hot profiler 能产出现代 `report_dev*.md`；
- exp010 cold profiler 能产出现代 `report_dev*.md`；
- exp012 mixed profiler 能产出现代 `report_dev*.md`；
- P0 graph 和 TP2 showcase 的 graph replay 计数、node 位置、report 结构不退化；
- 默认报告先出来，深度 graph signature 可以作为增强步骤，不要阻塞基础报告。

如果这些都能做到，TraceLoom 才能继续作为研究工具，而不是每次大 trace 都让人等到没脾气。
