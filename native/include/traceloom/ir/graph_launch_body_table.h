#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

enum class ReplayBodyTopologyPolicy : std::uint8_t {
  kSingleModelStream = 0,
  kCapturedStreamSetUnordered = 1,
};

struct ReplayBodyTemplateRow {
  ReplayBodyTemplateId id;
  SourceRefId source_ref_id;
  std::uint64_t exact_sequence_hash = 0;
  SymbolId op_sequence_symbol_id;
  std::uint32_t compute_task_count = 0;
  std::uint32_t communication_task_count = 0;
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
                              ReplayBodyTopologyPolicy topology_policy);

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
      std::uint32_t stream_count);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const GraphLaunchBodyRow& row(GraphLaunchBodyId id) const;
  const std::vector<GraphLaunchBodyRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<GraphLaunchBodyRow> rows_;
};

}  // namespace traceloom
