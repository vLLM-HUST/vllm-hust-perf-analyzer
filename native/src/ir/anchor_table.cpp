#include "traceloom/ir/anchor_table.h"

#include <stdexcept>

namespace traceloom {

AnchorId AnchorTable::append(SourceRefId source_ref_id,
                             TraceEventId trace_event_id,
                             ReplayUnitId replay_unit_id,
                             AnchorKind kind,
                             SymbolId symbol_id,
                             std::uint32_t device_id,
                             std::uint32_t stream_id,
                             std::int64_t start_ns,
                             std::int64_t end_ns,
                             ReplayUnitLaunchMemberId replay_unit_launch_member_id,
                             StructuralSymbolDecision symbol_decision) {
  const auto id = checked_next_id<AnchorId>(rows_.size());
  if (!symbol_decision.observed_symbol_id.valid() && symbol_id.valid() &&
      symbol_decision.outcome == StructuralSymbolOutcome::kUnsupported) {
    symbol_decision.observed_symbol_id = symbol_id;
    symbol_decision.rule_id = "fallback.identity-preserve";
    symbol_decision.outcome = StructuralSymbolOutcome::kIdentity;
  }
  rows_.push_back(AnchorRow{id, source_ref_id, trace_event_id, replay_unit_id,
                            replay_unit_launch_member_id, kind, symbol_id,
                            symbol_decision, device_id, stream_id, start_ns,
                            end_ns});
  return id;
}

const AnchorRow& AnchorTable::row(AnchorId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("AnchorId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
