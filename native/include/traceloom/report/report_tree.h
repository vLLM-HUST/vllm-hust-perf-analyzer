#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/core/diagnostics.h"
#include "traceloom/core/ids.h"

namespace traceloom {

enum class ReportAnchorKind {
  kExec,
  kGraphH,
  kGraphL,
  kGraphT,
  kGraphTemplate,
  kGraphLaunchActivity,
  kCollective,
  kUnknown,
};

const char* report_anchor_kind_name(ReportAnchorKind kind);

enum class ReportNodeKind {
  kSeq,
  kRepeat,
  kAtom,
};

struct ReportToken {
  std::uint32_t ordinal = 0;
  std::uint32_t device_id = 0;
  SymbolId symbol_id;
  std::string display_op;
  std::string display_category;
  ReportAnchorKind anchor_kind = ReportAnchorKind::kUnknown;
  AnchorId anchor_id;
  std::string launch_activity_id;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  bool has_prelude_cost = false;
  // Portion of this anchor's wall-clock interval owned by this token after
  // overlapping streams have been coalesced.  This is an additive timeline
  // cost; the raw anchor duration remains available through start_ns/end_ns.
  double timeline_anchor_us = 0.0;
  double prelude_exec_aux_us = 0.0;
  double prelude_comm_us = 0.0;
  double prelude_idle_us = 0.0;
  double prelude_aux_event_count = 0.0;
  double prelude_aux_us = 0.0;
};

struct ReportNodeDef {
  ReportNodeDefId id;
  std::string local_node_id;
  ReportNodeKind kind = ReportNodeKind::kAtom;
  std::string display_op;
  std::string display_category;
  SymbolId symbol_id;
  std::uint32_t repeat_count = 0;
  std::uint32_t definition_order = 0;
  std::uint32_t display_depth = 0;
  std::uint32_t loop_depth = 0;
  std::string visibility_reason;
};

struct ReportNodeOccurrence {
  ReportNodeOccurrenceId id;
  ReportNodeDefId node_def_id;
  ReportNodeOccurrenceId parent_occurrence_id;
  std::uint32_t edge_order = 0;
  std::uint32_t occurrence_index_for_def = 0;
  std::uint32_t token_start_ordinal = 0;
  std::uint32_t token_end_ordinal = 0;
  std::uint32_t repeat_iteration = 0;
};

struct ReportTreeEdge {
  ReportNodeOccurrenceId parent_occurrence_id;
  ReportNodeOccurrenceId child_occurrence_id;
  std::uint32_t edge_order = 0;
};

enum class ReportCoverageKind {
  kDirectBody,
  kAtomLeaf,
};

struct ReportNodeCoverage {
  ReportNodeOccurrenceId node_occurrence_id;
  std::uint32_t token_start_ordinal = 0;
  std::uint32_t token_end_ordinal = 0;
  ReportCoverageKind kind = ReportCoverageKind::kDirectBody;
};

struct ReportTree {
  std::vector<ReportNodeDef> node_defs;
  std::vector<ReportNodeOccurrence> occurrences;
  std::vector<ReportTreeEdge> edges;
  std::vector<ReportNodeCoverage> coverage;
  std::vector<Diagnostic> diagnostics;
  std::vector<std::uint32_t> occurrence_counts_by_def;
};

const ReportNodeDef& node_def(const ReportTree& tree, ReportNodeDefId id);
const ReportNodeOccurrence& node_occurrence(const ReportTree& tree,
                                            ReportNodeOccurrenceId id);

std::size_t occurrence_count_for_def(const ReportTree& tree,
                                     ReportNodeDefId id);

}  // namespace traceloom
