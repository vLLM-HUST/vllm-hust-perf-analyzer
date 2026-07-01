#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/adapters/aclgraph_fixture_reader.h"
#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/report/anchor_internal_cost_breakdown.h"

namespace traceloom::compat {

GraphReplaySqlRows build_aclgraph_fixture_graph_replay_sql_rows(
    const AclGraphSemanticFixture& fixture,
    const NativeIr& ir,
    std::uint32_t db_idx = 0);

std::vector<EventSqlRow> split_graph_replay_timeline_sql_rows(
    const GraphReplaySqlRows& rows);

std::vector<EventSourceSqlRow> split_graph_replay_source_lineage_sql_rows(
    const GraphReplaySqlRows& rows);

std::vector<AnchorSqlRow> split_graph_replay_anchor_sequence_sql_rows(
    const GraphReplaySqlRows& rows);

GraphReplayEvidenceSqlRows split_graph_replay_evidence_sql_rows(
    const GraphReplaySqlRows& rows);

void write_aclgraph_fixture_compatibility_sidecar(
    const std::string& sqlite_path,
    const AclGraphSemanticFixture& fixture,
    const NativeIr& ir,
    const AnchorInternalCostBreakdown& breakdown,
    const NativeCompatibilitySidecarOptions& options);

}  // namespace traceloom::compat
