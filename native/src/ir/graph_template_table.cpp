#include "traceloom/ir/graph_template_table.h"

#include <stdexcept>

namespace traceloom {

GraphTemplateId GraphTemplateTable::append(SourceRefId source_ref_id,
                                           std::uint64_t body_sequence_hash,
                                           std::uint32_t slot_count) {
  const auto id = checked_next_id<GraphTemplateId>(rows_.size());
  rows_.push_back(
      GraphTemplateRow{id, source_ref_id, body_sequence_hash, slot_count});
  return id;
}

const GraphTemplateRow& GraphTemplateTable::row(GraphTemplateId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("GraphTemplateId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
