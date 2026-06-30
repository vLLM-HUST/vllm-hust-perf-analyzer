#pragma once

#include <vector>

#include "traceloom/report/report_tree.h"

namespace traceloom {

struct ReportTreeBuildConfig {
  bool fold_adjacent_runs = true;
  std::uint32_t min_run_length = 2;
};

ReportTree build_report_tree_from_tokens(
    const std::vector<ReportToken>& tokens,
    ReportTreeBuildConfig config = ReportTreeBuildConfig{});

void validate_report_tree_or_throw(const ReportTree& tree,
                                   std::uint32_t token_count);

}  // namespace traceloom
