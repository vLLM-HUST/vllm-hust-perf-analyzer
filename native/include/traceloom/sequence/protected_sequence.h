#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "traceloom/core/ids.h"
#include "traceloom/ir/token_table.h"

namespace traceloom {

struct ProtectedSequenceToken {
  TokenId token_id;
  AnchorId anchor_id;
  SymbolId symbol_id;
  std::uint32_t device_id = 0;
  std::uint32_t sequence_index = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

class ProtectedSequence {
 public:
  static ProtectedSequence from_token_table(const TokenTable& tokens);

  std::size_t size() const noexcept { return tokens_.size(); }
  bool empty() const noexcept { return tokens_.empty(); }
  const ProtectedSequenceToken& token_at(std::size_t index) const;
  std::size_t index_of(TokenId token_id) const;
  const std::vector<ProtectedSequenceToken>& tokens() const noexcept {
    return tokens_;
  }

 private:
  std::vector<ProtectedSequenceToken> tokens_;
  std::vector<std::size_t> token_index_by_id_;
};

}  // namespace traceloom
