# Stream Gap 归因分析：完整工作总结

> 2026-07-08

## 总览

基于 [RFC: Synchronization Gap Attribution](rfc-synchronization-gap-attribution.md)，完成了从数据调研到可实现归因管线的全流程，同时借鉴了华为 MindSpore msprof-analyze 的架构设计，并对比了 TraceLoom Native C++ 引擎。

| 阶段 | 内容 | 产出 |
|---|---|---|
| 调研 | API 类型归类、证据边界 | 五类 host API + 三层置信度 |
| P0 | Device 级 productive timeline merge | 首次将 stream 内 gap 升级到 device 全局视角 |
| P2 | Derived Gap DB 事件化 | 38,978 个 gap 事件入库，17 种标签，可与原 DB JOIN |
| P3 | Timeline Tree 集成 | gap 作为 tree node 接入执行树 |
| P4 | 源码级 case study | 三个核心信号溯源到 `ascend_aclgraph.py` / `msprof_reader.py` |
| P5 | Host Evidence + msprof-analyze 借鉴 | 未知 gap 时间从 16.4s 缩减到 0.5s（97% reduction） |
| 对比分析 | TraceLoom Native C++ vs msprof-analyze | 7 个相似模式 + 4 个可借鉴点 |

所有产出均在仓库 `feature/derived-gap-db-timeline-integration` 分支。

---

## 关键发现

### 1. Gap 分布 Top-Heavy

99.3% 的 gap 时间集中在 6.5% 的大 gap（>1ms）上。后续分析只需聚焦这 ~5,500 个大 gap。

### 2. Stream 功能分化

每个 stream 基本只产生一种 gap：

| Stream 角色 | 主导 Pattern | 占比 |
|---|---|---|
| compute_stream | intra_kernel_launch_gap | 99.5% |
| sync_stream | event_synchronization_boundary | 99.1% |
| maintenance_stream | model_maintenance_interval | 100% |
| record_stream | event_record_boundary | 57-59% |

两 device 完全一致——pattern-based labeling 泛化性验证通过。

### 3. MODEL_MAINTAINCE 是 Profiler 标记点

源码定位：`ascend_aclgraph.py` GRAPH_TASK_KEYS。所有 MODEL_MAINTAINCE task 的 duration = 0μs（startNs = endNs）。它们是 ACL graph 控制信号，不是 runtime 任务。33s 的 control 时间应标注为 profiler artifact。

### 4. EVENT_RECORD→EVENT_WAIT 是最强信号

`aclrtStreamWaitEvent` 通过 connectionId 直接链接到设备端 EVENT_WAIT task（6,984 次 API 调用 → 4,564 个 confirmed 链接）。这是 profiler 数据中**唯一可完整追溯的 host→device 因果链条**。

---

## 核心成果

### Device Gap 归因（RFC Layer 1-4 全部实现）

| 类别 | 总时长 | 置信度 | 可操作 |
|---|---|---|---|
| `runtime_control_present` (→ 应重标为 acl_graph_control_phase) | 33.3s | heuristic | 否 |
| `host_memory_management` | 8.9s | contextual | 部分 |
| `phase_change_boundary` | 15.0s | heuristic | 否 |
| `host_sync_api_present` | 2.2s | contextual | 是 |
| `blocked_by_visible_wait` | 1.0s | **confirmed** | **是** |
| `no_observed_device_work` (残余) | 0.5s | unknown | 否 |

### 可 JOIN 的 Gap 数据库

```sql
-- 查某个 gap 区间内发生了哪些 device task 和 host API
SELECT ge.event_id, ge.durMs, ge.category, t.taskType, h.refined_category
FROM traceloom_gap_event ge
JOIN original.TASK t ON t.startNs BETWEEN ge.startNs AND ge.endNs
JOIN traceloom_gap_host_evidence h ON h.event_id = ge.event_id
WHERE ge.gap_type = 'device_visible_gap';
```

### 对比 TraceLoom Native C++ 引擎

| 维度 | 相同点 | 差异点 |
|---|---|---|
| 架构 | Adapter→Pipeline→Output 三段式 | TraceLoom 有 Grammar Engine（Sequitur 风格自动发现循环结构） |
| Idle 算法 | sort→merge→subtract（完全一致） | TraceLoom 有 Protected Interval 边界保护 |
| 多平台 | 均支持 Ascend + CUDA + Hygon | TraceLoom 有 Unified IR（分析管线与平台解耦） |
| 输出 | Star schema SQLite + denormalized views | TraceLoom 有 semantic_role + confidence 体系 |

### 从 msprof-analyze 借鉴并落地

| 借鉴来源 | 落地方式 | 效果 |
|---|---|---|
| FreeAnalysis 三层证据链 | `traceloom_gap_host_evidence` 表 | `no_observed_device_work` 缩减 97% |
| OpScheduleAdvice 小算子检测 | `traceloom_stream_idle_ratio` 表 | 每个 stream 的 gap/task 比 + 诊断 |
| StepTraceTimeAnalysis per-rank 处理 | Dynamic stream labeling | Pattern 占比 > 标签，不再硬编码 stream ID |

---

## 产出文件清单

| 路径 | 内容 |
|---|---|
| `scripts/query_msprof.py` | msprof DB 结构探索 |
| `scripts/query_stream_gaps.py` | Per-stream gap 分析 + 标签 |
| `scripts/device_gap_merge.py` | Device 级 productive timeline merge (P0) |
| `scripts/p2_derived_gap_db.py` | Derived Gap DB 生成 + label 定义 (P2) |
| `scripts/analyze_device_gaps.py` | Device gap 归因深度查询 (P3) |
| `scripts/p3_p4_gap_integration.py` | Tree 集成 + case study (P3+P4) |
| `scripts/p5_host_evidence_layer.py` | Host evidence + msprof-analyze 借鉴 (P5) |
| `notes/p0-device-gap-merge-report.md` | P0 实现汇报 |
| `notes/p0-device-gap-deep-analysis.md` | P0 深入分析（六个维度） |
| `notes/p2-p3-p4-summary-report.md` | P2-P4 汇总汇报 |
| `notes/gap-signal-case-studies.md` | 源码级 case study |
| `notes/p5-host-evidence-layer.md` | P5 详细说明 |
| `notes/pr-native-msprof-comparison.md` | TraceLoom Native vs msprof-analyze 对比 |
| `examples/.../traceloom_gap_db/` | Derived gap DB 输出（JSON + tree-map） |

---

## 下一步建议

1. **更新 MODEL_MAINTAINCE 标签**：将 `runtime_control_present` 重命名为 `acl_graph_control_phase`
2. **按阶段切片分析**：过滤 init/capture 阶段，只分析 decode loop 中的 gap
3. **多 profile 验证**：在不同模型和配置上验证 stream 标签的泛化性
4. **接入 C++ 引擎**：将 gap 归因逻辑从 Python 迁移到 `native/` pipeline 中
