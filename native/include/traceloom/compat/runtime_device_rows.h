#pragma once

#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

// Materializes provider-neutral runtime calls, device-work objects, and every
// supported/open correlation outcome. Provider identifiers remain evidence;
// timestamps alone are never promoted into a submission edge.
RuntimeDeviceSqlRows build_runtime_device_sql_rows(const NativeIr& ir,
                                                   std::uint32_t db_idx = 0);

}  // namespace traceloom::compat
