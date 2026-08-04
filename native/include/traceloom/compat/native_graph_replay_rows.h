#pragma once

#include <cstdint>
#include <string>

#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

GraphReplayEvidenceSqlRows build_native_graph_replay_evidence_sql_rows(
    const NativeIr& ir,
    const std::string& source_kind,
    std::uint32_t db_idx = 0);

}  // namespace traceloom::compat
