#include "traceloom/ir/graph_launch_activity_table.h"

#include <stdexcept>

namespace traceloom {

GraphLaunchActivityId GraphLaunchActivityTable::append(
    SourceRefId source_ref_id,
    std::uint64_t raw_global_tid,
    std::int64_t first_host_api_row_id,
    std::int64_t last_host_api_row_id,
    std::int64_t boundary_host_api_row_id,
    SymbolId boundary_api_symbol_id,
    std::int64_t start_ns,
    std::int64_t end_ns,
    std::uint32_t host_execute_count,
    std::uint32_t matched_launch_count,
    GraphLaunchActivityBoundaryPolicy boundary_policy) {
  const auto id = checked_next_id<GraphLaunchActivityId>(rows_.size());
  rows_.push_back(GraphLaunchActivityRow{
      id, source_ref_id, raw_global_tid, first_host_api_row_id,
      last_host_api_row_id, boundary_host_api_row_id, boundary_api_symbol_id,
      start_ns, end_ns, host_execute_count, matched_launch_count,
      boundary_policy});
  return id;
}

const GraphLaunchActivityRow& GraphLaunchActivityTable::row(
    GraphLaunchActivityId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("GraphLaunchActivityId is out of range");
  }
  return rows_[id.value()];
}

GraphLaunchActivityMemberId GraphLaunchActivityMemberTable::append(
    GraphLaunchActivityId graph_launch_activity_id,
    GraphLaunchOccurrenceId graph_launch_occurrence_id,
    std::uint32_t host_execute_order) {
  const auto id =
      checked_next_id<GraphLaunchActivityMemberId>(rows_.size());
  rows_.push_back(GraphLaunchActivityMemberRow{
      id, graph_launch_activity_id, graph_launch_occurrence_id,
      host_execute_order});
  return id;
}

const GraphLaunchActivityMemberRow& GraphLaunchActivityMemberTable::row(
    GraphLaunchActivityMemberId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("GraphLaunchActivityMemberId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
