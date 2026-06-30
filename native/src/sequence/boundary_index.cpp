#include "traceloom/sequence/boundary_index.h"

#include <algorithm>
#include <stdexcept>

namespace traceloom {

namespace {

}  // namespace

BoundaryIndex BoundaryIndex::build(const ProtectedSequence& sequence,
                                   const ProtectedIntervalTable& intervals) {
  BoundaryIndex index;
  index.intervals_.reserve(intervals.size());
  for (const ProtectedIntervalRow& row : intervals.rows()) {
    const std::size_t first = sequence.index_of(row.first_token_id);
    const std::size_t last = sequence.index_of(row.last_token_id);
    if (first > last) {
      throw std::invalid_argument(
          "Protected interval first token is after last token");
    }
    index.intervals_.push_back(ProtectedIntervalSpan{
        row.id, first, last, row.kind, row.boundary_policy});
  }

  std::sort(index.intervals_.begin(), index.intervals_.end(),
            [](const ProtectedIntervalSpan& lhs,
               const ProtectedIntervalSpan& rhs) {
              if (lhs.first_token_index != rhs.first_token_index) {
                return lhs.first_token_index < rhs.first_token_index;
              }
              if (lhs.last_token_index != rhs.last_token_index) {
                return lhs.last_token_index < rhs.last_token_index;
              }
              return lhs.id < rhs.id;
            });
  return index;
}

std::vector<ProtectedIntervalId> BoundaryIndex::intervals_covering(
    std::size_t token_index) const {
  std::vector<ProtectedIntervalId> result;
  for (const ProtectedIntervalSpan& interval : intervals_) {
    if (token_index >= interval.first_token_index &&
        token_index <= interval.last_token_index) {
      result.push_back(interval.id);
    }
  }
  return result;
}

ProtectedIntervalId BoundaryIndex::first_no_cross_violation(
    std::size_t begin,
    std::size_t end) const {
  const BoundaryViolation violation = first_violation(begin, end);
  if (violation.kind == BoundaryViolationKind::kCrossesNoCrossBoundary ||
      violation.kind == BoundaryViolationKind::kEnclosesNoCrossInterval) {
    return violation.protected_interval_id;
  }
  return ProtectedIntervalId::invalid();
}

BoundaryViolation BoundaryIndex::first_violation(std::size_t begin,
                                                 std::size_t end) const {
  if (begin >= end) {
    return BoundaryViolation{};
  }
  const std::size_t last = end - 1;
  for (const ProtectedIntervalSpan& interval : intervals_) {
    const bool overlaps = begin <= interval.last_token_index &&
                          last >= interval.first_token_index;
    if (!overlaps) {
      continue;
    }

    if (interval.boundary_policy == BoundaryPolicy::kBlockAnyOverlap) {
      return BoundaryViolation{
          BoundaryViolationKind::kAmbiguousIntervalBlocksCandidate,
          interval.id};
    }

    const bool exact_cover = begin == interval.first_token_index &&
                             last == interval.last_token_index;
    const bool inside_interval = begin >= interval.first_token_index &&
                                 last <= interval.last_token_index;
    const bool strictly_encloses = begin < interval.first_token_index &&
                                   last > interval.last_token_index;

    if (inside_interval || exact_cover) {
      continue;
    }
    if (strictly_encloses &&
        interval.boundary_policy == BoundaryPolicy::kNoCross) {
      return BoundaryViolation{
          BoundaryViolationKind::kEnclosesNoCrossInterval, interval.id};
    }
    if (!strictly_encloses &&
        (interval.boundary_policy == BoundaryPolicy::kNoCross ||
         interval.boundary_policy == BoundaryPolicy::kAllowEnclosing)) {
      return BoundaryViolation{
          BoundaryViolationKind::kCrossesNoCrossBoundary, interval.id};
    }
  }
  return BoundaryViolation{};
}

bool BoundaryIndex::violates_no_cross_interval(std::size_t begin,
                                               std::size_t end) const {
  return first_no_cross_violation(begin, end).valid();
}

}  // namespace traceloom
