#pragma once

#include <cstdint>

#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

struct RuntimeDeviceProjectionOptions {
  std::uint64_t max_host_activity_rows = 1000000;
};

// Materializes provider-neutral runtime calls, device-work objects, and every
// supported/open correlation outcome. Provider identifiers remain evidence;
// timestamps alone are never promoted into a submission edge.
RuntimeDeviceSqlRows build_runtime_device_sql_rows(const NativeIr& ir,
                                                   std::uint32_t db_idx = 0,
                                                   const RuntimeDeviceProjectionOptions&
                                                       options =
                                                           RuntimeDeviceProjectionOptions{});

}  // namespace traceloom::compat
