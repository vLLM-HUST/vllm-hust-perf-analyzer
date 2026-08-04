#include "traceloom/ir/graph_launch_occurrence_table.h"

#include <stdexcept>

namespace traceloom {

GraphLaunchOccurrenceId GraphLaunchOccurrenceTable::append(
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
    GraphLaunchMatchPolicy match_policy) {
  const auto id = checked_next_id<GraphLaunchOccurrenceId>(rows_.size());
  rows_.push_back(GraphLaunchOccurrenceRow{
      id,
      source_ref_id,
      host_api_source_ref_id,
      device_id,
      raw_host_api_row_id,
      raw_launch_connection_id,
      raw_graph_connection_id,
      raw_model_id,
      execute_stream_id,
      model_stream_id,
      captured_graph_instance_id,
      model_execute_task_id,
      notify_wait_task_id,
      notify_record_task_id,
      start_ns,
      end_ns,
      wait_record_end_delta_ns,
      match_policy,
  });
  return id;
}

const GraphLaunchOccurrenceRow& GraphLaunchOccurrenceTable::row(
    GraphLaunchOccurrenceId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("GraphLaunchOccurrenceId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
