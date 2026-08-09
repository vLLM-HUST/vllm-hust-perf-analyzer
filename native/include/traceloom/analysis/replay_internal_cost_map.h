#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/ir/native_ir.h"

namespace traceloom {

// Aggregation scope of the aligned aggregate surface. The map surface merges
// the structural family of repeated slot roles (for example the Lx35 layer
// family) into one explicit `role_collapsed` scope; exact per-slot drill-down
// is the member-row contract (replay-unit occurrence, slot id, slot_order,
// body and provenance retained per member).
enum class ReplayAggregationScope : std::uint8_t {
  kRoleCollapsed = 0,
};


// ---------------------------------------------------------------------------
// Replay-internal cost map
//
// Structural contract:
//   ReplayUnit -> ordered launch/composition slots -> body template ->
//   per-stream ordered members -> fine-grained costs and provenance.
//
// The map never flattens a multi-launch ReplayUnit, never assumes a
// ReplayUnit/body-template 1:1 mapping, and never infers workload semantics.
// Provider-visible identity is preserved: composition slot ids and roles,
// body template ids, raw stream ids, lane/within-stream ordinals, member
// identity symbols, and source references. The analyzer is provider- and
// workload-semantics-indifferent: it consumes only NativeIr tables.
//
// Aggregation scope: the aligned aggregate surface merges the structural
// family of repeated slot roles (for example the Lx35 layer family) under an
// explicit `role_collapsed` scope. Drill-down to one particular slot is done
// through the exact member rows, which retain replay-unit occurrence, slot
// id, slot_order, body id/template, stream, position, identity, and source
// provenance for every launch member; no evidence is lost at the map surface.
// ---------------------------------------------------------------------------

const char* replay_internal_cost_map_slot_role_name(
    ReplayCompositionSlotRole role) noexcept;
const char* replay_internal_cost_map_member_kind_name(
    GraphLaunchBodyMemberRow::Kind kind) noexcept;
const char* replay_internal_cost_map_aggregation_scope_name(
    ReplayAggregationScope scope) noexcept;

// Fine-grained row for one provider-visible body member with cost and
// provenance evidence. `relative_start_ns`/`relative_end_ns` are offsets from
// the earliest valid member start of the owning body (pure re-labeling of
// provider timestamps, not an invented wall-clock attribution).
//
// Scheduled-work share: `scheduled_work_share_ppm` is this member's share of
// the owning body's scheduled task_sum, in integer parts-per-million
// (member duration / owning body task_sum * 1,000,000, truncated). The exact
// denominator is `scheduled_work_denominator_body_task_sum_ns` (the owning
// body's task_sum). The share is scheduled-work evidence only: it is never a
// wall-clock or overlap-safe attribution, and `scheduled_work_share_supported`
// is false (share left at zero) when the denominator is zero -- a zero
// denominator never manufactures a value.
struct ReplayMemberCostRow {
  ReplayUnitId replay_unit_id;
  ReplayUnitLaunchMemberId replay_unit_launch_member_id;
  std::uint32_t member_order = 0;
  GraphLaunchOccurrenceId graph_launch_occurrence_id;
  ReplayCompositionSlotId replay_composition_slot_id;
  ReplayCompositionSlotRole slot_role =
      ReplayCompositionSlotRole::kUnclassified;
  std::uint32_t slot_order = 0;
  ReplayBodyTemplateId replay_body_template_id;
  GraphLaunchBodyId graph_launch_body_id;
  GraphLaunchBodyMemberId graph_launch_body_member_id;
  std::uint32_t device_id = 0;
  std::uint32_t stream_id = 0;             // raw provider stream id
  std::uint32_t lane_ordinal = 0;
  std::uint32_t within_stream_position = 0;  // per-stream task ordinal
  GraphLaunchBodyMemberRow::Kind kind =
      GraphLaunchBodyMemberRow::Kind::kCompute;
  TaskId task_id;
  TraceEventId trace_event_id;
  SourceRefId source_ref_id;
  SymbolId identity_symbol_id;  // op_name -> comm_name -> invalid
  std::uint64_t raw_task_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::uint64_t duration_ns = 0;
  std::int64_t relative_start_ns = 0;
  std::int64_t relative_end_ns = 0;
  std::uint64_t scheduled_work_share_ppm = 0;
  bool scheduled_work_share_supported = false;
  std::uint64_t scheduled_work_denominator_body_task_sum_ns = 0;
};

// Per-stream cost evidence for one launch member's body. `task_sum_ns` is
// the sum of member durations on the stream (scheduled-work evidence);
// `busy_union_ns` is the overlap-safe union of that stream's member
// intervals. Kind lenses (compute/communication/data_move) partition the
// stream's scheduled task_sum when every member is classified: they are
// additive in that scheduled-work sense, but they are not an additive
// wall-clock decomposition and are not interchangeable with busy_union or
// with the other kind lenses across streams.
struct ReplayStreamCostRow {
  std::uint32_t stream_id = 0;
  std::uint32_t lane_ordinal = 0;
  bool lane_consistent = true;
  std::uint32_t member_count = 0;
  std::uint64_t task_sum_ns = 0;
  std::uint64_t busy_union_ns = 0;
  std::uint64_t compute_ns = 0;
  std::uint64_t communication_ns = 0;
  std::uint64_t data_move_ns = 0;
};

// One ordered launch member of a ReplayUnit with its resolved composition
// slot, body template, graph-launch body, and whole-body cost lenses
// computed locally from the pre-validated body membership with the same
// overlap-aware interval arithmetic as GraphBodyCostSummary: `task_sum_ns`
// preserves scheduled work,
// `busy_union_ns` removes cross-stream double counting, `envelope_ns` retains
// the observed wall span, and compute/communication/data_move are kind
// lenses. Kind lenses partition the body's scheduled task_sum when every
// member is classified (additive in the scheduled-work sense only); they are
// not an additive wall-clock decomposition and are not interchangeable with
// busy_union_ns or envelope_ns. Unsupported members carry
// `supported = false` with an explicit `reason_code`.
struct ReplayLaunchMemberCostRow {
  ReplayUnitLaunchMemberId replay_unit_launch_member_id;
  std::uint32_t member_order = 0;
  GraphLaunchOccurrenceId graph_launch_occurrence_id;
  ReplayCompositionSlotId replay_composition_slot_id;
  ReplayCompositionSlotRole slot_role =
      ReplayCompositionSlotRole::kUnclassified;
  std::uint32_t slot_order = 0;
  ReplayBodyTemplateId replay_body_template_id;
  GraphLaunchBodyId graph_launch_body_id;
  bool supported = false;
  std::string reason_code;  // empty when supported
  std::uint32_t member_count = 0;
  std::uint64_t task_sum_ns = 0;
  std::uint64_t busy_union_ns = 0;
  std::uint64_t envelope_ns = 0;
  std::uint64_t compute_ns = 0;
  std::uint64_t communication_ns = 0;
  std::uint64_t data_move_ns = 0;
  std::vector<ReplayStreamCostRow> streams;
};

// One ReplayUnit occurrence block preserving exact identity and ordered
// launch membership (including multi-launch H/L/T-style units and repeated
// slot roles).
struct ReplayUnitCostBlock {
  ReplayUnitId replay_unit_id;
  GraphTemplateId graph_template_id;
  SourceRefId source_ref_id;
  std::uint32_t launch_member_count = 0;
  std::uint32_t resolved_launch_count = 0;
  bool supported = false;  // true iff every launch member is supported
  std::vector<std::string> unit_reason_codes;  // sorted, deduplicated
  std::vector<ReplayLaunchMemberCostRow> launch_members;
};

// Aligned aggregate over the explicit stable structural key:
// (replay template, device, composition slot role, body template, stream,
// within-stream position, compatible member identity) under the explicit
// `role_collapsed` aggregation scope. Repeated slot roles of the same role
// (for example the Lx35 layer family) intentionally merge at this map
// surface; `launch_member_count` preserves the multiplicity contributed by
// every slot position, and drill-down to an exact slot is available through
// the member rows (replay-unit occurrence, slot id, slot_order, body and
// provenance are all retained there). Identity is part of the key, so
// inconsistent identity never fuzzy-pairs; when aligned members disagree on
// kind or lane ordinal, the row is kept with explicit flags and the
// distribution statistics are suppressed (fail closed).
//
// Scheduled-work share: `scheduled_work_share_ppm` is the aligned family's
// share of the summed owning-body scheduled task_sum
// (sum of member durations / sum of owning body task_sums * 1,000,000,
// truncated); the exact denominator is
// `scheduled_work_denominator_body_task_sum_ns`. It is scheduled-work
// evidence only, never wall-clock or overlap-safe attribution, and is
// unsupported (zero) whenever any aligned member's owning body had a zero
// task_sum.
struct ReplayAlignedCostAggregateRow {
  GraphTemplateId graph_template_id;
  std::uint32_t device_id = 0;
  ReplayCompositionSlotRole slot_role =
      ReplayCompositionSlotRole::kUnclassified;
  ReplayAggregationScope aggregation_scope = ReplayAggregationScope::kRoleCollapsed;
  ReplayBodyTemplateId replay_body_template_id;
  std::uint32_t stream_id = 0;
  std::uint32_t within_stream_position = 0;
  SymbolId identity_symbol_id;
  GraphLaunchBodyMemberRow::Kind kind =
      GraphLaunchBodyMemberRow::Kind::kCompute;
  std::uint32_t member_occurrence_count = 0;
  std::uint32_t replay_unit_count = 0;
  std::uint32_t launch_member_count = 0;
  bool kind_consistent = true;
  bool lane_consistent = true;
  bool distribution_supported = true;
  std::uint64_t duration_p25_ns = 0;
  std::uint64_t duration_median_ns = 0;
  std::uint64_t duration_p75_ns = 0;
  std::uint64_t scheduled_work_share_ppm = 0;
  bool scheduled_work_share_supported = false;
  std::uint64_t scheduled_work_denominator_body_task_sum_ns = 0;
};

// Explicit support/reason row for missing or ambiguous body evidence.
struct ReplayInternalCostIssue {
  std::string code;
  ReplayUnitId replay_unit_id;
  ReplayUnitLaunchMemberId replay_unit_launch_member_id;
  std::string detail;
};

struct ReplayInternalCostMapResult {
  std::vector<ReplayUnitCostBlock> units;
  std::vector<ReplayMemberCostRow> members;
  std::vector<ReplayAlignedCostAggregateRow> aggregates;
  std::vector<ReplayInternalCostIssue> issues;
  std::vector<std::string> result_reason_codes;
  std::uint32_t resolved_launch_count = 0;
  std::uint32_t unsupported_launch_count = 0;
  std::uint32_t fully_supported_unit_count = 0;
  std::uint32_t partially_supported_unit_count = 0;
  std::uint32_t unsupported_unit_count = 0;
};

// Builds the replay-internal cost map from exact IR evidence. Deterministic
// ordering: units in table order; launch members by recorded member order
// (tie-break by member id); body members by (lane, within-stream position,
// start, body member id); aggregates by structural key. Missing, ambiguous,
// or structurally invalid evidence (empty bodies, invalid body-member
// references, duplicate within-stream positions, lane-inconsistent streams,
// out-of-range foreign keys) is reported through per-member reason codes and
// issues and suppresses the affected member/aggregate output -- never an
// empty success and never an exception.
ReplayInternalCostMapResult build_replay_internal_cost_map(
    const NativeIr& ir);

}  // namespace traceloom
