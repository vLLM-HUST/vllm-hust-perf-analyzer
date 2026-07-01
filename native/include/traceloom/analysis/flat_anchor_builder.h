#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "traceloom/ir/native_ir.h"

namespace traceloom {

struct FlatAnchorBuildConfig {
  std::vector<std::string> skipped_task_type_symbols;
  bool skip_tasks_covered_by_communication_ops = false;
  bool filter_auxiliary_task_anchors = false;
};

struct FlatAnchorBuildStats {
  std::string projection_kind = "raw_event_bootstrap";
  std::size_t device_event_anchors = 0;
  std::size_t communication_anchors = 0;
  std::size_t skipped_task_events = 0;
  std::size_t tokens = 0;
};

FlatAnchorBuildStats build_flat_anchors(
    NativeIr& ir,
    FlatAnchorBuildConfig config = FlatAnchorBuildConfig{});

}  // namespace traceloom
