#include "traceloom/materialize/native_result_json.h"

#include "traceloom/analysis/graph_body_cost_summary.h"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <string>
#include <vector>

namespace traceloom {
namespace {

void write_json_string(std::ostream& out, const std::string& value) {
  out << '"';
  for (const char ch : value) {
    switch (ch) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(static_cast<unsigned char>(ch)) << std::dec
              << std::setfill(' ');
        } else {
          out << ch;
        }
        break;
    }
  }
  out << '"';
}

void write_candidate_key(std::ostream& out,
                         const SymbolTable& symbols,
                         const CandidateKey& key) {
  out << '[';
  for (std::size_t index = 0; index < key.symbols.size(); ++index) {
    if (index != 0) {
      out << ", ";
    }
    const SymbolId symbol_id = key.symbols[index];
    write_json_string(out, symbol_id.valid() ? symbols.value(symbol_id)
                                             : "<invalid>");
  }
  out << ']';
}

std::vector<CandidateSummaryRow> top_candidates(
    const std::vector<CandidateSummaryRow>& candidates,
    std::size_t limit) {
  std::vector<CandidateSummaryRow> top = candidates;
  std::sort(top.begin(), top.end(),
            [](const CandidateSummaryRow& lhs,
               const CandidateSummaryRow& rhs) {
              if (lhs.occurrence_count != rhs.occurrence_count) {
                return lhs.occurrence_count > rhs.occurrence_count;
              }
              return lhs.key < rhs.key;
            });
  if (top.size() > limit) {
    top.resize(limit);
  }
  return top;
}

