#include "traceloom/adapters/aclgraph_fixture_adapter.h"

#include "traceloom/analysis/anchor_graph_child_cost.h"
#include "traceloom/analysis/structural_occurrence_builder.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace traceloom {

namespace {

struct AnchorSpan {
  AnchorId first_anchor_id;
  AnchorId last_anchor_id;
  TokenId first_token_id;
  TokenId last_token_id;
  bool valid = false;
};

RoleId role_id_for_slot_symbol(const std::string& slot_symbol) {
  if (slot_symbol == "H") {
    return RoleId(1);
  }
  if (slot_symbol == "L") {
    return RoleId(2);
  }
  if (slot_symbol == "T") {
    return RoleId(3);
  }
  return RoleId(0);
}

StructuralAnchorKind structural_anchor_kind_for_anchor_kind(AnchorKind anchor_kind) {
  switch (anchor_kind) {
    case AnchorKind::kDeviceEvent:
      return StructuralAnchorKind::kExec;
    case AnchorKind::kCommunication:
      return StructuralAnchorKind::kCollective;
    case AnchorKind::kGraphH:
      return StructuralAnchorKind::kGraphH;
    case AnchorKind::kGraphL:
      return StructuralAnchorKind::kGraphL;
    case AnchorKind::kGraphT:
      return StructuralAnchorKind::kGraphT;
    default:
      return StructuralAnchorKind::kUnknown;
  }
}

std::vector<StructuralProjectionToken> structural_tokens_from_ir(const NativeIr& ir) {
  std::vector<StructuralProjectionToken> tokens;
  tokens.reserve(ir.tokens.size());
  for (const TokenRow& token : ir.tokens.rows()) {
    const AnchorRow& anchor = ir.anchors.row(token.anchor_id);
    StructuralProjectionToken out;
    out.ordinal = token.sequence_index;
    out.symbol_id = token.symbol_id;
    out.display_op = ir.symbols.value(token.symbol_id);
    out.display_category = "graph";
    out.anchor_kind = structural_anchor_kind_for_anchor_kind(anchor.kind);
    out.anchor_id = token.anchor_id;
    out.start_ns = token.start_ns;
    out.end_ns = token.end_ns;
    tokens.push_back(std::move(out));
  }
  return tokens;
}

void add_error(AnchorInternalCostBreakdown& breakdown,
               std::string code,
               std::string message) {
  breakdown.diagnostics.push_back(
      Diagnostic{DiagnosticSeverity::kError, std::move(code),
                 std::move(message)});
}

}  // namespace

AclGraphFixtureAdapter::AclGraphFixtureAdapter(
    AclGraphSemanticFixture fixture)
    : fixture_(std::move(fixture)) {}

AnchorKind aclgraph_anchor_kind_for_slot_symbol(const std::string& slot_symbol) {
  if (slot_symbol == "H") {
    return AnchorKind::kGraphH;
  }
  if (slot_symbol == "L") {
    return AnchorKind::kGraphL;
  }
  if (slot_symbol == "T") {
    return AnchorKind::kGraphT;
  }
  return AnchorKind::kUnknown;
}

