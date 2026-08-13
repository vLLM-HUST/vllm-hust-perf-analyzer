#pragma once

#include <cstdint>
#include <string>

#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

// Publishes the effective flat policy table, one typed projection decision per
// normalized event, and normalized placement links into analysis.db.  The
// supplied config must be the same config used by build_flat_anchors so the
// audit surface reflects explicit per-analysis replacement/extension choices.
void replace_evidence_role_sql_rows(
    const std::string& sqlite_path,
    const NativeIr& ir,
    FlatAnchorBuildConfig config,
    std::uint32_t db_idx,
    bool materialize_aux_attribution);

}  // namespace traceloom::compat
