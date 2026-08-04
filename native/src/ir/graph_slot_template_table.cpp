#include "traceloom/ir/graph_slot_template_table.h"

#include <stdexcept>

namespace traceloom {

GraphSlotTemplateId GraphSlotTemplateTable::append(
    SourceRefId source_ref_id,
    std::uint64_t body_sequence_hash,
    SymbolId body_signature_symbol_id) {
  const auto id = checked_next_id<GraphSlotTemplateId>(rows_.size());
  rows_.push_back(GraphSlotTemplateRow{
      id, source_ref_id, body_sequence_hash, body_signature_symbol_id});
  return id;
}

const GraphSlotTemplateRow& GraphSlotTemplateTable::row(
    GraphSlotTemplateId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("GraphSlotTemplateId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
