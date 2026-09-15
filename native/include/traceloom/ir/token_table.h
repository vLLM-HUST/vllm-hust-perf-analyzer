#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

struct TokenRow {
  TokenId id;
  AnchorId anchor_id;
  SymbolId symbol_id;
  std::uint32_t device_id = 0;
  std::uint32_t sequence_index = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  // Structural ordering evidence, not a device-time or causal coordinate.
  RuntimeCallId order_runtime_call_id = RuntimeCallId::invalid();
};

class TokenTable {
 public:
  TokenId append(AnchorId anchor_id,
                 SymbolId symbol_id,
                 std::uint32_t device_id,
                 std::uint32_t sequence_index,
                 std::int64_t start_ns,
                 std::int64_t end_ns,
                 RuntimeCallId order_runtime_call_id = RuntimeCallId::invalid());

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const TokenRow& row(TokenId id) const;
  const std::vector<TokenRow>& rows() const noexcept { return rows_; }

 private:
  std::vector<TokenRow> rows_;
};

}  // namespace traceloom
