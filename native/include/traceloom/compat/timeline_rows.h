#pragma once

#include <cstdint>
#include <string>

#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/core/ids.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

std::string trace_event_compat_id(TraceEventId id);

EventSqlRows build_timeline_sql_rows(const NativeIr& ir,
                                     std::uint32_t db_idx = 0);

}  // namespace traceloom::compat
