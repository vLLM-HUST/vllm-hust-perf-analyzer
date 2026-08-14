#pragma once

#include <cstdint>

#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

AuxAttributionSqlRows build_aux_attribution_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx = 0);

AuxAttributionSqlRows build_aux_attribution_sql_rows(
    const NativeIr& ir,
    FlatAnchorBuildConfig config,
    std::uint32_t db_idx = 0);

}  // namespace traceloom::compat
