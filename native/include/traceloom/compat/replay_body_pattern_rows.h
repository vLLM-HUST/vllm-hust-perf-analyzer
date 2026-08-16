#pragma once

#include <cstdint>
#include <string>

#include "traceloom/analysis/replay_body_pattern.h"

namespace traceloom::compat {

// Materializes recursive replay-body patterns over the already-built exact
// replay cost map. Position rows retain aggregate ids, so every pattern and
// occurrence can drill through cost members to normalized events and raw
// source locators without rebuilding identity.
void replace_replay_body_pattern_rows(
    const std::string& sqlite_path,
    const NativeIr& ir,
    const ReplayInternalCostMapResult& replay_cost,
    std::uint32_t db_idx,
    const ReplayBodyPatternConfig& config = ReplayBodyPatternConfig{});

}  // namespace traceloom::compat
