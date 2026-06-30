#include "traceloom/ir/token_table.h"

#include <stdexcept>

namespace traceloom {

TokenId TokenTable::append(AnchorId anchor_id,
                           SymbolId symbol_id,
                           std::uint32_t device_id,
                           std::uint32_t sequence_index,
                           std::int64_t start_ns,
                           std::int64_t end_ns) {
  const auto id = checked_next_id<TokenId>(rows_.size());
  rows_.push_back(
      TokenRow{id, anchor_id, symbol_id, device_id, sequence_index, start_ns,
               end_ns});
  return id;
}

const TokenRow& TokenTable::row(TokenId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("TokenId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