void write_anchor_internal_cost_breakdown(
    std::ostream& out,
    const AnchorInternalCostBreakdown& breakdown) {
  out << "  \"anchor_internal_cost_breakdown\": {\n";
  out << "    \"row_count\": " << breakdown.rows.size() << ",\n";
  out << "    \"diagnostic_count\": " << breakdown.diagnostics.size() << ",\n";
  out << "    \"rows\": [\n";
  for (std::size_t index = 0; index < breakdown.rows.size(); ++index) {
    const AnchorInternalCostBreakdownRow& row = breakdown.rows[index];
    out << "      {\"anchor_occurrence_id\": "
        << row.anchor_occurrence_id.value()
        << ", \"anchor_def_id\": " << row.anchor_def_id.value()
        << ", \"anchor_idx\": " << row.anchor_idx
        << ", \"symbol\": ";
    write_json_string(out, row.symbol);
    out << ", \"anchor_kind\": ";
    write_json_string(out, report_anchor_kind_name(row.anchor_kind));
    out << ", \"total_ns\": " << row.total_ns
        << ", \"self_ns\": " << row.self_ns
        << ", \"aux_ns\": " << row.aux_ns
        << ", \"graph_child_ns\": " << row.graph_child_ns
        << ", \"residual_ns\": " << row.residual_ns
        << ", \"raw_child_task_count\": " << row.raw_child_task_count
        << ", \"source_ref_count\": " << row.source_ref_count
        << ", \"top_ops\": ";
    write_json_string(out, row.top_ops);
    out << ", \"diagnostic_flags\": ";
    write_json_string(out, row.diagnostic_flags);
    out << "}";
    if (index + 1 < breakdown.rows.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "    ]\n";
  out << "  },\n";
}

void write_structural_units(
    std::ostream& out,
    const std::vector<compat::StructuralUnitSqlRow>* rows) {
  out << "  \"structural_units\": [\n";
  if (rows != nullptr) {
    for (std::size_t index = 0; index < rows->size(); ++index) {
      const compat::StructuralUnitSqlRow& row = (*rows)[index];
      out << "    {\"unit_id\": ";
      write_json_string(out, row.unit_id);
      out << ", \"db_idx\": " << row.db_idx
          << ", \"device_id\": " << row.device_id
          << ", \"unit_order\": " << row.unit_order
          << ", \"family_id\": ";
      write_json_string(out, row.family_id);
      out << ", \"kind\": ";
      write_json_string(out, row.kind);
      out << ", \"run_count\": " << row.run_count
          << ", \"body_fingerprint\": ";
      write_json_string(out, row.body_fingerprint);
      out << ", \"token_start_ordinal\": " << row.token_start_ordinal
          << ", \"token_end_ordinal\": " << row.token_end_ordinal
          << ", \"first_anchor_idx\": " << row.first_anchor_idx
          << ", \"last_anchor_idx\": " << row.last_anchor_idx
          << ", \"anchor_count\": " << row.anchor_count
          << ", \"start_ns\": " << row.start_ns
          << ", \"end_ns\": " << row.end_ns
          << ", \"span_us\": " << row.span_us
          << ", \"compute_us\": " << row.compute_us
          << ", \"comm_us\": " << row.comm_us
          << ", \"idle_us\": " << row.idle_us
          << ", \"total_us\": " << row.total_us
          << ", \"aux_events\": " << row.aux_events
          << ", \"aux_us\": " << row.aux_us
          << ", \"evidence_status\": ";
      write_json_string(out, row.evidence_status);
      out << ", \"boundary_policy\": ";
      write_json_string(out, row.boundary_policy);
      out << ", \"expansion_nodes\": ";
      write_json_string(out, row.expansion_nodes);
      out << ", \"shape_signature\": ";
      write_json_string(out, row.shape_signature);
      out << "}";
      if (index + 1 < rows->size()) {
        out << ',';
      }
      out << '\n';
    }
  }
  out << "  ],\n";
}

const char* graph_launch_match_policy_name(GraphLaunchMatchPolicy policy) {
  switch (policy) {
    case GraphLaunchMatchPolicy::kNotifyCompletionAdjacent:
      return "notify_completion_adjacent";
    case GraphLaunchMatchPolicy::kNotifyOrderedFallback:
      return "notify_ordered_fallback";
    case GraphLaunchMatchPolicy::kUnmatched:
      return "unmatched";
    case GraphLaunchMatchPolicy::kCudaRuntimeCorrelation:
      return "cuda_runtime_correlation";
  }
  return "unmatched";
}

const char* graph_launch_instance_association_policy_name(
    GraphLaunchInstanceAssociationPolicy policy) {
  switch (policy) {
    case GraphLaunchInstanceAssociationPolicy::kNone:
      return "none";
    case GraphLaunchInstanceAssociationPolicy::kRecordModelId:
      return "record_model_id";
    case GraphLaunchInstanceAssociationPolicy::kRecordModelStream:
      return "record_model_stream";
    case GraphLaunchInstanceAssociationPolicy::kCudaGraphNodeSet:
      return "cuda_graph_node_set";
  }
  return "none";
}

const char* graph_launch_activity_boundary_policy_name(
    GraphLaunchActivityBoundaryPolicy policy) {
  switch (policy) {
    case GraphLaunchActivityBoundaryPolicy::kHostBlockingSync:
      return "host_blocking_sync";
    case GraphLaunchActivityBoundaryPolicy::kHostThreadTail:
      return "host_thread_tail";
  }
  return "host_thread_tail";
}

const char* capture_association_policy_name(CaptureAssociationPolicy policy) {
  switch (policy) {
    case CaptureAssociationPolicy::kCaptureOrdinalAligned:
      return "capture_ordinal_aligned";
    case CaptureAssociationPolicy::kModelGroupOnly:
      return "model_group_only";
  }
  return "model_group_only";
}

const char* replay_body_topology_policy_name(
    ReplayBodyTopologyPolicy policy) {
  switch (policy) {
    case ReplayBodyTopologyPolicy::kSingleModelStream:
      return "single_model_stream";
    case ReplayBodyTopologyPolicy::kCapturedStreamSetUnordered:
      return "captured_stream_set_unordered";
    case ReplayBodyTopologyPolicy::kObservedStreamSetUnordered:
      return "observed_stream_set_unordered";
  }
  return "single_model_stream";
}

const char* replay_composition_slot_role_name(
    ReplayCompositionSlotRole role) {
  switch (role) {
    case ReplayCompositionSlotRole::kUnclassified:
      return "unclassified";
    case ReplayCompositionSlotRole::kHead:
      return "head";
    case ReplayCompositionSlotRole::kLayer:
      return "layer";
    case ReplayCompositionSlotRole::kTail:
      return "tail";
    case ReplayCompositionSlotRole::kGraph:
      return "graph";
    case ReplayCompositionSlotRole::kCudaGraph:
      return "cuda_graph";
    case ReplayCompositionSlotRole::kGeneric:
      return "generic_slot";
  }
  return "unclassified";
}

void write_nullable_i64(std::ostream& out, std::int64_t value) {
  if (value < 0) {
    out << "null";
  } else {
    out << value;
  }
}

void write_nullable_task_source_row(std::ostream& out,
                                    const NativeIr& ir,
                                    TaskId task_id) {
  if (!task_id.valid()) {
    out << "null";
    return;
  }
  const TaskRow& task = ir.tasks.row(task_id);
  if (!task.trace_event_id.valid()) {
    out << "null";
    return;
  }
  out << ir.trace_events.row(task.trace_event_id).source_row_id;
}

void write_nullable_raw_stream(std::ostream& out,
                               const NativeIr& ir,
                               StreamId stream_id) {
  if (!stream_id.valid()) {
    out << "null";
    return;
  }
  out << ir.streams.row(stream_id).raw_stream_id;
}

void write_graph_capture_evidence(std::ostream& out, const NativeIr* ir) {
  out << "  \"graph_slot_templates\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->graph_slot_templates.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const GraphSlotTemplateRow& row = rows[index];
      out << "    {\"slot_template_id\": " << row.id.value()
          << ", \"body_sequence_hash\": " << row.body_sequence_hash
          << ", \"capture_body_signature\": ";
      if (row.body_signature_symbol_id.valid()) {
        write_json_string(out, ir->symbols.value(row.body_signature_symbol_id));
      } else {
        out << "null";
      }
      out << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";

  out << "  \"captured_graph_instances\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->captured_graph_instances.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const CapturedGraphInstanceRow& row = rows[index];
      out << "    {\"captured_graph_instance_id\": " << row.id.value()
          << ", \"device_id\": " << row.device_id
          << ", \"source_table\": ";
      write_json_string(out, ir->source_refs.row(row.source_ref_id).table_name);
      out << ", \"first_source_row_id\": " << row.first_source_row_id
          << ", \"model_id\": ";
      write_nullable_i64(out, row.raw_model_id);
      out << ", \"capture_timestamp\": ";
      write_nullable_i64(out, row.raw_capture_timestamp);
      out << ", \"capture_ordinal\": ";
      write_nullable_i64(out, row.capture_ordinal);
      out << ", \"slot_template_id\": ";
      if (row.slot_template_id.valid()) {
        out << row.slot_template_id.value();
      } else {
        out << "null";
      }
      out << ", \"model_stream_count\": " << row.model_stream_count
          << ", \"association_policy\": ";
      write_json_string(
          out, capture_association_policy_name(row.association_policy));
      out << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";

  out << "  \"captured_graph_streams\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->captured_graph_streams.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const CapturedGraphStreamRow& row = rows[index];
      out << "    {\"captured_graph_stream_id\": " << row.id.value()
          << ", \"captured_graph_instance_id\": "
          << row.captured_graph_instance_id.value()
          << ", \"source_row_id\": " << row.source_row_id
          << ", \"original_stream_id\": ";
      write_nullable_i64(out, row.raw_original_stream_id);
      out << ", \"model_stream_id\": " << row.raw_model_stream_id << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";

}

void write_graph_launch_occurrences(std::ostream& out, const NativeIr* ir) {
  out << "  \"graph_launch_occurrences\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->graph_launch_occurrences.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const GraphLaunchOccurrenceRow& row = rows[index];
      out << "    {\"occurrence_id\": " << row.id.value()
          << ", \"device_id\": " << row.device_id
          << ", \"match_policy\": ";
      write_json_string(out, graph_launch_match_policy_name(row.match_policy));
      out << ", \"instance_association_policy\": ";
      write_json_string(
          out, graph_launch_instance_association_policy_name(
                   row.instance_association_policy));
      out << ", \"task_source_table\": ";
      write_json_string(out, ir->source_refs.row(row.source_ref_id).table_name);
      out << ", \"host_api_source_table\": ";
      if (row.host_api_source_ref_id.valid()) {
        write_json_string(
            out,
            ir->source_refs.row(row.host_api_source_ref_id).table_name);
      } else {
        out << "null";
      }
      out << ", \"host_api_source_row_id\": ";
      write_nullable_i64(out, row.raw_host_api_row_id);
      out << ", \"model_execute_source_row_id\": ";
      write_nullable_task_source_row(out, *ir, row.model_execute_task_id);
      out << ", \"notify_wait_source_row_id\": ";
      write_nullable_task_source_row(out, *ir, row.notify_wait_task_id);
      out << ", \"notify_record_source_row_id\": ";
      write_nullable_task_source_row(out, *ir, row.notify_record_task_id);
      out << ", \"launch_connection_id\": ";
      write_nullable_i64(out, row.raw_launch_connection_id);
      out << ", \"graph_connection_id\": ";
      write_nullable_i64(out, row.raw_graph_connection_id);
      out << ", \"model_id\": ";
      write_nullable_i64(out, row.raw_model_id);
      out << ", \"captured_graph_instance_id\": ";
      if (row.captured_graph_instance_id.valid()) {
        out << row.captured_graph_instance_id.value();
      } else {
        out << "null";
      }
      out << ", \"slot_template_id\": ";
      if (row.captured_graph_instance_id.valid()) {
        const CapturedGraphInstanceRow& instance =
            ir->captured_graph_instances.row(row.captured_graph_instance_id);
        if (instance.slot_template_id.valid()) {
          out << instance.slot_template_id.value();
        } else {
          out << "null";
        }
      } else {
        out << "null";
      }
      out << ", \"execute_stream_id\": ";
      write_nullable_raw_stream(out, *ir, row.execute_stream_id);
      out << ", \"model_stream_id\": ";
      write_nullable_raw_stream(out, *ir, row.model_stream_id);
      out << ", \"start_ns\": " << row.start_ns
          << ", \"end_ns\": " << row.end_ns
          << ", \"wait_record_end_delta_ns\": ";
      if (row.notify_wait_task_id.valid() &&
          row.notify_record_task_id.valid()) {
        out << row.wait_record_end_delta_ns;
      } else {
        out << "null";
      }
      out << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";

}

void write_graph_launch_bodies(
    std::ostream& out,
    const NativeIr* ir,
    const SemanticTaskClassificationResult* classification) {
  out << "  \"replay_body_templates\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->replay_body_templates.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const ReplayBodyTemplateRow& row = rows[index];
      out << "    {\"replay_body_template_id\": " << row.id.value()
          << ", \"source_table\": ";
      write_json_string(out, ir->source_refs.row(row.source_ref_id).table_name);
      out << ", \"exact_sequence_hash\": " << row.exact_sequence_hash
          << ", \"compute_task_count\": " << row.compute_task_count
          << ", \"communication_task_count\": "
          << row.communication_task_count
          << ", \"data_move_task_count\": " << row.data_move_task_count
          << ", \"normalized_task_count\": "
          << row.compute_task_count + row.communication_task_count +
                 row.data_move_task_count
          << ", \"stream_count\": " << row.stream_count
          << ", \"topology_policy\": ";
      write_json_string(out,
                        replay_body_topology_policy_name(row.topology_policy));
      out << ", \"op_sequence\": ";
      if (row.op_sequence_symbol_id.valid()) {
        write_json_string(out, ir->symbols.value(row.op_sequence_symbol_id));
      } else {
        out << "null";
      }
      out << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";

  out << "  \"graph_launch_bodies\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->graph_launch_bodies.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const GraphLaunchBodyRow& row = rows[index];
      out << "    {\"graph_launch_body_id\": " << row.id.value()
          << ", \"graph_launch_occurrence_id\": "
          << row.graph_launch_occurrence_id.value()
          << ", \"replay_body_template_id\": "
          << row.replay_body_template_id.value()
          << ", \"first_normalized_task_source_row_id\": ";
      write_nullable_task_source_row(out, *ir, row.first_normalized_task_id);
      out << ", \"last_normalized_task_source_row_id\": ";
      write_nullable_task_source_row(out, *ir, row.last_normalized_task_id);
      out << ", \"compute_task_count\": " << row.compute_task_count
          << ", \"communication_task_count\": "
          << row.communication_task_count
          << ", \"data_move_task_count\": " << row.data_move_task_count
          << ", \"normalized_task_count\": "
          << row.compute_task_count + row.communication_task_count +
                 row.data_move_task_count
          << ", \"stream_count\": " << row.stream_count << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";

  out << "  \"graph_launch_body_members\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->graph_launch_body_members.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const GraphLaunchBodyMemberRow& row = rows[index];
      const TaskRow& task = ir->tasks.row(row.task_id);
      const TraceEventRow& event = ir->trace_events.row(task.trace_event_id);
      SymbolId op_symbol = task.op_type_symbol_id;
      if (!op_symbol.valid()) {
        op_symbol = task.op_name_symbol_id;
      }
      if (!op_symbol.valid()) {
        op_symbol = task.comm_name_symbol_id;
      }
      if (!op_symbol.valid()) {
        op_symbol = task.task_type_symbol_id;
      }
      out << "    {\"graph_launch_body_member_id\": " << row.id.value()
          << ", \"graph_launch_body_id\": "
          << row.graph_launch_body_id.value()
          << ", \"task_id\": " << row.task_id.value()
          << ", \"source_table\": ";
      write_json_string(out,
                        ir->source_refs.row(task.source_ref_id).table_name);
      out << ", \"source_row_id\": " << event.source_row_id
          << ", \"lane_ordinal\": " << row.lane_ordinal
          << ", \"task_ordinal\": " << row.task_ordinal
          << ", \"kind\": ";
      switch (row.kind) {
        case GraphLaunchBodyMemberRow::Kind::kCompute:
          write_json_string(out, "compute");
          break;
        case GraphLaunchBodyMemberRow::Kind::kCommunication:
          write_json_string(out, "communication");
          break;
        case GraphLaunchBodyMemberRow::Kind::kDataMove:
          write_json_string(out, "data_move");
          break;
      }
      out << ", \"operator\": ";
      if (op_symbol.valid()) {
        write_json_string(out, ir->symbols.value(op_symbol));
      } else {
        out << "null";
      }
      out << ", \"task_type\": ";
      if (task.task_type_symbol_id.valid()) {
        write_json_string(out, ir->symbols.value(task.task_type_symbol_id));
      } else {
        out << "null";
      }
      out << ", \"semantic_role\": ";
      if (classification != nullptr &&
          row.task_id.value() < classification->rows.size()) {
        const SemanticTaskClassificationRow& semantic =
            classification->rows[row.task_id.value()];
        write_json_string(
            out, std::string(semantic_task_role_name(semantic.role)));
        out << ", \"semantic_rule_id\": ";
        if (semantic.matched_rule_id.has_value()) {
          write_json_string(out, *semantic.matched_rule_id);
        } else {
          out << "null";
        }
      } else {
        out << "null, \"semantic_rule_id\": null";
      }
      out << ", \"device_id\": " << event.device_id
          << ", \"stream_id\": " << event.stream_id
          << ", \"start_ns\": " << event.start_ns
          << ", \"end_ns\": " << event.end_ns
          << ", \"duration_ns\": " << event.end_ns - event.start_ns << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";
}

void write_semantic_operator_coverage(
    std::ostream& out,
    const NativeIr* ir,
    const SemanticTaskClassificationResult* classification) {
  out << "  \"semantic_operator_coverage\": ";
  if (ir == nullptr || classification == nullptr) {
    out << "null,\n";
    return;
  }
  const SemanticOperatorCoverageSummary summary =
      summarize_semantic_operator_coverage(*ir, *classification);
  out << "{\n";
  out << "    \"semantic_rules_version\": ";
  write_json_string(out, classification->semantic_rules_version);
  out << ",\n    \"semantic_rules_sha256\": ";
  write_json_string(out, classification->semantic_rules_sha256);
  out << ",\n    \"task_count\": " << summary.task_count
      << ",\n    \"unknown_task_count\": " << summary.unknown_task_count
      << ",\n    \"unregistered_operator_occurrence_count\": "
      << summary.unregistered_operator_occurrence_count
      << ",\n    \"unique_unregistered_operator_count\": "
      << summary.unregistered_operators.size()
      << ",\n    \"unregistered_operators\": [\n";
  for (std::size_t index = 0; index < summary.unregistered_operators.size();
       ++index) {
    const UnregisteredOperatorSummaryRow& row =
        summary.unregistered_operators[index];
    out << "      {\"operator\": ";
    write_json_string(out, row.operator_name);
    out << ", \"task_type\": ";
    write_json_string(out, row.task_type);
    out << ", \"semantic_role\": ";
    write_json_string(out, row.semantic_role);
    out << ", \"matched_rule_id\": ";
    if (row.matched_rule_id.empty()) {
      out << "null";
    } else {
      write_json_string(out, row.matched_rule_id);
    }
    out << ", \"occurrence_count\": " << row.occurrence_count
        << ", \"total_duration_ns\": " << row.total_duration_ns
        << ", \"graph_body_member_count\": "
        << row.graph_body_member_count << "}";
    if (index + 1 < summary.unregistered_operators.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "    ]\n  },\n";
}

void write_graph_body_cost_summary(std::ostream& out, const NativeIr* ir) {
  out << "  \"graph_body_cost_summary\": ";
  if (ir == nullptr) {
    out << "null,\n";
    return;
  }
  const GraphBodyCostSummary summary = build_graph_body_cost_summary(*ir);
  out << "{\n    \"semantics\": "
         "\"task_sum preserves scheduled work; busy_union removes "
         "cross-stream overlap; envelope includes observed gaps\",\n";
  out << "    \"occurrences\": [\n";
  for (std::size_t index = 0; index < summary.occurrences.size(); ++index) {
    const GraphBodyOccurrenceCostRow& row = summary.occurrences[index];
    out << "      {\"graph_launch_body_id\": "
        << row.graph_launch_body_id.value()
        << ", \"graph_launch_occurrence_id\": "
        << row.graph_launch_occurrence_id.value()
        << ", \"replay_body_template_id\": "
        << row.replay_body_template_id.value()
        << ", \"exact_replay_unit\": "
        << (row.exact_replay_unit ? "true" : "false")
        << ", \"member_count\": " << row.member_count
        << ", \"compute_ns\": " << row.compute_ns
        << ", \"communication_ns\": " << row.communication_ns
        << ", \"data_move_ns\": " << row.data_move_ns
        << ", \"task_sum_ns\": " << row.task_sum_ns
        << ", \"busy_union_ns\": " << row.busy_union_ns
        << ", \"envelope_ns\": " << row.envelope_ns << "}";
    if (index + 1 < summary.occurrences.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "    ],\n    \"distributions\": [\n";
  for (std::size_t index = 0; index < summary.distributions.size(); ++index) {
    const GraphBodyCostDistributionRow& row = summary.distributions[index];
    out << "      {\"replay_body_template_id\": "
        << row.replay_body_template_id.value() << ", \"scope\": ";
    write_json_string(out, std::string(graph_body_cost_scope_name(row.scope)));
    out << ", \"occurrence_count\": " << row.occurrence_count
        << ", \"task_sum_p25_ns\": " << row.task_sum_p25_ns
        << ", \"task_sum_median_ns\": " << row.task_sum_median_ns
        << ", \"task_sum_p75_ns\": " << row.task_sum_p75_ns
        << ", \"busy_union_median_ns\": " << row.busy_union_median_ns
        << ", \"envelope_median_ns\": " << row.envelope_median_ns
        << ", \"compute_median_ns\": " << row.compute_median_ns
        << ", \"communication_median_ns\": "
        << row.communication_median_ns
        << ", \"data_move_median_ns\": " << row.data_move_median_ns << "}";
    if (index + 1 < summary.distributions.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "    ]\n  },\n";
}

void write_graph_launch_activities(std::ostream& out, const NativeIr* ir) {
  out << "  \"graph_launch_activities\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->graph_launch_activities.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const GraphLaunchActivityRow& row = rows[index];
      out << "    {\"graph_launch_activity_id\": " << row.id.value()
          << ", \"source_table\": ";
      write_json_string(out, ir->source_refs.row(row.source_ref_id).table_name);
      out << ", \"global_tid\": " << row.raw_global_tid
          << ", \"first_host_api_source_row_id\": ";
      write_nullable_i64(out, row.first_host_api_row_id);
      out << ", \"last_host_api_source_row_id\": ";
      write_nullable_i64(out, row.last_host_api_row_id);
      out << ", \"boundary_host_api_source_row_id\": ";
      write_nullable_i64(out, row.boundary_host_api_row_id);
      out << ", \"boundary_api\": ";
      if (row.boundary_api_symbol_id.valid()) {
        write_json_string(out, ir->symbols.value(row.boundary_api_symbol_id));
      } else {
        out << "null";
      }
      out << ", \"start_ns\": " << row.start_ns
          << ", \"end_ns\": " << row.end_ns
          << ", \"host_execute_count\": " << row.host_execute_count
          << ", \"matched_launch_count\": " << row.matched_launch_count
          << ", \"unmatched_host_execute_count\": "
          << (row.host_execute_count > row.matched_launch_count
                  ? row.host_execute_count - row.matched_launch_count
                  : 0)
          << ", \"boundary_policy\": ";
      write_json_string(
          out,
          graph_launch_activity_boundary_policy_name(row.boundary_policy));
      out << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";

  out << "  \"graph_launch_activity_members\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->graph_launch_activity_members.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const GraphLaunchActivityMemberRow& row = rows[index];
      const GraphLaunchOccurrenceRow& launch =
          ir->graph_launch_occurrences.row(row.graph_launch_occurrence_id);
      out << "    {\"graph_launch_activity_member_id\": "
          << row.id.value() << ", \"graph_launch_activity_id\": "
          << row.graph_launch_activity_id.value()
          << ", \"graph_launch_occurrence_id\": "
          << row.graph_launch_occurrence_id.value()
          << ", \"host_execute_order\": " << row.host_execute_order
          << ", \"host_api_source_row_id\": ";
      write_nullable_i64(out, launch.raw_host_api_row_id);
      out << ", \"captured_graph_instance_id\": ";
      if (launch.captured_graph_instance_id.valid()) {
        out << launch.captured_graph_instance_id.value();
      } else {
        out << "null";
      }
      out << ", \"graph_connection_id\": ";
      write_nullable_i64(out, launch.raw_graph_connection_id);
      out << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";
}

void write_replay_composition_candidates(std::ostream& out,
                                         const NativeIr* ir) {
  out << "  \"replay_composition_candidates\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->replay_composition_candidates.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const ReplayCompositionCandidateRow& row = rows[index];
      out << "    {\"replay_composition_candidate_id\": " << row.id.value()
          << ", \"device_id\": " << row.device_id
          << ", \"segment_first_launch_id\": "
          << row.segment_first_launch_id.value()
          << ", \"pattern_first_launch_id\": "
          << row.pattern_first_launch_id.value()
          << ", \"segment_launch_count\": " << row.segment_launch_count
          << ", \"leading_launch_count\": " << row.leading_launch_count
          << ", \"pattern_length\": " << row.pattern_length
          << ", \"full_repeat_count\": " << row.full_repeat_count
          << ", \"trailing_launch_count\": " << row.trailing_launch_count
          << ", \"pattern_sequence_hash\": " << row.pattern_sequence_hash
          << ", \"identity_policy\": ";
      write_json_string(
          out, replay_composition_identity_policy_name(row.identity_policy));
      out << ", \"order_policy\": ";
      write_json_string(
          out, replay_composition_order_policy_name(row.order_policy));
      out << ", \"shape_policy\": ";
      write_json_string(
          out, replay_composition_shape_policy_name(row.shape_policy));
      out << ", \"boundary_policy\": ";
      write_json_string(
          out, replay_composition_boundary_policy_name(row.boundary_policy));
      out << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";

  out << "  \"replay_composition_slots\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->replay_composition_slots.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const ReplayCompositionSlotRow& row = rows[index];
      out << "    {\"replay_composition_slot_id\": " << row.id.value()
          << ", \"replay_composition_candidate_id\": "
          << row.replay_composition_candidate_id.value()
          << ", \"slot_order\": " << row.slot_order
          << ", \"captured_graph_instance_id\": ";
      if (row.captured_graph_instance_id.valid()) {
        out << row.captured_graph_instance_id.value();
      } else {
        out << "null";
      }
      out << ", \"slot_template_id\": ";
      if (row.slot_template_id.valid()) {
        out << row.slot_template_id.value();
      } else {
        out << "null";
      }
      out << ", \"replay_body_template_id\": ";
      if (row.replay_body_template_id.valid()) {
        out << row.replay_body_template_id.value();
      } else {
        out << "null";
      }
      out << ", \"role\": ";
      write_json_string(out, replay_composition_slot_role_name(row.role));
      out << ", \"graph_connection_id\": ";
      write_nullable_i64(out, row.raw_graph_connection_id);
      out << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";

  out << "  \"replay_composition_regions\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->replay_composition_regions.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const ReplayCompositionRegionRow& row = rows[index];
      out << "    {\"replay_composition_region_id\": " << row.id.value()
          << ", \"replay_composition_candidate_id\": "
          << row.replay_composition_candidate_id.value()
          << ", \"region_order\": " << row.region_order
          << ", \"first_launch_id\": " << row.first_launch_id.value()
          << ", \"last_launch_id\": " << row.last_launch_id.value()
          << ", \"start_ns\": " << row.start_ns
          << ", \"end_ns\": " << row.end_ns
          << ", \"observed_launch_count\": " << row.observed_launch_count
          << ", \"expected_launch_count\": ";
      if (row.expected_launch_count == 0) {
        out << "null";
      } else {
        out << row.expected_launch_count;
      }
      out << ", \"status\": ";
      write_json_string(
          out, replay_composition_region_status_name(row.status));
      out << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";

  out << "  \"replay_composition_region_members\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->replay_composition_region_members.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const ReplayCompositionRegionMemberRow& row = rows[index];
      out << "    {\"replay_composition_region_member_id\": "
          << row.id.value() << ", \"replay_composition_region_id\": "
          << row.replay_composition_region_id.value()
          << ", \"member_order\": " << row.member_order
          << ", \"graph_launch_occurrence_id\": "
          << row.graph_launch_occurrence_id.value()
          << ", \"expected_slot_order\": ";
      write_nullable_i64(out, row.expected_slot_order);
      out << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";
}

void write_replay_units(std::ostream& out, const NativeIr* ir) {
  out << "  \"graph_templates\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->graph_templates.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const GraphTemplateRow& row = rows[index];
      out << "    {\"graph_template_id\": " << row.id.value()
          << ", \"source_table\": ";
      write_json_string(out, ir->source_refs.row(row.source_ref_id).table_name);
      out << ", \"body_sequence_hash\": " << row.body_sequence_hash
          << ", \"slot_count\": " << row.slot_count << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";

  out << "  \"replay_units\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->replay_units.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const ReplayUnitRow& row = rows[index];
      out << "    {\"replay_unit_id\": " << row.id.value()
          << ", \"graph_template_id\": " << row.graph_template_id.value()
          << ", \"replay_composition_region_id\": ";
      if (row.replay_composition_region_id.valid()) {
        out << row.replay_composition_region_id.value();
      } else {
        out << "null";
      }
      out << ", \"source_table\": ";
      write_json_string(out, ir->source_refs.row(row.source_ref_id).table_name);
      out << ", \"device_id\": ";
      if (row.launch_trace_event_id.valid()) {
        const TraceEventRow& event =
            ir->trace_events.row(row.launch_trace_event_id);
        out << event.device_id << ", \"stream_id\": " << event.stream_id
            << ", \"start_ns\": " << event.start_ns
            << ", \"end_ns\": " << event.end_ns;
      } else {
        out << "null, \"stream_id\": null, \"start_ns\": null, "
               "\"end_ns\": null";
      }
      out << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";

  out << "  \"replay_unit_launch_members\": [\n";
  if (ir != nullptr) {
    const auto& rows = ir->replay_unit_launch_members.rows();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const ReplayUnitLaunchMemberRow& row = rows[index];
      const ReplayCompositionSlotRow& slot =
          ir->replay_composition_slots.row(row.replay_composition_slot_id);
      out << "    {\"replay_unit_launch_member_id\": " << row.id.value()
          << ", \"replay_unit_id\": " << row.replay_unit_id.value()
          << ", \"member_order\": " << row.member_order
          << ", \"graph_launch_occurrence_id\": "
          << row.graph_launch_occurrence_id.value()
          << ", \"replay_composition_slot_id\": "
          << row.replay_composition_slot_id.value()
          << ", \"role\": ";
      write_json_string(out, replay_composition_slot_role_name(slot.role));
      out << "}";
      if (index + 1 < rows.size()) {
        out << ",";
      }
      out << "\n";
    }
  }
  out << "  ],\n";
}

}  // namespace

void write_native_result_json(std::ostream& out,
                              const SymbolTable& symbols,
                              const NativePipelineResult& result,
                              const NativeResultJsonOptions& options) {
  const std::vector<CandidateSummaryRow> preview =
      top_candidates(result.reduced_candidates, options.top_candidate_limit);

  out << "{\n";
  out << "  \"schema_version\": \"native_in_memory_result_v1\",\n";
  out << "  \"source\": {\n";
  out << "    \"kind\": ";
  write_json_string(out, options.source_kind);
  out << ",\n";
  out << "    \"path\": ";
  write_json_string(out, options.source_path);
  out << ",\n";
  out << "    \"fixture_id\": ";
  if (options.fixture_id.empty()) {
    out << "null";
  } else {
    write_json_string(out, options.fixture_id);
  }
  out << "\n";
  out << "  },\n";
  out << "  \"threads\": " << options.thread_count << ",\n";

  out << "  \"stats\": {\n";
  out << "    \"source_ref_count\": " << result.stats.source_ref_count << ",\n";
  out << "    \"trace_event_count\": " << result.stats.trace_event_count << ",\n";
  out << "    \"task_count\": " << result.stats.task_count << ",\n";
  out << "    \"communication_op_count\": "
      << result.stats.communication_op_count << ",\n";
  out << "    \"graph_slot_template_count\": "
      << result.stats.graph_slot_template_count << ",\n";
  out << "    \"captured_graph_instance_count\": "
      << result.stats.captured_graph_instance_count << ",\n";
  out << "    \"captured_graph_stream_count\": "
      << result.stats.captured_graph_stream_count << ",\n";
  out << "    \"graph_launch_occurrence_count\": "
      << result.stats.graph_launch_occurrence_count << ",\n";
  out << "    \"graph_launch_instance_linked_count\": "
      << result.stats.graph_launch_instance_linked_count << ",\n";
  out << "    \"graph_launch_completion_adjacent_count\": "
      << result.stats.graph_launch_completion_adjacent_count << ",\n";
  out << "    \"graph_launch_ordered_fallback_count\": "
      << result.stats.graph_launch_ordered_fallback_count << ",\n";
  out << "    \"graph_launch_cuda_correlation_count\": "
      << result.stats.graph_launch_cuda_correlation_count << ",\n";
  out << "    \"graph_launch_unmatched_count\": "
      << result.stats.graph_launch_unmatched_count << ",\n";
  out << "    \"replay_body_template_count\": "
      << result.stats.replay_body_template_count << ",\n";
  out << "    \"graph_launch_body_count\": "
      << result.stats.graph_launch_body_count << ",\n";
  out << "    \"graph_launch_body_member_count\": "
      << result.stats.graph_launch_body_member_count << ",\n";
  out << "    \"graph_launch_activity_count\": "
      << result.stats.graph_launch_activity_count << ",\n";
  out << "    \"graph_launch_activity_member_count\": "
      << result.stats.graph_launch_activity_member_count << ",\n";
  out << "    \"graph_launch_activity_host_sync_count\": "
      << result.stats.graph_launch_activity_host_sync_count << ",\n";
  out << "    \"graph_launch_activity_thread_tail_count\": "
      << result.stats.graph_launch_activity_thread_tail_count << ",\n";
  out << "    \"graph_launch_activity_unmatched_host_execute_count\": "
      << result.stats.graph_launch_activity_unmatched_host_execute_count
      << ",\n";
  out << "    \"replay_composition_candidate_count\": "
      << result.stats.replay_composition_candidate_count << ",\n";
  out << "    \"replay_composition_body_confirmed_count\": "
      << result.stats.replay_composition_body_confirmed_count << ",\n";
  out << "    \"replay_composition_slot_count\": "
      << result.stats.replay_composition_slot_count << ",\n";
  out << "    \"replay_composition_region_count\": "
      << result.stats.replay_composition_region_count << ",\n";
  out << "    \"replay_composition_region_member_count\": "
      << result.stats.replay_composition_region_member_count << ",\n";
  out << "    \"replay_composition_recognized_region_count\": "
      << result.stats.replay_composition_recognized_region_count << ",\n";
  out << "    \"replay_composition_unrecognized_region_count\": "
      << result.stats.replay_composition_unrecognized_region_count << ",\n";
  out << "    \"graph_template_count\": "
      << result.stats.graph_template_count << ",\n";
  out << "    \"replay_unit_count\": " << result.stats.replay_unit_count
      << ",\n";
  out << "    \"exact_replay_unit_count\": "
      << result.stats.exact_replay_unit_count << ",\n";
  out << "    \"replay_unit_launch_member_count\": "
      << result.stats.replay_unit_launch_member_count << ",\n";
  out << "    \"anchor_count\": " << result.stats.anchor_count << ",\n";
  out << "    \"token_count\": " << result.stats.token_count << ",\n";
  out << "    \"protected_interval_count\": "
      << result.stats.protected_interval_count << ",\n";
  out << "    \"candidate_occurrence_count\": "
      << result.stats.candidate_occurrence_count << ",\n";
  out << "    \"candidate_distinct_count\": "
      << result.stats.candidate_distinct_count << ",\n";
  out << "    \"candidate_diagnostic_count\": "
      << result.stats.candidate_diagnostic_count << "\n";
  out << "  },\n";

  out << "  \"anchor_projection\": {\n";
  out << "    \"kind\": ";
  write_json_string(out, result.anchor_stats.projection_kind);
  out << ",\n";
  out << "    \"device_event_anchors\": "
      << result.anchor_stats.device_event_anchors << ",\n";
  out << "    \"communication_anchors\": "
      << result.anchor_stats.communication_anchors << ",\n";
  out << "    \"skipped_task_events\": "
      << result.anchor_stats.skipped_task_events << ",\n";
  out << "    \"preserved_unclassified_task_events\": "
      << result.anchor_stats.preserved_unclassified_task_events << "\n";
  out << "  },\n";

  out << "  \"timing_ms\": {\n";
  out << "    \"load_source_adapter\": "
      << options.load_source_adapter_ms << ",\n";
  out << "    \"adapter_load_and_native_ir_build\": "
      << options.load_source_adapter_ms << ",\n";
  out << "    \"build_anchor_tokens\": "
      << result.timing.build_anchor_tokens_ms << ",\n";
  out << "    \"build_protected_sequence\": "
      << result.timing.build_protected_sequence_ms << ",\n";
  out << "    \"build_boundary_index\": "
      << result.timing.build_boundary_index_ms << ",\n";
  out << "    \"partition_plan\": " << result.timing.partition_plan_ms << ",\n";
  out << "    \"candidate_scan_map\": "
      << result.timing.candidate_scan_map_ms << ",\n";
  out << "    \"pattern_candidate_table\": "
      << result.timing.pattern_candidate_table_ms << ",\n";
  out << "    \"candidate_reduce\": "
      << result.timing.candidate_reduce_ms << ",\n";
  out << "    \"pattern_candidate_summary\": "
      << result.timing.pattern_candidate_summary_ms << ",\n";
  out << "    \"cost_summary_lite\": "
      << result.timing.cost_summary_lite_ms << ",\n";
  out << "    \"materialization\": " << options.materialization_ms << "\n";
  out << "  },\n";

  out << "  \"memory\": {\n";
  out << "    \"trace_event_bytes\": "
      << result.memory.trace_event_bytes << ",\n";
  out << "    \"anchor_bytes\": " << result.memory.anchor_bytes << ",\n";
  out << "    \"token_bytes\": " << result.memory.token_bytes << ",\n";
  out << "    \"protected_sequence_bytes\": "
      << result.memory.token_bytes << ",\n";
  out << "    \"candidate_map_bytes\": "
      << result.memory.candidate_occurrence_bytes << ",\n";
  out << "    \"pattern_mining_diagnostic_bytes\": "
      << result.memory.pattern_mining_diagnostic_bytes << ",\n";
  out << "    \"pattern_candidate_summary_bytes\": "
      << result.memory.pattern_candidate_summary_bytes << "\n";
  out << "  },\n";

  out << "  \"cost_summary_lite\": {\n";
  out << "    \"anchor_count\": "
      << result.cost_summary_lite.anchor_count << ",\n";
  out << "    \"total_duration_ns\": "
      << result.cost_summary_lite.total_duration_ns << "\n";
  out << "  },\n";

  out << "  \"pattern_candidate_summary\": {\n";
  out << "    \"row_count\": "
      << result.pattern_candidate_summary.rows.size() << "\n";
  out << "  },\n";

  write_graph_capture_evidence(out, options.native_ir);
  write_graph_launch_occurrences(out, options.native_ir);
  write_graph_launch_bodies(out, options.native_ir,
                            options.semantic_task_classification);
  write_graph_body_cost_summary(out, options.native_ir);
  write_semantic_operator_coverage(out, options.native_ir,
                                   options.semantic_task_classification);
  write_graph_launch_activities(out, options.native_ir);
  write_replay_composition_candidates(out, options.native_ir);
  write_replay_units(out, options.native_ir);
  write_structural_units(out, options.structural_units);

  if (options.anchor_internal_cost_breakdown != nullptr) {
    write_anchor_internal_cost_breakdown(
        out, *options.anchor_internal_cost_breakdown);
  }

  out << "  \"candidates_preview\": [\n";
  for (std::size_t index = 0; index < preview.size(); ++index) {
    const CandidateSummaryRow& row = preview[index];
    out << "    {\"rank\": " << index << ", \"occurrence_count\": "
        << row.occurrence_count << ", \"first_begin\": "
        << row.first_begin << ", \"key\": ";
    write_candidate_key(out, symbols, row.key);
    out << "}";
    if (index + 1 < preview.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ],\n";

  out << "  \"diagnostics\": {\n";
  out << "    \"pattern_mining_count\": "
      << result.pattern_mining_diagnostics.rows.size() << ",\n";
  out << "    \"pattern_mining_preview\": [\n";
  const std::size_t diagnostic_preview_count =
      std::min<std::size_t>(16, result.pattern_mining_diagnostics.rows.size());
  for (std::size_t index = 0; index < diagnostic_preview_count; ++index) {
    const CandidateDiagnostic& diagnostic =
        result.pattern_mining_diagnostics.rows[index];
    out << "      {\"code\": ";
    write_json_string(out, candidate_diagnostic_code_name(diagnostic.code));
    out << ", \"begin\": " << diagnostic.begin
        << ", \"end\": " << diagnostic.end
        << ", \"key\": ";
    write_candidate_key(out, symbols, diagnostic.key);
    out << ", \"partition_id\": " << diagnostic.partition_id.value()
        << ", \"protected_interval_id\": "
        << diagnostic.protected_interval_id.value() << "}";
    if (index + 1 < diagnostic_preview_count) {
      out << ",";
    }
    out << "\n";
  }
  out << "    ]\n";
  out << "  }\n";
  out << "}\n";
}

}  // namespace traceloom
