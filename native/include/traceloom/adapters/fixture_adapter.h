#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/adapters/source_adapter.h"
#include "traceloom/ir/anchor_table.h"
#include "traceloom/ir/protected_interval_table.h"

namespace traceloom {

struct FixtureToken {
  std::string symbol;
  AnchorKind anchor_kind = AnchorKind::kDeviceEvent;
  std::uint32_t device_id = 0;
  std::uint32_t stream_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

struct FixtureProtectedInterval {
  ProtectedIntervalKind kind = ProtectedIntervalKind::kGraphReplayUnit;
  BoundaryPolicy boundary_policy = BoundaryPolicy::kNoCross;
  std::size_t first_token_index = 0;
  std::size_t last_token_index = 0;
};

struct FixtureInput {
  std::string source_kind = "fixture";
  std::string source_path = "memory";
  std::vector<FixtureToken> tokens;
  std::vector<FixtureProtectedInterval> protected_intervals;
};

class FixtureAdapter final : public SourceAdapter {
 public:
  explicit FixtureAdapter(FixtureInput input);

  NativeIr load() const override;

 private:
  FixtureInput input_;
};

}  // namespace traceloom
