#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/compat/sidecar_writer.h"

namespace traceloom {

struct ReconstructionStatusCount {
  std::string status;
  std::uint64_t region_count = 0;
};

enum class LoopTreeMarkdownView {
  kCompact,
  kExpanded,
  kBoth,
};

struct LoopTreeMarkdownOptions {
  std::string db_label;
  std::string source_kind = "native_ir";
  std::string input_format = "unknown";
  std::string source_path;
  std::string input_evidence_contract = "not_evaluated";
  std::string input_scope = "not_evaluated";
  std::string input_evidence_state = "not_evaluated";
  std::string input_missing_components;
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
  LoopTreeMarkdownView view = LoopTreeMarkdownView::kExpanded;
  std::size_t compact_operator_family_limit = 40;
  std::size_t compact_rhs_symbol_limit = 8;
};

void write_loop_tree_markdown(
    std::ostream& out,
    const compat::NodeCoverageSqlRows& rows,
    const LoopTreeMarkdownOptions& options = LoopTreeMarkdownOptions{},
    const compat::NativeCompactGrammarProjection* compact_grammar = nullptr);

}  // namespace traceloom
