#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

struct GraphTemplateRow {
  GraphTemplateId id;
  SourceRefId source_ref_id;
  std::uint64_t body_sequence_hash = 0;
  std::uint32_t slot_count = 0;
};

class GraphTemplateTable {
 public:
  GraphTemplateId append(SourceRefId source_ref_id,
                         std::uint64_t body_sequence_hash,
                         std::uint32_t slot_count);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const GraphTemplateRow& row(GraphTemplateId id) const;
  const std::vector<GraphTemplateRow>& rows() const noexcept { return rows_; }

 private:
  std::vector<GraphTemplateRow> rows_;
};

}  // namespace traceloom
