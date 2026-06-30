#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

struct SourceRefRow {
  SourceRefId id;
  std::string source_kind;
  std::string source_path;
  std::string table_name;
  std::uint64_t row_id = 0;
};

class SourceRefTable {
 public:
  SourceRefId append(std::string source_kind,
                     std::string source_path,
                     std::string table_name,
                     std::uint64_t row_id);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const SourceRefRow& row(SourceRefId id) const;
  const std::vector<SourceRefRow>& rows() const noexcept { return rows_; }

 private:
  std::vector<SourceRefRow> rows_;
};

}  // namespace traceloom
