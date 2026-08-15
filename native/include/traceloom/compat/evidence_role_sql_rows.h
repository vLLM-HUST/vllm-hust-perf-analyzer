#pragma once

#include <cstdint>
#include <string>

#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

struct AuxAttributionSqlRows;

// Publishes the effective flat policy table, one typed projection decision per
// normalized event, and normalized placement links into analysis.db.  The
// supplied config must be the same config used by build_flat_anchors so the
// audit surface reflects explicit per-analysis replacement/extension choices.
void replace_evidence_role_sql_rows(
    const std::string& sqlite_path,
    const NativeIr& ir,
    FlatAnchorBuildConfig config,
    std::uint32_t db_idx,
    bool materialize_aux_attribution,
    bool timing_diagnostics = false);

// Reuses attribution rows already built with the same config.  The sidecar
// materializer uses this overload so publishing the evidence-role audit does
// not rediscover every auxiliary-to-anchor relation.
void replace_evidence_role_sql_rows(
    const std::string& sqlite_path,
    const NativeIr& ir,
    FlatAnchorBuildConfig config,
    std::uint32_t db_idx,
    const AuxAttributionSqlRows& aux_attribution,
    bool timing_diagnostics = false);

}  // namespace traceloom::compat
