#include "traceloom/sequence/protected_sequence.h"

#include <limits>
#include <stdexcept>

namespace traceloom {

ProtectedSequence ProtectedSequence::from_token_table(const TokenTable& tokens) {
  ProtectedSequence sequence;
  sequence.tokens_.reserve(tokens.size());
  for (const TokenRow& row : tokens.rows()) {
    if (row.sequence_index != sequence.tokens_.size()) {
      throw std::invalid_argument(
          "TokenTable sequence_index must be contiguous and zero-based");
    }
    sequence.tokens_.push_back(ProtectedSequenceToken{
        row.id, row.anchor_id, row.symbol_id, row.device_id,
        row.sequence_index, row.start_ns, row.end_ns});
  }
  sequence.token_index_by_id_.assign(
      sequence.tokens_.size(), std::numeric_limits<std::size_t>::max());
  for (std::size_t index = 0; index < sequence.tokens_.size(); ++index) {
    const TokenId token_id = sequence.tokens_[index].token_id;
    if (!token_id.valid() || token_id.value() >= sequence.token_index_by_id_.size()) {
      throw std::invalid_argument("TokenId must be dense inside ProtectedSequence");
    }
    sequence.token_index_by_id_[token_id.value()] = index;
  }
  return sequence;
}

const ProtectedSequenceToken& ProtectedSequence::token_at(
    std::size_t index) const {
  if (index >= tokens_.size()) {
    throw std::out_of_range("ProtectedSequence token index is out of range");
  }
  return tokens_[index];
}

std::size_t ProtectedSequence::index_of(TokenId token_id) const {
  if (!token_id.valid() || token_id.value() >= token_index_by_id_.size()) {
    throw std::out_of_range("TokenId is not present in ProtectedSequence");
  }
  const std::size_t index = token_index_by_id_[token_id.value()];
  if (index == std::numeric_limits<std::size_t>::max()) {
    throw std::out_of_range("TokenId is not present in ProtectedSequence");
  }
  return index;
}

}  // namespace traceloom
