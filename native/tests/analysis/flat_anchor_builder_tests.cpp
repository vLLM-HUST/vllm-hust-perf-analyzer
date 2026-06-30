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

  require(ir.anchors.row(AnchorId(0)).kind == AnchorKind::kCommunication);
  require(ir.anchors.row(AnchorId(0)).trace_event_id == comm_event);
  require(ir.anchors.row(AnchorId(0)).symbol_id == all_reduce);
  require(ir.tokens.row(TokenId(0)).sequence_index == 0);
  require(ir.tokens.row(TokenId(0)).symbol_id == all_reduce);

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
  const TraceEventId skipped_task_event = filtered.trace_events.append(
      filtered_task_source, 201, 0, 3, 0, 10, skip_symbol);
  const TraceEventId covered_task_event = filtered.trace_events.append(
      filtered_task_source, 202, 0, 3, 20, 30, keep_symbol);
  const TraceEventId kept_task_event = filtered.trace_events.append(
      filtered_task_source, 203, 0, 3, 40, 50, keep_symbol);
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
  filtered.communication_ops.append(filtered_comm_source, filtered_comm_event,
                                    801, 66, 1, 1, comm_symbol);
  FlatAnchorBuildConfig filter_config;
  filter_config.skipped_task_type_symbols = {"CAPTURE_WAIT"};
  filter_config.skip_tasks_covered_by_communication_ops = true;
  const FlatAnchorBuildStats filtered_stats =
      build_flat_anchors(filtered, filter_config);
  require(filtered_stats.skipped_task_events == 2);
  require(filtered_stats.device_event_anchors == 1);
  require(filtered_stats.communication_anchors == 1);
  require(filtered.tokens.size() == 2);
  require(filtered.anchors.row(AnchorId(0)).kind == AnchorKind::kCommunication);
  require(filtered.anchors.row(AnchorId(1)).trace_event_id == kept_task_event);

  return 0;
}
