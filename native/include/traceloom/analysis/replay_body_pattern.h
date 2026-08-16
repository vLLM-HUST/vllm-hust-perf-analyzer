#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/analysis/replay_internal_cost_map.h"
#include "traceloom/analysis/structural_occurrence_graph.h"
#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/pattern/grammar_modes.h"

namespace traceloom {

// One exact coordinate domain inside a replay-body cost map. TraceLoom keeps
// streams separate: a domain is never a synthetic cross-stream total order.
struct ReplayBodyPatternDomainKey {
  GraphTemplateId graph_template_id;
  std::uint32_t device_id = 0;
  ReplayCompositionSlotRole slot_role =
      ReplayCompositionSlotRole::kUnclassified;
  ReplayAggregationScope aggregation_scope =
      ReplayAggregationScope::kRoleCollapsed;
  ReplayBodyTemplateId replay_body_template_id;
  std::uint32_t stream_id = 0;
};

enum class ReplayBodyPatternSupportStatus {
  kSupported,
  kRejected,
};

const char* replay_body_pattern_support_status_name(
    ReplayBodyPatternSupportStatus status) noexcept;

struct ReplayBodyPatternConfig {
  GrammarAlgorithmMode grammar_mode = GrammarAlgorithmMode::kAnalysisQualityV1;
  std::size_t target_nodes_per_chunk = 4096;
  std::size_t worker_count = 1;
  std::size_t full_discovery_cap = 50000;
  std::size_t max_rounds = 10000;
};

// A supported domain owns a dense ordered Position sequence, the aggregate
// row index backing each Position, and its recursively recovered occurrence
// graph. Rejected domains retain their coordinates and a typed reason but no
// structural graph.
struct ReplayBodyPatternDomain {
  ReplayBodyPatternDomainKey key;
  ReplayBodyPatternSupportStatus support_status =
      ReplayBodyPatternSupportStatus::kRejected;
  std::string reason_code;
  GrammarAlgorithmMode grammar_mode = GrammarAlgorithmMode::kAnalysisQualityV1;
  GrammarEngineStopReason grammar_stop_reason =
      GrammarEngineStopReason::kDone;
  std::vector<std::size_t> aggregate_indices;
  StructuralOccurrenceGraph graph;
  std::size_t grammar_live_node_count = 0;
  std::size_t grammar_macro_def_count = 0;
  std::size_t grammar_step_count = 0;
};

struct ReplayBodyPatternIssue {
  std::string code;
  std::size_t domain_index = 0;
  std::string detail;
};

struct ReplayBodyPatternResult {
  std::vector<ReplayBodyPatternDomain> domains;
  std::vector<ReplayBodyPatternIssue> issues;
  std::vector<std::string> result_reason_codes;
  std::size_t supported_domain_count = 0;
  std::size_t rejected_domain_count = 0;
};

// Recovers recursive structure independently inside every exact replay-body
// stream Position sequence. The input sequence is the authoritative aligned
// replay cost map; symbols are exact (identity, kind) pairs. Invalid, sparse,
// inconsistent, oversized, or rejected domains fail closed and remain
// queryable through typed status rows.
ReplayBodyPatternResult build_replay_body_patterns(
    const NativeIr& ir,
    const ReplayInternalCostMapResult& replay_cost,
    const ReplayBodyPatternConfig& config = ReplayBodyPatternConfig{});

}  // namespace traceloom
