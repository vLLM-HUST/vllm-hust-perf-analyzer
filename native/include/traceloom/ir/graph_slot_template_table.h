#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

struct GraphSlotTemplateRow {
  GraphSlotTemplateId id;
  SourceRefId source_ref_id;
  std::uint64_t body_sequence_hash = 0;
  SymbolId body_signature_symbol_id;
};

class GraphSlotTemplateTable {
 public:
  GraphSlotTemplateId append(SourceRefId source_ref_id,
                             std::uint64_t body_sequence_hash,
                             SymbolId body_signature_symbol_id);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const GraphSlotTemplateRow& row(GraphSlotTemplateId id) const;
  const std::vector<GraphSlotTemplateRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<GraphSlotTemplateRow> rows_;
};

}  // namespace traceloom
