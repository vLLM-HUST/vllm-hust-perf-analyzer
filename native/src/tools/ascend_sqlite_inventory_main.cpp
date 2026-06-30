#include <algorithm>
#include <cstddef>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/adapters/ascend_sqlite_adapter.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/pattern/candidate_reduce.h"
#include "traceloom/pattern/candidate_scan.h"
#include "traceloom/sequence/boundary_index.h"
#include "traceloom/sequence/partition_plan.h"
#include "traceloom/sequence/protected_sequence.h"

namespace {

void print_usage(const char* argv0) {
  std::cerr << "usage: " << argv0 << " <ascend-msprof.db>\n";
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
    const traceloom::AscendSQLiteAdapter adapter(db_path);
    traceloom::NativeIr ir = adapter.load();
    traceloom::FlatAnchorBuildConfig anchor_config;
    anchor_config.skipped_task_type_symbols = {"CAPTURE_WAIT"};
    anchor_config.skip_tasks_covered_by_communication_ops = true;
    const traceloom::FlatAnchorBuildStats anchor_stats =
        traceloom::build_flat_anchors(ir, anchor_config);
    const traceloom::ProtectedSequence sequence =
        traceloom::ProtectedSequence::from_token_table(ir.tokens);
    const traceloom::BoundaryIndex boundaries =
        traceloom::BoundaryIndex::build(sequence, ir.protected_intervals);
    const traceloom::PartitionPlan plan = traceloom::PartitionPlan::build(
        sequence.size(), traceloom::PartitionPlanConfig{4096, 3});
    const traceloom::CandidateScanResult scan_result =
        traceloom::scan_candidate_partitions_with_diagnostics(
            sequence, boundaries, plan, traceloom::CandidateScanConfig{2, 3},
            4);
    const std::vector<traceloom::CandidateSummaryRow> candidate_summary =
        traceloom::reduce_candidates(scan_result.occurrences);
    std::vector<traceloom::CandidateSummaryRow> top_candidates =
        candidate_summary;
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

    std::cout << "source_kind=ascend_sqlite\n";
    std::cout << "source_path=" << db_path << "\n";
    std::cout << "source_refs=" << ir.source_refs.size() << "\n";
    std::cout << "schema_objects=" << schema_objects << "\n";
    std::cout << "strings=" << ir.strings.size() << "\n";
    std::cout << "symbols=" << ir.symbols.size() << "\n";
    std::cout << "streams=" << ir.streams.size() << "\n";
    std::cout << "trace_events=" << ir.trace_events.size() << "\n";
    std::cout << "tasks=" << ir.tasks.size() << "\n";
    std::cout << "communication_ops=" << ir.communication_ops.size() << "\n";
    std::cout << "anchor_projection=raw_event_bootstrap\n";
    std::cout << "anchors=" << ir.anchors.size() << "\n";
    std::cout << "tokens=" << ir.tokens.size() << "\n";
    std::cout << "device_event_anchors="
              << anchor_stats.device_event_anchors << "\n";
    std::cout << "communication_anchors="
              << anchor_stats.communication_anchors << "\n";
    std::cout << "skipped_task_events="
              << anchor_stats.skipped_task_events << "\n";
    std::cout << "candidate_projection=raw_event_bootstrap_len2_3\n";
    std::cout << "candidate_occurrences="
              << scan_result.occurrences.size() << "\n";
    std::cout << "candidate_distinct=" << candidate_summary.size() << "\n";
    std::cout << "candidate_diagnostics="
              << scan_result.diagnostics.size() << "\n";
    const std::size_t preview_count = std::min<std::size_t>(8, top_candidates.size());
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
