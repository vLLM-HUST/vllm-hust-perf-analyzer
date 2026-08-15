#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/core/diagnostics.h"
#include "traceloom/core/ids.h"

namespace traceloom {

enum class StructuralAnchorKind {
  kExec,
  kGraphH,
  kGraphL,
  kGraphT,
  kGraphTemplate,
  kGraphLaunchActivity,
  kCollective,
  kUnknown,
};

const char* structural_anchor_kind_name(StructuralAnchorKind kind);

enum class StructuralNodeKind {
  kSeq,
  kRepeat,
  kAtom,
};

struct StructuralProjectionToken {
  std::uint32_t ordinal = 0;
  std::uint32_t device_id = 0;
  SymbolId symbol_id;
  std::string display_op;
  std::string display_category;
  StructuralAnchorKind anchor_kind = StructuralAnchorKind::kUnknown;
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

struct StructuralNodeDef {
  StructuralNodeDefId id;
  std::string local_node_id;
  StructuralNodeKind kind = StructuralNodeKind::kAtom;
  std::string display_op;
  std::string display_category;
  SymbolId symbol_id;
  std::uint32_t repeat_count = 0;
  std::uint32_t definition_order = 0;
  std::uint32_t display_depth = 0;
  std::uint32_t loop_depth = 0;
  std::string visibility_reason;
};

struct StructuralNodeOccurrence {
  StructuralNodeOccurrenceId id;
  StructuralNodeDefId node_def_id;
  StructuralNodeOccurrenceId parent_occurrence_id;
  std::uint32_t edge_order = 0;
  std::uint32_t occurrence_index_for_def = 0;
  std::uint32_t token_start_ordinal = 0;
  std::uint32_t token_end_ordinal = 0;
  std::uint32_t repeat_iteration = 0;
};

struct StructuralOccurrenceEdge {
  StructuralNodeOccurrenceId parent_occurrence_id;
  StructuralNodeOccurrenceId child_occurrence_id;
  std::uint32_t edge_order = 0;
};

enum class StructuralCoverageKind {
  kDirectBody,
  kAtomLeaf,
};

struct StructuralNodeCoverage {
  StructuralNodeOccurrenceId node_occurrence_id;
  std::uint32_t token_start_ordinal = 0;
  std::uint32_t token_end_ordinal = 0;
  StructuralCoverageKind kind = StructuralCoverageKind::kDirectBody;
};

struct StructuralOccurrenceGraph {
  std::vector<StructuralNodeDef> node_defs;
  std::vector<StructuralNodeOccurrence> occurrences;
  std::vector<StructuralOccurrenceEdge> edges;
  std::vector<StructuralNodeCoverage> coverage;
  std::vector<Diagnostic> diagnostics;
  std::vector<std::uint32_t> occurrence_counts_by_def;
};

const StructuralNodeDef& structural_node_def(
    const StructuralOccurrenceGraph& graph, StructuralNodeDefId id);
const StructuralNodeOccurrence& structural_node_occurrence(
    const StructuralOccurrenceGraph& graph, StructuralNodeOccurrenceId id);

std::size_t structural_occurrence_count_for_def(
    const StructuralOccurrenceGraph& graph, StructuralNodeDefId id);

}  // namespace traceloom
