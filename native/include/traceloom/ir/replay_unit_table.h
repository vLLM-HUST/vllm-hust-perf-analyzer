#pragma once

#include <cstddef>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

struct ReplayUnitRow {
  ReplayUnitId id;
  GraphTemplateId graph_template_id;
  SourceRefId source_ref_id;
  AnchorId first_anchor_id;
  AnchorId last_anchor_id;
  TraceEventId launch_trace_event_id;
};

class ReplayUnitTable {
 public:
  ReplayUnitId append(GraphTemplateId graph_template_id,
                      SourceRefId source_ref_id,
                      AnchorId first_anchor_id,
                      AnchorId last_anchor_id,
                      TraceEventId launch_trace_event_id);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const ReplayUnitRow& row(ReplayUnitId id) const;
  const std::vector<ReplayUnitRow>& rows() const noexcept { return rows_; }

 private:
  std::vector<ReplayUnitRow> rows_;
};

}  // namespace traceloom
