#pragma once

#include <cstdint>
#include <vector>

#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/core/ids.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom {

// Identifies normalized observations whose intervals may participate in the
// auxiliary/transition cost lens when they do not own an anchor.  Structural
// identity, raw evidence retention, and cost attribution are separate
// decisions: an observation can remain fully queryable while being absent from
// this mask.
class EventCostAttributionMask {
 public:
  bool includes(TraceEventId event_id) const;
  std::size_t size() const noexcept { return included_.size(); }

 private:
  friend EventCostAttributionMask build_event_cost_attribution_mask(
      const NativeIr&, FlatAnchorBuildConfig);
  std::vector<std::uint8_t> included_;
};

// Uses the same effective FlatAnchorBuildConfig as anchor construction.  In
// particular, communication/replay replacement never reappears as auxiliary
// cost, and retained_as_evidence observations remain auditable without entering
// an additive or scheduled-cost attribution.
EventCostAttributionMask build_event_cost_attribution_mask(
    const NativeIr& ir,
    FlatAnchorBuildConfig config = FlatAnchorBuildConfig{});

}  // namespace traceloom

