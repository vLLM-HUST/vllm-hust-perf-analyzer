#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

struct CaptureSlotRow {
  CaptureSlotId id;
  GraphTemplateId graph_template_id;
  SourceRefId source_ref_id;
  std::uint32_t slot_order = 0;
  RoleId slot_role_id;
  SymbolId slot_symbol_id;
  TraceEventId first_event_id;
  TraceEventId last_event_id;
};

class CaptureSlotTable {
 public:
  CaptureSlotId append(GraphTemplateId graph_template_id,
                       SourceRefId source_ref_id,
                       std::uint32_t slot_order,
                       RoleId slot_role_id,
                       SymbolId slot_symbol_id,
                       TraceEventId first_event_id,
                       TraceEventId last_event_id);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const CaptureSlotRow& row(CaptureSlotId id) const;
  const std::vector<CaptureSlotRow>& rows() const noexcept { return rows_; }

 private:
  std::vector<CaptureSlotRow> rows_;
};

}  // namespace traceloom
