#include "augmented_replay_projection_catalog.h"

#include <string>
#include <vector>

#include "sidecar_sqlite_utils.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
namespace traceloom::compat::detail {
namespace {

void insert_values(sqlite3* db,
                   const std::string& table,
                   const std::vector<std::vector<std::string>>& rows,
                   const std::string& context) {
  for (const std::vector<std::string>& row : rows) {
    std::string sql = "INSERT INTO " + table + " VALUES(";
    for (std::size_t index = 0; index < row.size(); ++index) {
      if (index != 0) {
        sql += ',';
      }
      sql += quote_literal(row[index]);
    }
    sql += ')';
    sqlite_exec(db, sql, context);
  }
}

}  // namespace

void materialize_replay_projection_catalog(sqlite3* db) {
  insert_values(
      db, "traceloom_analysis_surface",
      {
          {"replay_region_annotation_status",
           "traceloom_v_replay_region_annotation_status",
           "one device-level replay-region annotation support result",
           "fail closed before highlighting exact replay regions on the "
           "main anchor timeline",
           "SELECT * FROM traceloom_v_replay_region_annotation_status "
           "ORDER BY db_idx, device_id;"},
          {"annotated_anchor_timeline",
           "traceloom_v_annotated_anchor_timeline",
           "one mainline anchor with nullable replay-region and Position "
           "coordinates",
           "read compute, communication, and replay Position anchors on one "
           "plane while treating replay as a background region annotation, "
           "not event ownership",
           "SELECT * FROM traceloom_v_annotated_anchor_timeline ORDER BY "
           "db_idx, device_id, anchor_idx;"},
          {"flattened_execution_timeline",
           "traceloom_v_flattened_execution_timeline",
           "one retained mainline anchor or exact expanded Position member",
           "plot ordinary anchors, communication operations, and exact "
           "replay-body operators on one observed timestamp plane without "
           "double-rendering expanded Position spans",
           "SELECT * FROM traceloom_v_flattened_execution_timeline ORDER BY "
           "db_idx, device_id, timeline_order;"},
          {"exact_graph_member", "traceloom_v_node_graph_body_member",
           "one exact graph body member in one tree occurrence",
           "drill through replay structure to exact member cost and "
           "provenance; timestamp order is observational, not causal",
           "SELECT * FROM traceloom_v_node_graph_body_member ORDER BY "
           "db_idx, device_id, node_id, occurrence_idx, node_member_order, "
           "start_ns, end_ns, lane_ordinal, task_ordinal, member_id;"},
          {"replay_position_realization_member",
           "traceloom_v_replay_position_realization_member",
           "one exact member in one realized replay Position plane",
           "read operators, collectives, and support evidence in observed "
           "timestamp order while retaining protected-boundary role lineage",
           "SELECT * FROM "
           "traceloom_v_replay_position_realization_member WHERE launch_id "
           "= :launch_id ORDER BY observed_order, member_id;"},
          {"replay_hotspot", "traceloom_replay_cost_aggregate",
           "one role-collapsed replay member distribution",
           "rank stable replay-internal cost distributions",
           "SELECT * FROM traceloom_replay_cost_aggregate ORDER BY "
           "duration_median_ns DESC, aggregate_id;"},
          {"analysis_issue", "traceloom_replay_cost_issue",
           "one typed replay analysis issue",
           "audit unsupported or unrecognized replay analysis",
           "SELECT * FROM traceloom_replay_cost_issue ORDER BY db_idx, "
           "device_id, replay_unit_id, issue_id;"},
          {"reconstruction_status",
           "traceloom_aclgraph_reconstruction_region",
           "one graph reconstruction region",
           "audit recognized and unrecognized graph reconstruction evidence",
           "SELECT * FROM traceloom_aclgraph_reconstruction_region ORDER BY "
           "db_idx, device_id, region_order;"},
          {"operator_audit", "traceloom_operator_audit",
           "one observed operator identity",
           "rank concrete device operators without an allowlist",
           "SELECT * FROM traceloom_operator_audit ORDER BY "
           "graph_body_member_count DESC, total_duration_ns DESC, "
           "operator_name;"},
      },
      "failed to insert replay analysis surface");

  insert_values(
      db, "traceloom_projection_recipe",
      {
          {"annotated_anchor_timeline", "35", "device_sequence",
           "all_mainline_anchors", "replay_region_highlight", "device",
           "anchor_span_identity_and_provenance", "(none)",
           "retain the canonical anchor sequence and add only fail-closed "
           "Pattern-Occurrence-Position coordinates plus replay background "
           "regions",
           "SELECT db_idx,device_id,anchor_idx,anchor_id,event_id,symbol,role,"
           "source_table,start_ns,end_ns,replay_annotation_support_state,"
           "replay_region_state,protected_interval_id,replay_occurrence_id,"
           "replay_pattern_id,replay_position_id,replay_position_order,"
           "replay_annotation_semantics,observation_semantics FROM "
           "traceloom_v_annotated_anchor_timeline ORDER BY db_idx,device_id,"
           "anchor_idx;"},
          {"flattened_execution_timeline", "36", "device_sequence",
           "exact_position_expansion", "observed_timestamp_plane", "device",
           "event_spans_with_non_additive_nested_duration", "(none)",
           "replace supported graph Position spans with their exact body "
           "members while retaining every other mainline anchor at its real "
           "timestamp",
           "SELECT db_idx,device_id,timeline_order,timeline_item_id,item_kind,"
           "mainline_anchor_idx,mainline_anchor_id,position_anchor_id,event_id,"
           "symbol,source_table,start_ns,end_ns,replay_region_state,"
           "replay_occurrence_id,replay_pattern_id,replay_position_id,"
           "replay_position_order,flattening_action,flattening_semantics,"
           "replay_annotation_semantics,observation_semantics,"
           "duration_aggregation_semantics FROM "
           "traceloom_v_flattened_execution_timeline ORDER BY db_idx,"
           "device_id,timeline_order;"},
          {"scope_exact_replay_members", "40", "structural_node",
           "one_or_all_occurrences", "observed_member_plane", "device",
           "scheduled_member_cost_role_and_provenance",
           ":node_id, :occurrence_idx (NULL selects all)",
           "expand supported graph/replay anchors to one observed Position "
           "plane without inferring membership, dependency, or causality "
           "from time",
           "SELECT member.node_id, member.occurrence_idx, "
           "position.cost_unit_id, position.replay_unit_id, "
           "position.launch_id, position.position_order AS slot_order, "
           "member.node_member_order, position.observed_order, "
           "position.lane_ordinal, position.task_ordinal, position.event_id, "
           "position.identity, position.policy_role, position.final_role, "
           "position.interval_relation, position.start_ns, position.end_ns, "
           "position.duration_ns, position.source_table, "
           "position.source_row_id, position.evidence_level FROM "
           "traceloom_v_node_graph_body_member member JOIN "
           "traceloom_v_replay_position_realization_member position ON "
           "position.launch_id = member.node_launch_id AND "
           "position.member_id = member.member_id AND position.db_idx = "
           "member.db_idx AND position.device_id = member.device_id WHERE "
           "member.node_id = :node_id AND (:occurrence_idx IS NULL OR "
           "member.occurrence_idx = :occurrence_idx) ORDER BY "
           "member.occurrence_idx, member.node_member_order, "
           "position.observed_order, position.member_id;"},
          {"replay_cost_units", "41", "exact_replay_unit",
           "candidate_occurrences", "unit_cost_summary", "device",
           "support_and_launch_inventory", "(none)",
           "discover exact replay-unit cost support before selecting one "
           "occurrence for launch-level inspection",
           "SELECT cost_unit_id, db_idx, device_id, replay_unit_id, "
           "graph_template_id, launch_member_count, resolved_launch_count, "
           "support_status, reason_codes FROM traceloom_replay_cost_unit "
           "ORDER BY db_idx, device_id, replay_unit_id;"},
          {"replay_cost_launches", "42", "exact_replay_unit",
           "one_or_all_slots", "ordered_launch_slots", "device",
           "launch_cost_lenses",
           ":cost_unit_id, :slot_order (NULL selects all)",
           "inspect every ordered launch slot or one selected slot "
           "occurrence inside an exact replay unit",
           "SELECT launch_id, cost_unit_id, db_idx, device_id, "
           "replay_unit_id, member_order, graph_launch_occurrence_id, "
           "composition_slot_id, slot_role, slot_order, "
           "replay_body_template_id, body_id, support_status, reason_code, "
           "member_count, task_sum_ns, busy_union_ns, envelope_ns, "
           "compute_ns, communication_ns, data_move_ns FROM "
           "traceloom_replay_cost_launch WHERE cost_unit_id = :cost_unit_id "
           "AND (:slot_order IS NULL OR slot_order = :slot_order) ORDER BY "
           "member_order, launch_id;"},
          {"replay_cost_members", "43", "graph_launch", "all_members",
           "observed_member_plane", "device",
           "scheduled_member_cost_role_and_provenance", ":launch_id",
           "expand one supported replay launch to exact member costs and "
           "roles in observed timestamp order, not inferred dependency "
           "order",
           "SELECT member_id, launch_id, cost_unit_id, db_idx, device_id, "
           "position_anchor_id, composition_slot_id, slot_role, "
           "position_order AS slot_order, replay_body_template_id, body_id, "
           "observed_order, observed_relation_to_previous, "
           "observation_semantics, stream_id, lane_ordinal, task_ordinal, "
           "kind, event_id, identity, policy_role, final_role, "
           "effective_structural_participation, membership_relation, "
           "interval_relation, raw_task_id, start_ns, end_ns, duration_ns, "
           "relative_start_ns, relative_end_ns, scheduled_work_share_ppm, "
           "scheduled_work_share_supported, "
           "scheduled_work_denominator_body_task_sum_ns, source_table, "
           "source_row_id, evidence_level FROM "
           "traceloom_v_replay_position_realization_member WHERE launch_id "
           "= :launch_id ORDER BY observed_order, member_id;"},
          {"replay_structural_placements", "44", "exact_replay_unit",
           "one_unit", "structural_occurrences", "device",
           "structural_membership", ":replay_unit_id",
           "locate every recovered graph-unit tree occurrence that realizes "
           "one exact replay unit without assuming one placement",
           "SELECT DISTINCT member.replay_unit_id, member.db_idx, "
           "member.device_id, member.node_id, member.occurrence_idx, "
           "node.display_order, node.path, node.label, node.category FROM "
           "traceloom_v_node_graph_body_member member JOIN "
           "traceloom_v_tree_node node ON node.node_id = member.node_id "
           "WHERE member.replay_unit_id = :replay_unit_id AND "
           "node.category = 'graph_unit' ORDER BY node.display_order, "
           "member.occurrence_idx, member.node_id;"},
          {"exact_replay_partition", "45", "device_sequence",
           "complete_partition", "open_replay_between_segments", "device",
           "right_anchored_disjoint_cost", "(none)",
           "read the complete cost partition induced by ordered exact replay "
           "boundaries without rebuilding intervals in client SQL",
           "SELECT tree_id,db_idx,device_id,segment_order,coordinate_kind,"
           "coordinate_index,segment_label,left_protected_interval_id,"
           "right_protected_interval_id,anchor_count,compute_us,comm_us,"
           "idle_us,total_us,aux_us FROM "
           "traceloom_v_exact_replay_partition ORDER BY db_idx,device_id,"
           "tree_id,segment_order;"},
      },
      "failed to insert replay projection recipe");

  insert_values(
      db, "traceloom_projection_parameter",
      {
          {"scope_exact_replay_members", "10", "node_id", "TEXT", "0",
           "structural_node_id", "traceloom_v_tree_node", "node_id",
           "selected structural scope containing supported replay evidence"},
          {"scope_exact_replay_members", "20", "occurrence_idx", "INTEGER",
           "1", "structural_occurrence_index",
           "traceloom_tree_node_occurrence", "occurrence_idx",
           "NULL selects all occurrences; a value selects one execution"},
          {"replay_cost_launches", "10", "cost_unit_id", "TEXT", "0",
           "replay_cost_unit_id", "traceloom_replay_cost_unit",
           "cost_unit_id", "selected exact replay-unit cost occurrence"},
          {"replay_cost_launches", "20", "slot_order", "INTEGER", "1",
           "replay_slot_order", "traceloom_replay_cost_launch",
           "slot_order", "NULL selects all launch slots; a value selects "
           "one slot"},
          {"replay_cost_members", "10", "launch_id", "TEXT", "0",
           "replay_cost_launch_id", "traceloom_replay_cost_launch",
           "launch_id", "selected supported replay launch"},
          {"replay_structural_placements", "10", "replay_unit_id",
           "INTEGER", "0", "replay_unit_id",
           "traceloom_replay_cost_unit", "replay_unit_id",
           "selected exact replay occurrence"},
      },
      "failed to insert replay projection parameter");

  insert_values(
      db, "traceloom_projection_coordinate",
      {
          {"annotated_anchor_timeline", "10", "db_idx", "database_index",
           "source database coordinate"},
          {"annotated_anchor_timeline", "20", "device_id", "device_id",
           "device coordinate"},
          {"annotated_anchor_timeline", "30", "anchor_idx",
           "mainline_anchor_index", "canonical anchor-sequence coordinate"},
          {"annotated_anchor_timeline", "40", "event_id",
           "normalized_event_id", "normalized anchor event for audit"},
          {"annotated_anchor_timeline", "50", "replay_occurrence_id",
           "replay_occurrence_id", "nullable highlighted replay occurrence"},
          {"annotated_anchor_timeline", "60", "replay_pattern_id",
           "graph_template_id", "nullable replay Pattern coordinate"},
          {"annotated_anchor_timeline", "70", "replay_position_id",
           "graph_launch_id", "nullable replay Position coordinate"},
          {"flattened_execution_timeline", "10", "db_idx",
           "database_index", "source database coordinate"},
          {"flattened_execution_timeline", "20", "device_id", "device_id",
           "device coordinate"},
          {"flattened_execution_timeline", "30", "timeline_order",
           "observed_timeline_order",
           "stable timestamp order without dependency semantics"},
          {"flattened_execution_timeline", "40", "event_id",
           "normalized_event_id", "normalized timeline event for audit"},
          {"flattened_execution_timeline", "50", "replay_occurrence_id",
           "replay_occurrence_id", "nullable highlighted replay occurrence"},
          {"flattened_execution_timeline", "60", "replay_pattern_id",
           "graph_template_id", "nullable replay Pattern coordinate"},
          {"flattened_execution_timeline", "70", "replay_position_id",
           "graph_launch_id", "nullable replay Position coordinate"},
          {"scope_exact_replay_members", "10", "node_id",
           "structural_node_id", "selected structural scope"},
          {"scope_exact_replay_members", "20", "occurrence_idx",
           "structural_occurrence_index", "selected realized occurrence"},
          {"scope_exact_replay_members", "30", "cost_unit_id",
           "replay_cost_unit_id", "selected exact replay cost unit"},
          {"scope_exact_replay_members", "40", "replay_unit_id",
           "replay_unit_id", "selected exact replay occurrence"},
          {"scope_exact_replay_members", "50", "launch_id",
           "replay_cost_launch_id", "selected graph launch cost occurrence"},
          {"scope_exact_replay_members", "60", "slot_order",
           "replay_slot_order", "ordered launch slot inside the replay unit"},
          {"scope_exact_replay_members", "70", "event_id",
           "normalized_event_id", "exact replay member available for audit"},
          {"replay_cost_units", "10", "cost_unit_id",
           "replay_cost_unit_id", "selected exact replay cost unit"},
          {"replay_cost_units", "20", "replay_unit_id", "replay_unit_id",
           "selected exact replay occurrence"},
          {"replay_cost_units", "30", "graph_template_id",
           "graph_template_id", "recovered replay template"},
          {"replay_cost_units", "40", "device_id", "device_id",
           "device coordinate"},
          {"replay_cost_launches", "10", "launch_id",
           "replay_cost_launch_id", "selected replay launch cost occurrence"},
          {"replay_cost_launches", "20", "cost_unit_id",
           "replay_cost_unit_id", "owning exact replay cost unit"},
          {"replay_cost_launches", "30", "replay_unit_id",
           "replay_unit_id", "owning exact replay occurrence"},
          {"replay_cost_launches", "40", "graph_launch_occurrence_id",
           "graph_launch_occurrence_id", "exact graph launch occurrence"},
          {"replay_cost_launches", "50", "composition_slot_id",
           "replay_composition_slot_id", "recovered composition slot"},
          {"replay_cost_launches", "60", "slot_order",
           "replay_slot_order", "ordered launch slot inside the replay unit"},
          {"replay_cost_members", "10", "member_id",
           "replay_cost_member_id", "selected exact replay member cost row"},
          {"replay_cost_members", "20", "launch_id",
           "replay_cost_launch_id", "owning replay launch cost occurrence"},
          {"replay_cost_members", "30", "cost_unit_id",
           "replay_cost_unit_id", "owning exact replay cost unit"},
          {"replay_cost_members", "40", "event_id",
           "normalized_event_id", "normalized event available for audit"},
          {"replay_structural_placements", "10", "replay_unit_id",
           "replay_unit_id", "selected exact replay occurrence"},
          {"replay_structural_placements", "20", "node_id",
           "structural_node_id", "recovered structural scope containing it"},
          {"replay_structural_placements", "30", "occurrence_idx",
           "structural_occurrence_index", "realized structural occurrence"},
          {"exact_replay_partition", "10", "tree_id", "structural_tree_id",
           "device-tree whose exact replay boundaries induce the partition"},
          {"exact_replay_partition", "20", "db_idx", "database_index",
           "source database coordinate"},
          {"exact_replay_partition", "30", "device_id", "device_id",
           "device coordinate"},
          {"exact_replay_partition", "40", "coordinate_kind",
           "replay_partition_kind", "open, replay, or between-replays class"},
          {"exact_replay_partition", "50", "coordinate_index",
           "replay_partition_index", "stable index within the segment class"},
      },
      "failed to insert replay projection coordinate");
}

}  // namespace traceloom::compat::detail
#endif
