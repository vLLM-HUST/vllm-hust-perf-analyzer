#pragma once

#include <unordered_map>
#include <vector>

#include "traceloom/ir/native_ir.h"

namespace traceloom {

// Exact replays own their observed body and matched execution/completion
// controls, not every event inside a multi-launch composition's envelope.
// Legacy opaque replays retain their existing temporal coverage contract.
class ReplayEventMembershipIndex {
 public:
  explicit ReplayEventMembershipIndex(const NativeIr& ir);
  std::vector<ReplayUnitId> memberships(const TraceEventRow& event) const;
  bool contains(const TraceEventRow& event) const;

 private:
  struct LegacySpan {
    ReplayUnitId replay;
    std::int64_t start, end;
  };
  std::unordered_map<TraceEventId::value_type, std::vector<ReplayUnitId>> exact_;
  std::unordered_map<std::uint32_t, std::vector<LegacySpan>> legacy_;
};

}  // namespace traceloom
