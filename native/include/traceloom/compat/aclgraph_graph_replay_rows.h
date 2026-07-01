#pragma once

#include <cstdint>

#include "traceloom/adapters/aclgraph_fixture_reader.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

GraphReplaySqlRows build_aclgraph_fixture_graph_replay_sql_rows(
    const AclGraphSemanticFixture& fixture,
    const NativeIr& ir,
    std::uint32_t db_idx = 0);

}  // namespace traceloom::compat
