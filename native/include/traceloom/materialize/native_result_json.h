#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include "traceloom/analysis/native_pipeline.h"
#include "traceloom/analysis/semantic_task_classifier.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/core/string_table.h"
#include "traceloom/report/anchor_internal_cost_breakdown.h"

namespace traceloom {

struct NativeResultJsonOptions {
  std::string source_kind = "unknown";
  std::string source_path;
  std::string fixture_id;
  std::size_t thread_count = 1;
  std::size_t top_candidate_limit = 16;
  double load_source_adapter_ms = 0.0;
  double materialization_ms = 0.0;
  const NativeIr* native_ir = nullptr;
  const SemanticTaskClassificationResult* semantic_task_classification =
      nullptr;
  const AnchorInternalCostBreakdown* anchor_internal_cost_breakdown = nullptr;
  const std::vector<compat::StructuralUnitSqlRow>* structural_units = nullptr;
};

void write_native_result_json(std::ostream& out,
                              const SymbolTable& symbols,
                              const NativePipelineResult& result,
                              const NativeResultJsonOptions& options);

}  // namespace traceloom
