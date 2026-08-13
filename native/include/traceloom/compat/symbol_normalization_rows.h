#pragma once

#include <cstdint>

#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

SymbolNormalizationSqlRows build_symbol_normalization_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx = 0);

}  // namespace traceloom::compat
