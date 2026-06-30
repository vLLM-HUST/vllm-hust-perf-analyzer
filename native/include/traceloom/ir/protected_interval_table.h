#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

enum class ProtectedIntervalKind {
  kUnknown,
  kGraphReplayUnit,
  kUserWindow,
};

enum class BoundaryPolicy {
  kNoCross,
  kAllowEnclosing,
};

struct ProtectedIntervalRow {
  ProtectedIntervalId id;
  ProtectedIntervalKind kind = ProtectedIntervalKind::kUnknown;
  BoundaryPolicy boundary_policy = BoundaryPolicy::kNoCross;
  TokenId first_token_id;
  TokenId last_token_id;
  AnchorId first_anchor_id;
  AnchorId last_anchor_id;
  SourceRefId evidence_source_ref_id;
};

class ProtectedIntervalTable {
 public:
  ProtectedIntervalId append(ProtectedIntervalKind kind,
                             BoundaryPolicy boundary_policy,
                             TokenId first_token_id,
                             TokenId last_token_id,
                             AnchorId first_anchor_id,
                             AnchorId last_anchor_id,
                             SourceRefId evidence_source_ref_id);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const ProtectedIntervalRow& row(ProtectedIntervalId id) const;
  const std::vector<ProtectedIntervalRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<ProtectedIntervalRow> rows_;
};

}  // namespace traceloom
