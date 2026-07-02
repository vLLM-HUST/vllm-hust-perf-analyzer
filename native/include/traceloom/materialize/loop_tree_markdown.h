#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

#include "traceloom/compat/sidecar_writer.h"

namespace traceloom {

struct LoopTreeMarkdownOptions {
  std::string db_label;
  std::string source_kind = "native_ir";
  std::string source_path;
  std::uint32_t db_idx = 0;
  bool has_device_id = false;
  std::uint32_t device_id = 0;
  std::uint64_t trace_event_count = 0;
  std::uint64_t anchor_count = 0;
};

void write_loop_tree_markdown(
    std::ostream& out,
    const compat::NodeCoverageSqlRows& rows,
    const LoopTreeMarkdownOptions& options = LoopTreeMarkdownOptions{});

}  // namespace traceloom
