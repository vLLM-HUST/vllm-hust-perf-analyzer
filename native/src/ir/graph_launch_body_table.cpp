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
    ReplayBodyTopologyPolicy topology_policy,
    std::uint32_t data_move_task_count) {
  const auto id = checked_next_id<ReplayBodyTemplateId>(rows_.size());
  rows_.push_back(ReplayBodyTemplateRow{
      id, source_ref_id, exact_sequence_hash, op_sequence_symbol_id,
      compute_task_count, communication_task_count, data_move_task_count,
      stream_count,
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
    std::uint32_t stream_count,
    std::uint32_t data_move_task_count) {
  const auto id = checked_next_id<GraphLaunchBodyId>(rows_.size());
  rows_.push_back(GraphLaunchBodyRow{
      id, graph_launch_occurrence_id, replay_body_template_id,
      first_normalized_task_id, last_normalized_task_id, compute_task_count,
      communication_task_count, data_move_task_count, stream_count});
  return id;
}

const GraphLaunchBodyRow& GraphLaunchBodyTable::row(
    GraphLaunchBodyId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("GraphLaunchBodyId is out of range");
  }
  return rows_[id.value()];
}

GraphLaunchBodyMemberId GraphLaunchBodyMemberTable::append(
    GraphLaunchBodyId graph_launch_body_id,
    TaskId task_id,
    std::uint32_t lane_ordinal,
    std::uint32_t task_ordinal,
    GraphLaunchBodyMemberRow::Kind kind,
    std::int64_t raw_graph_node_id,
    std::int64_t original_graph_node_id) {
  const auto id = checked_next_id<GraphLaunchBodyMemberId>(rows_.size());
  rows_.push_back(GraphLaunchBodyMemberRow{
      id, graph_launch_body_id, task_id, lane_ordinal, task_ordinal,
      kind, raw_graph_node_id, original_graph_node_id});
  return id;
}

const GraphLaunchBodyMemberRow& GraphLaunchBodyMemberTable::row(
    GraphLaunchBodyMemberId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("GraphLaunchBodyMemberId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
