#include "traceloom/materialize/native_result_json.h"

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

const char* report_anchor_kind_name(ReportAnchorKind kind) {
  switch (kind) {
    case ReportAnchorKind::kExec:
      return "exec";
    case ReportAnchorKind::kGraphH:
      return "graph_h";
    case ReportAnchorKind::kGraphL:
      return "graph_l";
    case ReportAnchorKind::kGraphT:
      return "graph_t";
    case ReportAnchorKind::kGraphTemplate:
      return "graph_template";
    case ReportAnchorKind::kGraphLaunchActivity:
      return "graph_launch_activity";
    case ReportAnchorKind::kCollective:
      return "collective";
    case ReportAnchorKind::kUnknown:
      return "unknown";
  }
  return "unknown";
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
  out << "    \"kind\": \"raw_event_bootstrap\",\n";
  out << "    \"device_event_anchors\": "
      << result.anchor_stats.device_event_anchors << ",\n";
  out << "    \"communication_anchors\": "
      << result.anchor_stats.communication_anchors << ",\n";
  out << "    \"skipped_task_events\": "
      << result.anchor_stats.skipped_task_events << "\n";
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
