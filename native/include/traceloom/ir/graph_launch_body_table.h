#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

enum class ReplayBodyTopologyPolicy : std::uint8_t {
  kSingleModelStream = 0,
  kCapturedStreamSetUnordered = 1,
  kObservedStreamSetUnordered = 2,
};

struct ReplayBodyTemplateRow {
  ReplayBodyTemplateId id;
  SourceRefId source_ref_id;
  std::uint64_t exact_sequence_hash = 0;
  SymbolId op_sequence_symbol_id;
  std::uint32_t compute_task_count = 0;
  std::uint32_t communication_task_count = 0;
  std::uint32_t data_move_task_count = 0;
  std::uint32_t stream_count = 0;
  ReplayBodyTopologyPolicy topology_policy =
      ReplayBodyTopologyPolicy::kSingleModelStream;
};

class ReplayBodyTemplateTable {
 public:
  ReplayBodyTemplateId append(SourceRefId source_ref_id,
                              std::uint64_t exact_sequence_hash,
                              SymbolId op_sequence_symbol_id,
                              std::uint32_t compute_task_count,
                              std::uint32_t communication_task_count,
                              std::uint32_t stream_count,
                              ReplayBodyTopologyPolicy topology_policy,
                              std::uint32_t data_move_task_count = 0);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const ReplayBodyTemplateRow& row(ReplayBodyTemplateId id) const;
  const std::vector<ReplayBodyTemplateRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<ReplayBodyTemplateRow> rows_;
};

struct GraphLaunchBodyRow {
  GraphLaunchBodyId id;
  GraphLaunchOccurrenceId graph_launch_occurrence_id;
  ReplayBodyTemplateId replay_body_template_id;
  TaskId first_normalized_task_id;
  TaskId last_normalized_task_id;
  std::uint32_t compute_task_count = 0;
  std::uint32_t communication_task_count = 0;
  std::uint32_t data_move_task_count = 0;
  std::uint32_t stream_count = 0;
};

class GraphLaunchBodyTable {
 public:
  GraphLaunchBodyId append(
      GraphLaunchOccurrenceId graph_launch_occurrence_id,
      ReplayBodyTemplateId replay_body_template_id,
      TaskId first_normalized_task_id,
      TaskId last_normalized_task_id,
      std::uint32_t compute_task_count,
      std::uint32_t communication_task_count,
      std::uint32_t stream_count,
      std::uint32_t data_move_task_count = 0);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const GraphLaunchBodyRow& row(GraphLaunchBodyId id) const;
  const std::vector<GraphLaunchBodyRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<GraphLaunchBodyRow> rows_;
};

// Exact membership evidence for one observed graph launch body. Members are
// kept separately from the template: a template describes stable structure,
// while these rows retain the concrete TaskRow (and therefore timing and
// source provenance) for every occurrence.
struct GraphLaunchBodyMemberRow {
  GraphLaunchBodyMemberId id;
  GraphLaunchBodyId graph_launch_body_id;
  TaskId task_id;
  std::uint32_t lane_ordinal = 0;
  std::uint32_t task_ordinal = 0;
  enum class Kind : std::uint8_t { kCompute, kCommunication, kDataMove };
  Kind kind = Kind::kCompute;
};

class GraphLaunchBodyMemberTable {
 public:
  GraphLaunchBodyMemberId append(GraphLaunchBodyId graph_launch_body_id,
                                 TaskId task_id,
                                 std::uint32_t lane_ordinal,
                                 std::uint32_t task_ordinal,
                                 GraphLaunchBodyMemberRow::Kind kind);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const GraphLaunchBodyMemberRow& row(GraphLaunchBodyMemberId id) const;
  const std::vector<GraphLaunchBodyMemberRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<GraphLaunchBodyMemberRow> rows_;
};

}  // namespace traceloom
