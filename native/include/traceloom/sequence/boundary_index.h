#pragma once

#include <cstddef>
#include <vector>

#include "traceloom/core/ids.h"
#include "traceloom/ir/protected_interval_table.h"
#include "traceloom/sequence/protected_sequence.h"

namespace traceloom {

struct ProtectedIntervalSpan {
  ProtectedIntervalId id;
  std::size_t first_token_index = 0;
  std::size_t last_token_index = 0;
  ProtectedIntervalKind kind = ProtectedIntervalKind::kUnknown;
  BoundaryPolicy boundary_policy = BoundaryPolicy::kNoCross;
};

class BoundaryIndex {
 public:
  static BoundaryIndex build(const ProtectedSequence& sequence,
                             const ProtectedIntervalTable& intervals);

  std::size_t interval_count() const noexcept { return intervals_.size(); }
  const std::vector<ProtectedIntervalSpan>& intervals() const noexcept {
    return intervals_;
  }

  std::vector<ProtectedIntervalId> intervals_covering(
      std::size_t token_index) const;

  ProtectedIntervalId first_no_cross_violation(std::size_t begin,
                                               std::size_t end) const;

  bool violates_no_cross_interval(std::size_t begin,
                                  std::size_t end) const;

 private:
  std::vector<ProtectedIntervalSpan> intervals_;
};

}  // namespace traceloom
