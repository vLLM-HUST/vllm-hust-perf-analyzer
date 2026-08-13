#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir;
  const SourceRefId task_source =
      ir.source_refs.append("fixture", "memory", "TASK", 0);
  const SourceRefId comm_source =
      ir.source_refs.append("fixture", "memory", "COMMUNICATION_OP", 0);

  const SymbolId ai_core = ir.symbols.intern("AI_CORE");
  const SymbolId matmul = ir.symbols.intern("MatMul");
  const SymbolId wait = ir.symbols.intern("EVENT_WAIT");
  const SymbolId all_reduce = ir.symbols.intern("hcclAllReduce");

  const TraceEventId task0 =
      ir.trace_events.append(task_source, 101, 0, 3, 100, 120, ai_core);
  const TraceEventId comm_event =
      ir.trace_events.append(comm_source, 7, 0, 3, 90, 130, all_reduce);
  const TraceEventId task1 =
      ir.trace_events.append(task_source, 102, 0, 3, 130, 150, wait);

  ir.tasks.append(task_source, task0, 11, 9001, 700, ai_core,
                  SymbolId::invalid(), matmul, SymbolId::invalid(),
                  SymbolId::invalid());
  ir.tasks.append(task_source, task1, 12, 9002, 701, wait,
                  SymbolId::invalid(), SymbolId::invalid(),
                  SymbolId::invalid(), SymbolId::invalid());
  ir.communication_ops.append(comm_source, comm_event, 700, 55, 1, 1,
                              all_reduce);

  const FlatAnchorBuildStats stats = build_flat_anchors(ir);
  require(stats.device_event_anchors == 2);
  require(stats.communication_anchors == 1);
  require(stats.skipped_task_events == 0);
  require(stats.tokens == 3);
  require(ir.anchors.size() == 3);
  require(ir.tokens.size() == 3);

  const SymbolId normalized_all_reduce =
      ir.symbols.intern("AllReduce");
  require(ir.anchors.row(AnchorId(0)).kind == AnchorKind::kCommunication);
  require(ir.anchors.row(AnchorId(0)).trace_event_id == comm_event);
  require(ir.anchors.row(AnchorId(0)).symbol_id == normalized_all_reduce);
  require(ir.tokens.row(TokenId(0)).sequence_index == 0);
  require(ir.tokens.row(TokenId(0)).symbol_id == normalized_all_reduce);

  require(ir.anchors.row(AnchorId(1)).kind == AnchorKind::kDeviceEvent);
  require(ir.anchors.row(AnchorId(1)).trace_event_id == task0);
  require(ir.anchors.row(AnchorId(1)).symbol_id == matmul);
  require(ir.tokens.row(TokenId(1)).sequence_index == 1);
  require(ir.tokens.row(TokenId(1)).symbol_id == matmul);

  require(ir.anchors.row(AnchorId(2)).trace_event_id == task1);
  require(ir.anchors.row(AnchorId(2)).symbol_id == wait);
  require(ir.tokens.row(TokenId(2)).sequence_index == 2);

  bool caught_duplicate_build = false;
  try {
    (void)build_flat_anchors(ir);
  } catch (const std::invalid_argument&) {
    caught_duplicate_build = true;
  }
  require(caught_duplicate_build);

  NativeIr filtered;
  const SourceRefId filtered_task_source =
      filtered.source_refs.append("fixture", "memory", "TASK", 0);
  const SourceRefId filtered_comm_source =
      filtered.source_refs.append("fixture", "memory", "COMMUNICATION_OP", 0);
  const SymbolId skip_symbol = filtered.symbols.intern("CAPTURE_WAIT");
  const SymbolId keep_symbol = filtered.symbols.intern("MatMul");
  const SymbolId comm_symbol = filtered.symbols.intern("hcclAllReduce");
  const SymbolId event_wait_symbol = filtered.symbols.intern("EVENT_WAIT");
  const SymbolId memcpy_symbol = filtered.symbols.intern("MEMCPY_ASYNC");
  const SymbolId maintenance_symbol =
      filtered.symbols.intern("MODEL_MAINTAINCE");
  const SymbolId unknown_kernel_symbol =
      filtered.symbols.intern("FutureFusedKernelV1");
  const SymbolId filtered_ai_core = filtered.symbols.intern("AI_CORE");
  const TraceEventId skipped_task_event = filtered.trace_events.append(
      filtered_task_source, 201, 0, 3, 0, 10, skip_symbol);
  const TraceEventId covered_task_event = filtered.trace_events.append(
      filtered_task_source, 202, 0, 3, 20, 30, keep_symbol);
  const TraceEventId kept_task_event = filtered.trace_events.append(
      filtered_task_source, 203, 0, 3, 40, 50, keep_symbol);
  const TraceEventId event_wait_task_event = filtered.trace_events.append(
      filtered_task_source, 204, 0, 3, 60, 70, event_wait_symbol);
  const TraceEventId memcpy_task_event = filtered.trace_events.append(
      filtered_task_source, 205, 0, 3, 80, 90, memcpy_symbol);
  const TraceEventId maintenance_task_event = filtered.trace_events.append(
      filtered_task_source, 206, 0, 3, 100, 110, maintenance_symbol);
  const TraceEventId unknown_task_event = filtered.trace_events.append(
      filtered_task_source, 207, 0, 3, 120, 130, unknown_kernel_symbol);
  const TraceEventId filtered_comm_event = filtered.trace_events.append(
      filtered_comm_source, 1, 0, 3, 18, 32, comm_symbol);
  filtered.tasks.append(filtered_task_source, skipped_task_event, 21, 9201,
                        800, skip_symbol, SymbolId::invalid(),
                        SymbolId::invalid(), SymbolId::invalid(),
                        SymbolId::invalid());
  filtered.tasks.append(filtered_task_source, covered_task_event, 22, 9202,
                        801, keep_symbol, SymbolId::invalid(), keep_symbol,
                        SymbolId::invalid(), SymbolId::invalid());
  filtered.tasks.append(filtered_task_source, kept_task_event, 23, 9203, 802,
                        keep_symbol, SymbolId::invalid(), keep_symbol,
                        SymbolId::invalid(), SymbolId::invalid());
  filtered.tasks.append(filtered_task_source, event_wait_task_event, 24, 9204,
                        803, event_wait_symbol, SymbolId::invalid(),
                        SymbolId::invalid(), SymbolId::invalid(),
                        SymbolId::invalid());
  filtered.tasks.append(filtered_task_source, memcpy_task_event, 25, 9205, 804,
                        memcpy_symbol, SymbolId::invalid(),
                        SymbolId::invalid(), SymbolId::invalid(),
                        SymbolId::invalid());
  filtered.tasks.append(filtered_task_source, maintenance_task_event, 26, 9206,
                        805, maintenance_symbol, SymbolId::invalid(),
                        SymbolId::invalid(), SymbolId::invalid(),
                        SymbolId::invalid());
  filtered.tasks.append(filtered_task_source, unknown_task_event, 27, 9207,
                        806, filtered_ai_core, SymbolId::invalid(),
                        unknown_kernel_symbol, SymbolId::invalid(),
                        SymbolId::invalid());
  filtered.communication_ops.append(filtered_comm_source, filtered_comm_event,
                                    801, 66, 1, 1, comm_symbol);
  FlatAnchorBuildConfig filter_config;
  filter_config.skipped_task_type_symbols = {"CAPTURE_WAIT"};
  filter_config.skip_tasks_covered_by_communication_ops = true;
  filter_config.filter_auxiliary_task_anchors = true;
  const FlatAnchorBuildStats filtered_stats =
      build_flat_anchors(filtered, filter_config);
  require(filtered_stats.skipped_task_events == 5);
  require(filtered_stats.auxiliary_task_events == 3);
  require(filtered_stats.transparent_task_events == 0);
  require(filtered_stats.unknown_anchor_task_events == 1);
  require(filtered_stats.preserved_unclassified_task_events == 1);
  require(filtered_stats.classification_policy_id ==
          "traceloom.default.accelerator-task-projection");
  require(filtered_stats.classification_policy_version == "1");
  require(filtered_stats.classification_manifest_sha256.size() == 64);
  require(filtered_stats.device_event_anchors == 2);
  require(filtered_stats.communication_anchors == 1);
  require(filtered.tokens.size() == 3);
  require(filtered.anchors.row(AnchorId(0)).kind == AnchorKind::kCommunication);
  require(filtered.anchors.row(AnchorId(0)).symbol_id ==
          filtered.symbols.intern("AllReduce"));
  require(filtered.anchors.row(AnchorId(1)).trace_event_id == kept_task_event);
  require(filtered.anchors.row(AnchorId(2)).trace_event_id ==
          unknown_task_event);

  NativeIr normalized_comm;
  const SourceRefId normalized_comm_source =
      normalized_comm.source_refs.append("ascend", "memory",
                                         "COMMUNICATION_OP", 0);
  const SymbolId unique_all_reduce0 =
      normalized_comm.symbols.intern("hcom_allReduce__503_0_1");
  const SymbolId unique_all_reduce1 =
      normalized_comm.symbols.intern("hcom_allReduce__503_1_1");
  const TraceEventId comm_event0 = normalized_comm.trace_events.append(
      normalized_comm_source, 301, 0, 7, 0, 10, unique_all_reduce0);
  const TraceEventId comm_event1 = normalized_comm.trace_events.append(
      normalized_comm_source, 302, 0, 7, 20, 30, unique_all_reduce1);
  normalized_comm.communication_ops.append(normalized_comm_source, comm_event0,
                                           900, 1, 1, 1,
                                           unique_all_reduce0);
  normalized_comm.communication_ops.append(normalized_comm_source, comm_event1,
                                           901, 2, 1, 1,
                                           unique_all_reduce1);
  const FlatAnchorBuildStats normalized_comm_stats =
      build_flat_anchors(normalized_comm);
  require(normalized_comm_stats.communication_anchors == 2);
  require(normalized_comm.tokens.size() == 2);
  require(normalized_comm.tokens.row(TokenId(0)).symbol_id ==
          normalized_comm.tokens.row(TokenId(1)).symbol_id);
  require(normalized_comm.symbols.value(
              normalized_comm.tokens.row(TokenId(0)).symbol_id) ==
          "AllReduce");
  require(normalized_comm.anchors.row(AnchorId(0)).symbol_decision.rule_id ==
          "collective.communication.allreduce-alias");
  require(normalized_comm.anchors.row(AnchorId(0)).symbol_decision.outcome ==
          StructuralSymbolOutcome::kCanonicalized);
  require(normalized_comm.symbols.value(
              normalized_comm.anchors.row(AnchorId(0))
                  .symbol_decision.observed_symbol_id) ==
          "hcom_allReduce__503_0_1");

  NativeIr normalized_aiv;
  const SourceRefId normalized_aiv_source =
      normalized_aiv.source_refs.append("ascend", "memory", "TASK", 0);
  const SymbolId aiv_all_reduce =
      normalized_aiv.symbols.intern("aiv_all_reduce_bfloat16_t");
  const TraceEventId aiv_event = normalized_aiv.trace_events.append(
      normalized_aiv_source, 401, 0, 9, 0, 10, aiv_all_reduce);
  normalized_aiv.tasks.append(normalized_aiv_source, aiv_event, 31, 9301, -1,
                              aiv_all_reduce, SymbolId::invalid(),
                              aiv_all_reduce, SymbolId::invalid(),
                              SymbolId::invalid());
  const FlatAnchorBuildStats normalized_aiv_stats =
      build_flat_anchors(normalized_aiv);
  require(normalized_aiv_stats.device_event_anchors == 1);
  require(normalized_aiv.symbols.value(
              normalized_aiv.tokens.row(TokenId(0)).symbol_id) ==
          "AIV_AllReduce");
  require(normalized_aiv.anchors.row(AnchorId(0)).symbol_decision.rule_id ==
          "ascend.task.aiv-allreduce");

  NativeIr normalized_matmul;
  const SourceRefId normalized_matmul_source =
      normalized_matmul.source_refs.append("ascend", "memory", "TASK", 0);
  const SymbolId ai_core_task = normalized_matmul.symbols.intern("AI_CORE");
  const SymbolId matmul_v2 = normalized_matmul.symbols.intern("MatMulV2");
  const SymbolId matmul_v3 = normalized_matmul.symbols.intern("MatMulV3");
  const TraceEventId matmul_event0 = normalized_matmul.trace_events.append(
      normalized_matmul_source, 501, 0, 3, 0, 10, matmul_v2);
  const TraceEventId matmul_event1 = normalized_matmul.trace_events.append(
      normalized_matmul_source, 502, 0, 3, 20, 30, matmul_v3);
  normalized_matmul.tasks.append(
      normalized_matmul_source, matmul_event0, 41, 9401, -1, ai_core_task,
      SymbolId::invalid(), matmul_v2, SymbolId::invalid(),
      SymbolId::invalid());
  normalized_matmul.tasks.append(
      normalized_matmul_source, matmul_event1, 42, 9402, -1, ai_core_task,
      SymbolId::invalid(), matmul_v3, SymbolId::invalid(),
      SymbolId::invalid());
  const FlatAnchorBuildStats normalized_matmul_stats =
      build_flat_anchors(normalized_matmul);
  require(normalized_matmul_stats.device_event_anchors == 2);
  require(normalized_matmul.tokens.size() == 2);
  require(normalized_matmul.tokens.row(TokenId(0)).symbol_id ==
          normalized_matmul.tokens.row(TokenId(1)).symbol_id);
  require(normalized_matmul.symbols.value(
              normalized_matmul.tokens.row(TokenId(0)).symbol_id) ==
          "MatMul");
  require(normalized_matmul.anchors.row(AnchorId(0)).symbol_decision.rule_id ==
          "ascend.task.matmul-backend-variant");
  require(normalized_matmul.anchors.row(AnchorId(0))
              .symbol_decision.observed_source ==
          StructuralSymbolSource::kTaskOpType);
  require(normalized_matmul.symbols.value(
              normalized_matmul.anchors.row(AnchorId(0))
                  .symbol_decision.observed_symbol_id) ==
          "MatMulV2");
  require(normalized_matmul.symbols.value(
              normalized_matmul.anchors.row(AnchorId(1))
                  .symbol_decision.observed_symbol_id) ==
          "MatMulV3");

  NativeIr reconciled;
  const SourceRefId reconciled_source =
      reconciled.source_refs.append("ascend", "memory", "TASK", 0);
  const SymbolId profiling_enable =
      reconciled.symbols.intern("PROFILING_ENABLE");
  const SymbolId profiling_disable =
      reconciled.symbols.intern("PROFILING_DISABLE");
  const SymbolId mix_aiv = reconciled.symbols.intern("KERNEL_MIX_AIV");
  const SymbolId reduce_all = reconciled.symbols.intern("ReduceAll");
  const TraceEventId enable_event = reconciled.trace_events.append(
      reconciled_source, 601, 0, 46, 0, 1, profiling_enable);
  const TraceEventId envelope_event = reconciled.trace_events.append(
      reconciled_source, 602, 0, 46, 10, 30, mix_aiv);
  const TraceEventId detail_event = reconciled.trace_events.append(
      reconciled_source, 603, 0, 46, 12, 28, mix_aiv);
  const TraceEventId disable_event = reconciled.trace_events.append(
      reconciled_source, 604, 0, 46, 40, 40, profiling_disable);
  reconciled.tasks.append(
      reconciled_source, enable_event, 1, 1, 1, profiling_enable,
      SymbolId::invalid(), SymbolId::invalid(), SymbolId::invalid(),
      SymbolId::invalid(), -1, SymbolId::invalid(), 4294967295LL);
  reconciled.tasks.append(
      reconciled_source, envelope_event, 2, 2, 2, mix_aiv,
      SymbolId::invalid(), SymbolId::invalid(), SymbolId::invalid(),
      SymbolId::invalid(), -1, SymbolId::invalid(), 4294967295LL);
  reconciled.tasks.append(
      reconciled_source, detail_event, 2, 3, 2, mix_aiv,
      SymbolId::invalid(), reduce_all, SymbolId::invalid(),
      SymbolId::invalid(), -1, SymbolId::invalid(), 0);
  reconciled.tasks.append(
      reconciled_source, disable_event, 3, 4, 3, profiling_disable,
      SymbolId::invalid(), SymbolId::invalid(), SymbolId::invalid(),
      SymbolId::invalid(), -1, SymbolId::invalid(), 4294967295LL);
  FlatAnchorBuildConfig reconciled_config;
  reconciled_config.filter_auxiliary_task_anchors = true;
  const FlatAnchorBuildStats reconciled_stats =
      build_flat_anchors(reconciled, reconciled_config);
  require(reconciled.trace_events.size() == 4);
  require(reconciled.anchors.size() == 3);
  require(reconciled_stats.reconciled_event_groups == 1);
  require(reconciled_stats.reconciled_event_members == 2);
  require(reconciled_stats.suppressed_duplicate_observations == 1);
  require(reconciled.anchors.row(AnchorId(0)).symbol_id == profiling_enable);
  require(reconciled.anchors.row(AnchorId(1)).trace_event_id == detail_event);
  require(reconciled.anchors.row(AnchorId(1)).symbol_id == reduce_all);
  require(reconciled.anchors.row(AnchorId(1)).start_ns == 10);
  require(reconciled.anchors.row(AnchorId(1)).end_ns == 30);
  require(reconciled.anchors.row(AnchorId(2)).symbol_id == profiling_disable);

  return 0;
}
