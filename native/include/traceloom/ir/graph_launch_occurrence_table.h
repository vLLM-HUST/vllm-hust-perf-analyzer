#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

enum class GraphLaunchMatchPolicy : std::uint8_t {
  kUnmatched = 0,
  kNotifyCompletionAdjacent = 1,
  kNotifyOrderedFallback = 2,
  kCudaRuntimeCorrelation = 3,
};

enum class GraphLaunchInstanceAssociationPolicy : std::uint8_t {
  kNone = 0,
  kRecordModelId = 1,
  kRecordModelStream = 2,
  kCudaGraphNodeSet = 3,
};

struct GraphLaunchOccurrenceRow {
  GraphLaunchOccurrenceId id;
  SourceRefId source_ref_id;
  SourceRefId host_api_source_ref_id;
  std::uint32_t device_id = 0;
  std::int64_t raw_host_api_row_id = -1;
  std::int64_t raw_launch_connection_id = -1;
  std::int64_t raw_graph_connection_id = -1;
  std::int64_t raw_model_id = -1;
  StreamId execute_stream_id;
  StreamId model_stream_id;
  CapturedGraphInstanceId captured_graph_instance_id;
  TaskId model_execute_task_id;
  TaskId notify_wait_task_id;
  TaskId notify_record_task_id;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::int64_t wait_record_end_delta_ns = -1;
  GraphLaunchMatchPolicy match_policy = GraphLaunchMatchPolicy::kUnmatched;
  GraphLaunchInstanceAssociationPolicy instance_association_policy =
      GraphLaunchInstanceAssociationPolicy::kNone;
};

class GraphLaunchOccurrenceTable {
 public:
  GraphLaunchOccurrenceId append(
      SourceRefId source_ref_id,
      SourceRefId host_api_source_ref_id,
      std::uint32_t device_id,
      std::int64_t raw_host_api_row_id,
      std::int64_t raw_launch_connection_id,
      std::int64_t raw_graph_connection_id,
      std::int64_t raw_model_id,
      StreamId execute_stream_id,
      StreamId model_stream_id,
      CapturedGraphInstanceId captured_graph_instance_id,
      TaskId model_execute_task_id,
      TaskId notify_wait_task_id,
      TaskId notify_record_task_id,
      std::int64_t start_ns,
      std::int64_t end_ns,
      std::int64_t wait_record_end_delta_ns,
      GraphLaunchMatchPolicy match_policy,
      GraphLaunchInstanceAssociationPolicy instance_association_policy =
          GraphLaunchInstanceAssociationPolicy::kNone);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  void reserve(std::size_t count) { rows_.reserve(count); }
  const GraphLaunchOccurrenceRow& row(GraphLaunchOccurrenceId id) const;
  const std::vector<GraphLaunchOccurrenceRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<GraphLaunchOccurrenceRow> rows_;
};

}  // namespace traceloom
