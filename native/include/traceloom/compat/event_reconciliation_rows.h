#pragma once

#include <cstdint>
#include <string>

#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

// Replaces the sparse event-reconciliation audit relations and recreates the
// member-centric drill-down view. Normalized event rows remain untouched.
void replace_event_reconciliation_rows(const std::string& sqlite_path,
                                       const NativeIr& ir,
                                       std::uint32_t db_idx = 0);

}  // namespace traceloom::compat