NativeIr AclGraphFixtureAdapter::load() const {
  NativeIr ir;
  const SourceRefId source_ref = ir.source_refs.append(
      "aclgraph_semantic_fixture", fixture_.fixture_id, "aclgraph_fixture", 0);
  const SourceRefId graph_replay_source_ref = ir.source_refs.append(
      "aclgraph_semantic_fixture", fixture_.fixture_id,
      "ACLGRAPH_REPLAY_UNIT", 0);
  const SourceRefId graph_subslot_source_ref = ir.source_refs.append(
      "aclgraph_semantic_fixture", fixture_.fixture_id,
      "ACLGRAPH_REPLAY_SUBSLOT", 0);

  const GraphTemplateId graph_template = ir.graph_templates.append(
      source_ref, 0,
      static_cast<std::uint32_t>(fixture_.capture_dictionary.size()));

  for (const AclGraphCaptureSlotFixtureRow& slot : fixture_.capture_slots) {
    const RoleId role_id = role_id_for_slot_symbol(slot.slot_symbol);
    const SymbolId symbol_id = ir.symbols.intern(slot.slot_symbol);
    ir.capture_slots.append(
        graph_template, source_ref, slot.capture_slot_idx, role_id, symbol_id,
        TraceEventId::invalid(), TraceEventId::invalid());
  }

  const SymbolId replay_symbol = ir.symbols.intern("ACLGraphReplay");
  std::vector<TraceEventId> replay_launch_events;
  replay_launch_events.reserve(fixture_.replay_units.size());
  for (std::size_t index = 0; index < fixture_.replay_units.size(); ++index) {
    const AclGraphReplayUnitFixtureRow& unit = fixture_.replay_units[index];
    replay_launch_events.push_back(ir.trace_events.append(
        graph_replay_source_ref, index, 0, 0, unit.start_ns, unit.end_ns,
        replay_symbol));
  }

  std::map<std::string, TraceEventId> event_by_subslot;
  for (std::size_t index = 0; index < fixture_.replay_subslots.size();
       ++index) {
    const AclGraphReplaySubslotFixtureRow& subslot =
        fixture_.replay_subslots[index];
    const TraceEventId event_id = ir.trace_events.append(
        graph_subslot_source_ref, index, 0, subslot.stream_id,
        subslot.start_ns, subslot.end_ns,
        ir.symbols.intern(subslot.slot_symbol));
    if (!event_by_subslot.emplace(subslot.subslot_id, event_id).second) {
      throw std::invalid_argument(
          "ACLGraph replay subslots contain a duplicate id");
    }
  }

  std::map<std::string, ReplayUnitId> replay_unit_ids;
  for (std::size_t index = 0; index < fixture_.replay_units.size(); ++index) {
    const AclGraphReplayUnitFixtureRow& unit = fixture_.replay_units[index];
    const ReplayUnitId replay_unit_id = ir.replay_units.append(
        graph_template, graph_replay_source_ref, AnchorId::invalid(),
        AnchorId::invalid(), replay_launch_events[index]);
    replay_unit_ids.emplace(unit.replay_unit_id, replay_unit_id);
  }

  std::map<std::string, AnchorSpan> replay_unit_spans;
  std::uint64_t next_synthetic_subslot_source_row =
      fixture_.replay_subslots.size();
  for (std::size_t index = 0; index < fixture_.hlt_anchor_seeds.size();
       ++index) {
    const AclGraphHltAnchorSeedFixtureRow& seed =
        fixture_.hlt_anchor_seeds[index];
    const auto replay_unit_found = replay_unit_ids.find(seed.replay_unit_id);
    if (replay_unit_found == replay_unit_ids.end()) {
      throw std::invalid_argument(
          "HLT anchor seed references unknown replay unit");
    }
    TraceEventId anchor_event_id;
    if (seed.subslot_id.empty()) {
      // Some historical fixtures predate explicit replay-subslot rows.  Each
      // seed still describes a distinct observed interval, so preserve that
      // one-to-one evidence identity instead of coalescing empty subslot IDs.
      anchor_event_id = ir.trace_events.append(
          graph_subslot_source_ref, next_synthetic_subslot_source_row++, 0, 0,
          seed.start_ns, seed.end_ns, ir.symbols.intern(seed.symbol));
    } else {
      auto event_found = event_by_subslot.find(seed.subslot_id);
      if (event_found == event_by_subslot.end()) {
        const TraceEventId event_id = ir.trace_events.append(
            graph_subslot_source_ref, next_synthetic_subslot_source_row++, 0,
            0, seed.start_ns, seed.end_ns,
            ir.symbols.intern(seed.symbol));
        event_found =
            event_by_subslot.emplace(seed.subslot_id, event_id).first;
      }
      anchor_event_id = event_found->second;
    }

    const SymbolId symbol_id = ir.symbols.intern(seed.symbol);
    const TraceEventRow& anchor_event =
        ir.trace_events.row(anchor_event_id);
    const AnchorId anchor_id = ir.anchors.append(
        graph_subslot_source_ref, anchor_event_id,
        replay_unit_found->second,
        aclgraph_anchor_kind_for_slot_symbol(seed.slot_symbol), symbol_id, 0,
        static_cast<std::uint32_t>(anchor_event.stream_id), seed.start_ns,
        seed.end_ns);
    const TokenId token_id = ir.tokens.append(
        anchor_id, symbol_id, 0, static_cast<std::uint32_t>(index),
        seed.start_ns, seed.end_ns);

    AnchorSpan& span = replay_unit_spans[seed.replay_unit_id];
    if (!span.valid) {
      span.first_anchor_id = anchor_id;
      span.last_anchor_id = anchor_id;
      span.first_token_id = token_id;
      span.last_token_id = token_id;
      span.valid = true;
    } else {
      span.last_anchor_id = anchor_id;
      span.last_token_id = token_id;
    }
  }

  for (const auto& entry : replay_unit_spans) {
    const ReplayUnitId replay_unit_id = replay_unit_ids.at(entry.first);
    const AnchorSpan& span = entry.second;
    ir.replay_units.set_anchor_bounds(replay_unit_id, span.first_anchor_id,
                                      span.last_anchor_id);
    ir.protected_intervals.append(
        ProtectedIntervalKind::kGraphReplayUnit, BoundaryPolicy::kNoCross,
        span.first_token_id, span.last_token_id, span.first_anchor_id,
        span.last_anchor_id, source_ref);
  }

  return ir;
}

