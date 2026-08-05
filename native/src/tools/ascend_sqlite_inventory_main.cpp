#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "traceloom/analysis/native_pipeline.h"
#include "traceloom/adapters/ascend_sqlite_adapter.h"
#include "traceloom/ir/native_ir.h"

namespace {

void print_usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " <ascend-msprof.db-or-PROF-directory>\n";
}

std::string format_candidate_key(
    const traceloom::SymbolTable& symbols,
    const traceloom::CandidateKey& key) {
  std::ostringstream out;
  for (std::size_t index = 0; index < key.symbols.size(); ++index) {
    if (index != 0) {
      out << " ";
    }
    const traceloom::SymbolId symbol_id = key.symbols[index];
    if (symbol_id.valid()) {
      out << symbols.value(symbol_id);
    } else {
      out << "<invalid>";
    }
  }
  return out.str();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    print_usage(argc > 0 ? argv[0] : "traceloom-native-ascend-sqlite-inventory");
    return 2;
  }

  try {
    const std::string db_path = argv[1];
    const bool split = std::filesystem::is_directory(db_path);
    if (split) {
      std::cout << "source_mode=split_sqlite\n";
      for (const auto& table :
           traceloom::inventory_ascend_split_sqlite_profile(db_path)) {
        std::cout << "split_table path=" << table.db_path
                  << " table=" << table.table_name
                  << " rows=" << table.row_count << "\n";
      }
    } else {
      std::cout << "source_mode=monolithic\n";
    }
    traceloom::AscendSQLiteAdapterOptions adapter_options;
    adapter_options.db_path = db_path;
    adapter_options.source_kind =
        split ? "ascend_sqlite_split" : "ascend_sqlite";
    const traceloom::AscendSQLiteAdapter adapter(std::move(adapter_options));
    traceloom::NativeIr ir = adapter.load();

    traceloom::NativePipelineOptions pipeline_options;
    pipeline_options.thread_count = 4;
    pipeline_options.partition_config = traceloom::PartitionPlanConfig{4096, 3};
    pipeline_options.candidate_scan_config =
        traceloom::CandidateScanConfig{2, 3};
    pipeline_options.anchor_config.skip_tasks_covered_by_communication_ops =
        true;
    pipeline_options.anchor_config.filter_auxiliary_task_anchors = true;
    const traceloom::NativePipelineResult pipeline =
        traceloom::run_native_pipeline(ir, pipeline_options);
    std::vector<traceloom::CandidateSummaryRow> top_candidates =
        pipeline.reduced_candidates;
    std::sort(top_candidates.begin(), top_candidates.end(),
              [](const traceloom::CandidateSummaryRow& lhs,
                 const traceloom::CandidateSummaryRow& rhs) {
                if (lhs.occurrence_count != rhs.occurrence_count) {
                  return lhs.occurrence_count > rhs.occurrence_count;
                }
                return lhs.key < rhs.key;
              });

    std::size_t schema_objects = 0;
    for (const auto& row : ir.source_refs.rows()) {
      if (row.row_id == 0) {
        ++schema_objects;
      }
    }

    std::cout << "source_kind="
              << (split ? "ascend_sqlite_split" : "ascend_sqlite") << "\n";
    std::cout << "source_path=" << db_path << "\n";
    std::cout << "source_refs=" << ir.source_refs.size() << "\n";
    std::cout << "schema_objects=" << schema_objects << "\n";
    std::cout << "strings=" << ir.strings.size() << "\n";
    std::cout << "symbols=" << ir.symbols.size() << "\n";
    std::cout << "streams=" << ir.streams.size() << "\n";
    std::cout << "trace_events=" << ir.trace_events.size() << "\n";
    std::cout << "tasks=" << ir.tasks.size() << "\n";
    std::cout << "communication_ops=" << ir.communication_ops.size() << "\n";
    std::cout << "anchor_projection="
              << pipeline.anchor_stats.projection_kind << "\n";
    std::cout << "evidence_role_policy_id="
              << pipeline.anchor_stats.classification_policy_id << "\n";
    std::cout << "evidence_role_policy_version="
              << pipeline.anchor_stats.classification_policy_version << "\n";
    std::cout << "evidence_role_manifest_sha256="
              << pipeline.anchor_stats.classification_manifest_sha256 << "\n";
    std::cout << "anchors=" << ir.anchors.size() << "\n";
    std::cout << "tokens=" << ir.tokens.size() << "\n";
    std::cout << "device_event_anchors="
              << pipeline.anchor_stats.device_event_anchors << "\n";
    std::cout << "communication_anchors="
              << pipeline.anchor_stats.communication_anchors << "\n";
    std::cout << "skipped_task_events="
              << pipeline.anchor_stats.skipped_task_events << "\n";
    std::cout << "auxiliary_task_events="
              << pipeline.anchor_stats.auxiliary_task_events << "\n";
    std::cout << "transparent_task_events="
              << pipeline.anchor_stats.transparent_task_events << "\n";
    std::cout << "unknown_anchor_task_events="
              << pipeline.anchor_stats.unknown_anchor_task_events << "\n";
    std::cout << "candidate_projection=raw_event_bootstrap_len2_3\n";
    std::cout << "candidate_occurrences="
              << pipeline.stats.candidate_occurrence_count << "\n";
    std::cout << "candidate_distinct="
              << pipeline.reduced_candidates.size() << "\n";
    std::cout << "pattern_candidate_rows="
              << pipeline.pattern_candidate_table.rows.size() << "\n";
    std::cout << "pattern_candidate_storage=summary_only\n";
    std::cout << "pattern_candidate_summary_rows="
              << pipeline.pattern_candidate_summary.rows.size() << "\n";
    std::cout << "candidate_diagnostics="
              << pipeline.pattern_mining_diagnostics.rows.size() << "\n";
    std::cout << "cost_summary_anchor_count="
              << pipeline.cost_summary_lite.anchor_count << "\n";
    std::cout << "cost_summary_total_duration_ns="
              << pipeline.cost_summary_lite.total_duration_ns << "\n";
    std::cout << "timing_build_anchor_tokens_ms="
              << pipeline.timing.build_anchor_tokens_ms << "\n";
    std::cout << "timing_build_protected_sequence_ms="
              << pipeline.timing.build_protected_sequence_ms << "\n";
    std::cout << "timing_build_boundary_index_ms="
              << pipeline.timing.build_boundary_index_ms << "\n";
    std::cout << "timing_partition_plan_ms="
              << pipeline.timing.partition_plan_ms << "\n";
    std::cout << "timing_candidate_scan_map_ms="
              << pipeline.timing.candidate_scan_map_ms << "\n";
    std::cout << "timing_pattern_candidate_table_ms="
              << pipeline.timing.pattern_candidate_table_ms << "\n";
    std::cout << "timing_candidate_reduce_ms="
              << pipeline.timing.candidate_reduce_ms << "\n";
    std::cout << "timing_pattern_candidate_summary_ms="
              << pipeline.timing.pattern_candidate_summary_ms << "\n";
    std::cout << "timing_cost_summary_lite_ms="
              << pipeline.timing.cost_summary_lite_ms << "\n";
    std::cout << "memory_trace_event_bytes="
              << pipeline.memory.trace_event_bytes << "\n";
    std::cout << "memory_anchor_bytes=" << pipeline.memory.anchor_bytes << "\n";
    std::cout << "memory_token_bytes=" << pipeline.memory.token_bytes << "\n";
    std::cout << "memory_candidate_occurrence_bytes="
              << pipeline.memory.candidate_occurrence_bytes << "\n";
    std::cout << "memory_pattern_mining_diagnostic_bytes="
              << pipeline.memory.pattern_mining_diagnostic_bytes << "\n";
    std::cout << "memory_pattern_candidate_summary_bytes="
              << pipeline.memory.pattern_candidate_summary_bytes << "\n";
    const std::size_t preview_count =
        std::min<std::size_t>(8, top_candidates.size());
    for (std::size_t index = 0; index < preview_count; ++index) {
      const auto& row = top_candidates[index];
      std::cout << "candidate_top[" << index << "]="
                << row.occurrence_count << " "
                << format_candidate_key(ir.symbols, row.key) << "\n";
    }
    for (const auto& row : ir.source_refs.rows()) {
      if (row.row_id != 0) {
        continue;
      }
      std::cout << "  [" << static_cast<std::size_t>(row.id.value()) << "] "
                << row.table_name << "\n";
    }
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}
