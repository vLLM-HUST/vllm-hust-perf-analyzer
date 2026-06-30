#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

class StringTable {
 public:
  StringId intern(const std::string& value);
  StringId intern(std::string&& value);

  const std::string& value(StringId id) const;
  std::size_t size() const noexcept { return values_.size(); }
  bool empty() const noexcept { return values_.empty(); }

 private:
  std::vector<std::string> values_;
  std::unordered_map<std::string, StringId::value_type> index_;
};

class SymbolTable {
 public:
  SymbolId intern(const std::string& value);
  SymbolId intern(std::string&& value);

  const std::string& value(SymbolId id) const;
  std::size_t size() const noexcept { return values_.size(); }
  bool empty() const noexcept { return values_.empty(); }

 private:
  std::vector<std::string> values_;
  std::unordered_map<std::string, SymbolId::value_type> index_;
};

}  // namespace traceloom
