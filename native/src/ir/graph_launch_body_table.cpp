#include "traceloom/ir/graph_launch_body_table.h"

#include <stdexcept>

namespace traceloom {

ReplayBodyTemplateId ReplayBodyTemplateTable::append(
    SourceRefId source_ref_id,
    std::uint64_t exact_sequence_hash,
    SymbolId op_sequence_symbol_id,
    std::uint32_t compute_task_count,
    std::uint32_t communication_task_count,
    std::uint32_t stream_count,
    ReplayBodyTopologyPolicy topology_policy) {
  const auto id = checked_next_id<ReplayBodyTemplateId>(rows_.size());
  rows_.push_back(ReplayBodyTemplateRow{
      id, source_ref_id, exact_sequence_hash, op_sequence_symbol_id,
      compute_task_count, communication_task_count, stream_count,
      topology_policy});
  return id;
}

const ReplayBodyTemplateRow& ReplayBodyTemplateTable::row(
    ReplayBodyTemplateId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("ReplayBodyTemplateId is out of range");
  }
  return rows_[id.value()];
}

GraphLaunchBodyId GraphLaunchBodyTable::append(
    GraphLaunchOccurrenceId graph_launch_occurrence_id,
    ReplayBodyTemplateId replay_body_template_id,
    TaskId first_normalized_task_id,
    TaskId last_normalized_task_id,
    std::uint32_t compute_task_count,
    std::uint32_t communication_task_count,
    std::uint32_t stream_count) {
  const auto id = checked_next_id<GraphLaunchBodyId>(rows_.size());
  rows_.push_back(GraphLaunchBodyRow{
      id, graph_launch_occurrence_id, replay_body_template_id,
      first_normalized_task_id, last_normalized_task_id, compute_task_count,
      communication_task_count, stream_count});
  return id;
}

const GraphLaunchBodyRow& GraphLaunchBodyTable::row(
    GraphLaunchBodyId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("GraphLaunchBodyId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
