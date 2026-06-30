#include "traceloom/core/string_table.h"

#include <stdexcept>
#include <utility>

namespace traceloom {

StringId StringTable::intern(const std::string& value) {
  const auto found = index_.find(value);
  if (found != index_.end()) {
    return StringId(found->second);
  }
  const auto id = checked_next_id<StringId>(values_.size());
  values_.push_back(value);
  index_.emplace(values_.back(), id.value());
  return id;
}

StringId StringTable::intern(std::string&& value) {
  const auto found = index_.find(value);
  if (found != index_.end()) {
    return StringId(found->second);
  }
  const auto id = checked_next_id<StringId>(values_.size());
  values_.push_back(std::move(value));
  index_.emplace(values_.back(), id.value());
  return id;
}

const std::string& StringTable::value(StringId id) const {
  if (!id.valid() || id.value() >= values_.size()) {
    throw std::out_of_range("StringId is out of range");
  }
  return values_[id.value()];
}

SymbolId SymbolTable::intern(const std::string& value) {
  const auto found = index_.find(value);
  if (found != index_.end()) {
    return SymbolId(found->second);
  }
  const auto id = checked_next_id<SymbolId>(values_.size());
  values_.push_back(value);
  index_.emplace(values_.back(), id.value());
  return id;
}

SymbolId SymbolTable::intern(std::string&& value) {
  const auto found = index_.find(value);
  if (found != index_.end()) {
    return SymbolId(found->second);
  }
  const auto id = checked_next_id<SymbolId>(values_.size());
  values_.push_back(std::move(value));
  index_.emplace(values_.back(), id.value());
  return id;
}

const std::string& SymbolTable::value(SymbolId id) const {
  if (!id.valid() || id.value() >= values_.size()) {
    throw std::out_of_range("SymbolId is out of range");
  }
  return values_[id.value()];
}

}  // namespace traceloom
