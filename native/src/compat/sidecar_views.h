#pragma once

#include "sqlite_support.h"

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
void drop_report_compatibility_views(SqliteDb& db);
void materialize_cuda_graph_views(SqliteDb& db);
void materialize_exact_graph_views(SqliteDb& db);
void materialize_global_collective_indexes(SqliteDb& db);
void materialize_node_cost_views(SqliteDb& db);
void materialize_replay_cost_views(SqliteDb& db);
void materialize_report_compatibility_indexes(SqliteDb& db);
void materialize_runtime_device_views(SqliteDb& db);
void materialize_structure_bubble_views(SqliteDb& db);
void materialize_symbol_normalization_views(SqliteDb& db);
void materialize_semantic_tree_views(SqliteDb& db);
void materialize_tree_node_anchor_view(SqliteDb& db);
void materialize_tree_node_occurrence_view(SqliteDb& db);
void materialize_tree_node_view(SqliteDb& db);
#endif

}  // namespace traceloom::compat
