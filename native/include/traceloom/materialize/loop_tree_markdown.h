#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "traceloom/compat/sidecar_writer.h"

namespace traceloom {

struct ReconstructionStatusCount {
  std::string status;
  std::uint64_t region_count = 0;
};

struct IdleExplanationSummaryCount {
  std::string category;
  std::uint64_t slice_count = 0;
  std::uint64_t duration_ns = 0;
};

struct LoopTreeMarkdownOptions {
  std::string db_label;
  std::string source_kind = "native_ir";
  std::string source_path;
  std::uint32_t db_idx = 0;
  bool has_device_id = false;
  std::uint32_t device_id = 0;
  std::uint64_t trace_event_count = 0;
  std::uint64_t anchor_count = 0;
  std::uint64_t replay_composition_region_count = 0;
  std::uint64_t recognized_replay_composition_region_count = 0;
  std::uint64_t unrecognized_replay_composition_region_count = 0;
  std::uint64_t replay_unit_count = 0;
  std::uint64_t exact_replay_unit_count = 0;
  std::vector<ReconstructionStatusCount> reconstruction_status_counts;
  bool has_idle_explanation_summary = false;
  std::string idle_analysis_status;
  std::string idle_collection_status;
  std::string idle_attribution_rule_version;
  std::uint64_t visible_productive_idle_ns = 0;
  std::uint64_t direct_explained_idle_ns = 0;
  std::vector<IdleExplanationSummaryCount> idle_explanation_counts;
};

void write_loop_tree_markdown(
    std::ostream& out,
    const compat::NodeCoverageSqlRows& rows,
    const LoopTreeMarkdownOptions& options = LoopTreeMarkdownOptions{});

}  // namespace traceloom