AnchorInternalCostBreakdown build_aclgraph_fixture_anchor_cost_breakdown(
    const AclGraphSemanticFixture& fixture,
    const NativeIr& ir) {
  AnchorInternalCostBreakdown breakdown;
  if (fixture.hlt_anchor_seeds.size() != ir.tokens.size()) {
    add_error(breakdown, "aclgraph_fixture_cost_token_seed_mismatch",
              "ACLGraph fixture HLT anchor seed count must match NativeIr "
              "token count before building fixture cost breakdown");
    return breakdown;
  }

  std::vector<AnchorGraphChildSummary> summaries;
  summaries.reserve(fixture.hlt_anchor_seeds.size());
  for (std::size_t index = 0; index < fixture.hlt_anchor_seeds.size();
       ++index) {
    const AclGraphHltAnchorSeedFixtureRow& seed =
        fixture.hlt_anchor_seeds[index];
    // Semantic fixtures carry already-aggregated graph-child evidence. Until a
    // real DB adapter can scan raw tasks, use the slot window as the stable
    // golden-sample cost carrier.
    summaries.push_back(AnchorGraphChildSummary{
        static_cast<std::uint32_t>(index),
        seed.start_ns,
        seed.end_ns,
        seed.end_ns - seed.start_ns,
        seed.raw_child_task_count,
        0,
        seed.raw_top_ops,
        "",
    });
  }

  const AnchorGraphChildCostResult graph_cost =
      build_anchor_graph_child_summary_components(summaries);
  const std::vector<StructuralProjectionToken> structural_tokens = structural_tokens_from_ir(ir);
  const StructuralOccurrenceGraph tree =
      build_structural_occurrence_graph_from_tokens(structural_tokens);
  breakdown = build_anchor_internal_cost_breakdown(
      tree, structural_tokens, graph_cost.component_leaves);
  breakdown.diagnostics.insert(breakdown.diagnostics.begin(),
                               graph_cost.diagnostics.begin(),
                               graph_cost.diagnostics.end());
  return breakdown;
}

}  // namespace traceloom
