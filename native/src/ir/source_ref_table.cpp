#include "traceloom/ir/source_ref_table.h"

#include <stdexcept>
#include <utility>

namespace traceloom {

SourceRefId SourceRefTable::append(std::string source_kind,
                                   std::string source_path,
                                   std::string table_name,
                                   std::uint64_t row_id) {
  const auto id = checked_next_id<SourceRefId>(rows_.size());
  rows_.push_back(SourceRefRow{id, std::move(source_kind), std::move(source_path),
                               std::move(table_name), row_id});
  return id;
}

const SourceRefRow& SourceRefTable::row(SourceRefId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("SourceRefId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
