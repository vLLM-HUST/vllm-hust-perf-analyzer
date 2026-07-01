#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/core/ids.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

std::string anchor_compat_id(AnchorId id);

std::vector<AnchorSqlRow> build_anchor_sequence_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx = 0);

}  // namespace traceloom::compat
