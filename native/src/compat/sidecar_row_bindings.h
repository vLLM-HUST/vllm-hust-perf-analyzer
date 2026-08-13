#pragma once

#include "traceloom/compat/sidecar_writer.h"
#include "sqlite_support.h"

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

void insert_metadata_row(SqliteStmt& stmt, const MetadataSqlRow& row);
void insert_anchor_cost_breakdown_row(SqliteStmt& stmt, const AnchorCostBreakdownSqlRow& row);
void insert_event_row(SqliteStmt& stmt, const EventSqlRow& row);
void insert_event_source_row(SqliteStmt& stmt, const EventSourceSqlRow& row);
void insert_runtime_call_row(SqliteStmt& stmt, const RuntimeCallSqlRow& row);
void insert_device_work_row(SqliteStmt& stmt, const DeviceWorkSqlRow& row);
void insert_runtime_device_relation_row(SqliteStmt& stmt, const RuntimeDeviceRelationSqlRow& row);
void insert_anchor_runtime_relation_row(SqliteStmt& stmt, const AnchorRuntimeRelationSqlRow& row);
void insert_anchor_host_interval_row(SqliteStmt& stmt, const AnchorHostIntervalSqlRow& row);
void insert_anchor_host_activity_row(SqliteStmt& stmt, const AnchorHostActivitySqlRow& row);
void insert_anchor_host_api_summary_row(
    SqliteStmt& stmt, const AnchorHostApiSummarySqlRow& row);
void insert_anchor_row(SqliteStmt& stmt, const AnchorSqlRow& row);
void insert_aux_link_row(SqliteStmt& stmt, const AuxLinkSqlRow& row);
void insert_anchor_aux_slot_row(SqliteStmt& stmt, const AnchorAuxSlotSqlRow& row);
void insert_viz_node_row(SqliteStmt& stmt, const VizNodeSqlRow& row);
void insert_viz_edge_row(SqliteStmt& stmt, const VizEdgeSqlRow& row);
void insert_viz_node_anchor_row(SqliteStmt& stmt, const VizNodeAnchorSqlRow& row);
void insert_anchor_primary_node_row(SqliteStmt& stmt, const AnchorPrimaryNodeSqlRow& row);
void insert_loop_node_row(SqliteStmt& stmt, const LoopNodeSqlRow& row);
void insert_graph_replay_row(SqliteStmt& stmt, const GraphReplaySqlRow& row);
void insert_graph_envelope_row(SqliteStmt& stmt, const GraphEnvelopeSqlRow& row);
void insert_graph_reconstruction_region_row(SqliteStmt& stmt, const GraphReconstructionRegionSqlRow& row);
void insert_graph_launch_row(SqliteStmt& stmt, const GraphLaunchSqlRow& row);
void insert_graph_body_member_row(SqliteStmt& stmt, const GraphBodyMemberSqlRow& row);
void insert_collective_global_link_row(SqliteStmt& stmt, const CollectiveGlobalLinkSqlRow& row);
void insert_global_collective_summary_row(SqliteStmt& stmt, const GlobalCollectiveSummarySqlRow& row);
void insert_global_collective_member_row(SqliteStmt& stmt, const GlobalCollectiveMemberSqlRow& row);
void insert_semantic_tree_header_row(SqliteStmt& stmt, const SemanticTreeHeaderSqlRow& row);
void insert_semantic_node_row(SqliteStmt& stmt, const SemanticNodeSqlRow& row);
void insert_semantic_edge_row(SqliteStmt& stmt, const SemanticEdgeSqlRow& row);

#endif

}  // namespace traceloom::compat
