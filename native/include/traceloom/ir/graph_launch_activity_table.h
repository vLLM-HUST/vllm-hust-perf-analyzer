#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

enum class GraphLaunchActivityBoundaryPolicy : std::uint8_t {
  kHostBlockingSync = 0,
  kHostThreadTail = 1,
};

struct GraphLaunchActivityRow {
  GraphLaunchActivityId id;
  SourceRefId source_ref_id;
  std::uint64_t raw_global_tid = 0;
  std::int64_t first_host_api_row_id = -1;
  std::int64_t last_host_api_row_id = -1;
  std::int64_t boundary_host_api_row_id = -1;
  SymbolId boundary_api_symbol_id;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::uint32_t host_execute_count = 0;
  std::uint32_t matched_launch_count = 0;
  GraphLaunchActivityBoundaryPolicy boundary_policy =
      GraphLaunchActivityBoundaryPolicy::kHostThreadTail;
};

class GraphLaunchActivityTable {
 public:
  GraphLaunchActivityId append(
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
      GraphLaunchActivityBoundaryPolicy boundary_policy);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const GraphLaunchActivityRow& row(GraphLaunchActivityId id) const;
  const std::vector<GraphLaunchActivityRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<GraphLaunchActivityRow> rows_;
};

struct GraphLaunchActivityMemberRow {
  GraphLaunchActivityMemberId id;
  GraphLaunchActivityId graph_launch_activity_id;
  GraphLaunchOccurrenceId graph_launch_occurrence_id;
  std::uint32_t host_execute_order = 0;
};

class GraphLaunchActivityMemberTable {
 public:
  GraphLaunchActivityMemberId append(
      GraphLaunchActivityId graph_launch_activity_id,
      GraphLaunchOccurrenceId graph_launch_occurrence_id,
      std::uint32_t host_execute_order);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const GraphLaunchActivityMemberRow& row(
      GraphLaunchActivityMemberId id) const;
  const std::vector<GraphLaunchActivityMemberRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<GraphLaunchActivityMemberRow> rows_;
};

}  // namespace traceloom
