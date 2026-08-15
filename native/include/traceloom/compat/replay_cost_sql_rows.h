#pragma once

#include <cstdint>
#include <string>

#include "traceloom/analysis/replay_internal_cost_map.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

// Materializes the authoritative native replay-internal cost map as a
// normalized SQL surface.  Exact launch/member identifiers deliberately
// match traceloom_graph_launch and traceloom_graph_body_member so callers can
// drill from a cost lens to provider evidence without re-deriving identity.
void replace_replay_cost_rows(const std::string &sqlite_path,
                              const NativeIr &ir, std::uint32_t db_idx);

// Reuses an already-built map when another materializer (for example replay
// body pattern recovery) consumes the same authoritative result.
void replace_replay_cost_rows(
    const std::string& sqlite_path,
    const NativeIr& ir,
    const ReplayInternalCostMapResult& result,
    std::uint32_t db_idx);

} // namespace traceloom::compat
