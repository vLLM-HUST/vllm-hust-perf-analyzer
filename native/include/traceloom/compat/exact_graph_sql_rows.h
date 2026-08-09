#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

// One exact graph launch occurrence inside a replay unit: the launch/anchor
// relation. Each row corresponds to exactly one ReplayUnitLaunchMember, so the
// relation is exact by construction: it never uses timestamp containment.
struct GraphLaunchSqlRow {
  std::string launch_id;  // "graph-launch-<ReplayUnitLaunchMemberId>"
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string graph_provider;  // "cuda" | "aclgraph"
  // Replay-unit launch event: the node occurrence for this exact launch.
  std::string graph_event_id;
  // Promoted tree anchor when the exact member has one; empty otherwise.
  std::string anchor_id;
  std::uint32_t replay_unit_id = 0;
  std::uint32_t graph_template_id = 0;
  std::uint32_t graph_launch_occurrence_id = 0;
  std::uint32_t replay_body_template_id = 0;
  std::uint32_t body_id = 0;
  std::uint32_t member_order = 0;
  std::int64_t slot_order = -1;
  // raw_launch_connection_id rendered as text when >= 0 (CUDA correlationId).
  std::string correlation_id;
  std::string match_policy;
  std::string association_policy;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  double dur_us = 0.0;
  std::string evidence_level = "exact_direct";
};

// One exact body member of an exact launch: the ordered member relation.
// Keeps provider, replay/unit/template/launch/body identity, slot/member
// order, lane/task order, kind, event/task/source provenance, timing,
// correlation, raw graph-node identity, and the evidence/match/association
// semantics that distinguish exact direct evidence from envelope estimates.
struct GraphBodyMemberSqlRow {
  std::string member_id;  // "graph-body-member-<GraphLaunchBodyMemberId>"
  std::string launch_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string graph_provider;
  std::string graph_event_id;
  std::uint32_t replay_unit_id = 0;
  std::uint32_t graph_template_id = 0;
  std::uint32_t graph_launch_occurrence_id = 0;
  std::uint32_t body_id = 0;
  std::uint32_t replay_body_template_id = 0;
  std::uint32_t member_order = 0;
  std::int64_t slot_order = -1;
  std::uint32_t lane_ordinal = 0;
  std::uint32_t task_ordinal = 0;
  std::string kind;  // compute | communication | data_move
  std::string event_id;
  std::uint32_t task_id = 0;  // native TaskId
  std::string source_table;
  std::uint64_t source_row_id = 0;
  std::uint64_t raw_task_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  double dur_us = 0.0;
  std::string correlation_id;
  std::int64_t graph_node_id = -1;
  std::int64_t original_graph_node_id = -1;
  std::string match_policy;
  std::string association_policy;
  std::string evidence_level = "exact_direct";
};

struct ExactGraphSqlRows {
  std::vector<GraphLaunchSqlRow> launches;
  std::vector<GraphBodyMemberSqlRow> members;
};

// Derives the provider-neutral exact graph SQL surface from the exact NativeIr
// chain ReplayUnit -> ordered ReplayUnitLaunchMember -> slot ->
// GraphLaunchOccurrence -> GraphLaunchBody -> GraphLaunchBodyMember -> Task ->
// TraceEvent -> SourceRef. Invalid or ambiguous chains fail closed (throw)
// rather than emitting guessed rows. Best-effort replay units without launch
// membership produce no rows.
ExactGraphSqlRows build_exact_graph_sql_rows(const NativeIr& ir,
                                             const std::string& source_kind,
                                             std::uint32_t db_idx);

}  // namespace traceloom::compat
