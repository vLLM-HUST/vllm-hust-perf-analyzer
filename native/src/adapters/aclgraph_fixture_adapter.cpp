#include "traceloom/adapters/aclgraph_fixture_adapter.h"

#include <algorithm>
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

  std::map<std::string, ReplayUnitId> replay_unit_ids;
  for (const AclGraphReplayUnitFixtureRow& unit : fixture_.replay_units) {
    const ReplayUnitId replay_unit_id = ir.replay_units.append(
        graph_template, source_ref, AnchorId::invalid(), AnchorId::invalid(),
        TraceEventId::invalid());
    replay_unit_ids.emplace(unit.replay_unit_id, replay_unit_id);
  }

  std::map<std::string, AnchorSpan> replay_unit_spans;
  for (std::size_t index = 0; index < fixture_.hlt_anchor_seeds.size();
       ++index) {
    const AclGraphHltAnchorSeedFixtureRow& seed =
        fixture_.hlt_anchor_seeds[index];
    const auto replay_unit_found = replay_unit_ids.find(seed.replay_unit_id);
    if (replay_unit_found == replay_unit_ids.end()) {
      throw std::invalid_argument(
          "HLT anchor seed references unknown replay unit");
    }

    const SymbolId symbol_id = ir.symbols.intern(seed.symbol);
    const AnchorId anchor_id = ir.anchors.append(
        source_ref, TraceEventId::invalid(), replay_unit_found->second,
        aclgraph_anchor_kind_for_slot_symbol(seed.slot_symbol), symbol_id, 0, 0,
        seed.start_ns, seed.end_ns);
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

}  // namespace traceloom
