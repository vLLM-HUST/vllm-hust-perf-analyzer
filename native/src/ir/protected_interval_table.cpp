#include "traceloom/ir/protected_interval_table.h"

#include <stdexcept>

namespace traceloom {

ProtectedIntervalId ProtectedIntervalTable::append(
    ProtectedIntervalKind kind,
    BoundaryPolicy boundary_policy,
    TokenId first_token_id,
    TokenId last_token_id,
    AnchorId first_anchor_id,
    AnchorId last_anchor_id,
    SourceRefId evidence_source_ref_id) {
  const auto id = checked_next_id<ProtectedIntervalId>(rows_.size());
  rows_.push_back(ProtectedIntervalRow{id, kind, boundary_policy,
                                       first_token_id, last_token_id,
                                       first_anchor_id, last_anchor_id,
                                       evidence_source_ref_id});
  return id;
}

const ProtectedIntervalRow& ProtectedIntervalTable::row(
    ProtectedIntervalId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("ProtectedIntervalId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
