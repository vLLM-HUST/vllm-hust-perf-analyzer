PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE traceloom_metadata (key TEXT NOT NULL, value TEXT NOT NULL);
CREATE TABLE traceloom_event (event_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, step_idx INTEGER NOT NULL, source_table TEXT NOT NULL, source_key TEXT NOT NULL, stream_id INTEGER, start_ns INTEGER, end_ns INTEGER, dur_us REAL, category TEXT, role TEXT, semantic_role TEXT, semantic_role_reason TEXT, symbol TEXT, label TEXT, raw_label TEXT, op_type TEXT, compute_task_type TEXT, family TEXT, task_type TEXT, raw_json TEXT);
CREATE TABLE traceloom_event_source (event_id TEXT NOT NULL, source_ordinal INTEGER NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, source_table TEXT NOT NULL, source_key TEXT NOT NULL, source_role TEXT, raw_json TEXT);
CREATE TABLE traceloom_runtime_call (runtime_call_id TEXT NOT NULL, db_idx INTEGER NOT NULL, provider TEXT NOT NULL, clock_domain TEXT NOT NULL, source_table TEXT NOT NULL, source_key TEXT NOT NULL, start_ns INTEGER NOT NULL, end_ns INTEGER NOT NULL, dur_us REAL NOT NULL, api_name TEXT, api_type TEXT, process_id TEXT, thread_id TEXT, global_tid TEXT, context_id TEXT, device_id TEXT, correlation_id TEXT, match_policy TEXT NOT NULL, raw_json TEXT);
CREATE TABLE traceloom_device_work (device_work_id TEXT NOT NULL, db_idx INTEGER NOT NULL, provider TEXT NOT NULL, device_id INTEGER NOT NULL, work_kind TEXT NOT NULL, event_id TEXT, task_id TEXT, graph_launch_occurrence_id INTEGER, source_table TEXT NOT NULL, source_key TEXT NOT NULL, start_ns INTEGER NOT NULL, end_ns INTEGER NOT NULL, dur_us REAL NOT NULL, symbol TEXT, raw_json TEXT);
CREATE TABLE traceloom_runtime_device_relation (relation_id TEXT NOT NULL, db_idx INTEGER NOT NULL, runtime_call_id TEXT, device_work_id TEXT, relation_kind TEXT NOT NULL, match_policy TEXT NOT NULL, evidence_level TEXT NOT NULL, support_state TEXT NOT NULL, cardinality TEXT NOT NULL, runtime_candidate_count INTEGER NOT NULL, device_candidate_count INTEGER NOT NULL, correlation_id TEXT, raw_json TEXT);
CREATE TABLE traceloom_anchor_runtime_relation (anchor_id TEXT NOT NULL, relation_id TEXT NOT NULL, runtime_call_id TEXT, device_work_id TEXT NOT NULL, endpoint_kind TEXT NOT NULL);
CREATE TABLE traceloom_anchor_host_interval (interval_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, left_anchor_id TEXT NOT NULL, right_anchor_id TEXT NOT NULL, left_runtime_call_id TEXT, right_runtime_call_id TEXT, left_endpoint_count INTEGER NOT NULL, right_endpoint_count INTEGER NOT NULL, provider TEXT, clock_domain TEXT, host_start_ns INTEGER, host_end_ns INTEGER, scope_policy TEXT NOT NULL, process_id TEXT, thread_id TEXT, support_state TEXT NOT NULL);
CREATE TABLE traceloom_anchor_host_activity (interval_id TEXT NOT NULL, runtime_call_id TEXT NOT NULL, observed_order INTEGER NOT NULL);
CREATE TABLE traceloom_anchor_host_api_summary (interval_id TEXT NOT NULL, api_family TEXT NOT NULL, call_count INTEGER NOT NULL, distinct_api_name_count INTEGER NOT NULL, scheduled_call_us REAL NOT NULL, scheduled_overlap_us REAL NOT NULL);
CREATE TABLE traceloom_anchor (anchor_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, anchor_idx INTEGER NOT NULL, event_id TEXT NOT NULL, step_idx INTEGER NOT NULL, symbol TEXT, role TEXT, label TEXT, family TEXT, start_ns INTEGER, end_ns INTEGER, dur_us REAL);
CREATE TABLE traceloom_event_reconciliation_policy (policy_id TEXT NOT NULL, policy_version TEXT NOT NULL, manifest_schema TEXT NOT NULL, source_manifest TEXT NOT NULL, manifest_sha256 TEXT NOT NULL, unmatched_behavior TEXT NOT NULL, description TEXT NOT NULL);
CREATE TABLE traceloom_event_reconciliation_rule (policy_id TEXT NOT NULL, policy_version TEXT NOT NULL, rule_id TEXT NOT NULL, priority INTEGER NOT NULL, provider_scope TEXT NOT NULL, source_domain TEXT NOT NULL, task_type TEXT NOT NULL, generic_context_id INTEGER NOT NULL, concrete_context_id INTEGER NOT NULL, min_contained_fraction REAL NOT NULL, rule_origin TEXT NOT NULL, rule_origin_sha256 TEXT NOT NULL, source_line INTEGER NOT NULL, note TEXT NOT NULL);
CREATE TABLE traceloom_event_reconciliation_decision (decision_id TEXT NOT NULL, db_idx INTEGER NOT NULL, policy_id TEXT NOT NULL, policy_version TEXT NOT NULL, rule_id TEXT NOT NULL, status TEXT NOT NULL, reason_code TEXT NOT NULL, canonical_event_id TEXT, envelope_event_id TEXT, canonical_anchor_id TEXT, canonical_start_ns INTEGER, canonical_end_ns INTEGER, contained_fraction REAL, member_count INTEGER NOT NULL);
CREATE TABLE traceloom_event_reconciliation_member (decision_id TEXT NOT NULL, member_order INTEGER NOT NULL, db_idx INTEGER NOT NULL, event_id TEXT NOT NULL, task_id INTEGER NOT NULL, source_path TEXT NOT NULL, source_table TEXT NOT NULL, source_key TEXT NOT NULL, device_id INTEGER NOT NULL, stream_id INTEGER NOT NULL, raw_task_id INTEGER NOT NULL, raw_global_task_id INTEGER, raw_connection_id INTEGER, raw_context_id INTEGER, member_role TEXT NOT NULL, contributes_timing INTEGER NOT NULL, contributes_symbol INTEGER NOT NULL, contributes_cost INTEGER NOT NULL, retained_as_normalized_evidence INTEGER NOT NULL);
CREATE TABLE traceloom_symbol_normalization_policy (policy_id TEXT NOT NULL, policy_version TEXT NOT NULL, policy_kind TEXT NOT NULL, source_manifest TEXT NOT NULL, manifest_sha256 TEXT NOT NULL, description TEXT NOT NULL);
CREATE TABLE traceloom_symbol_normalization_rule (policy_id TEXT NOT NULL, policy_version TEXT NOT NULL, rule_id TEXT NOT NULL, precedence INTEGER NOT NULL, provider_scope TEXT NOT NULL, source_domain TEXT NOT NULL, match_mode TEXT NOT NULL, match_expression TEXT NOT NULL, structural_symbol TEXT NOT NULL, required_fields TEXT NOT NULL, rule_origin TEXT NOT NULL, rule_origin_sha256 TEXT NOT NULL, source_line INTEGER NOT NULL, description TEXT NOT NULL);
CREATE TABLE traceloom_anchor_symbol_normalization (anchor_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, anchor_idx INTEGER NOT NULL, event_id TEXT, source_path TEXT NOT NULL, source_table TEXT NOT NULL, source_key TEXT NOT NULL, observed_symbol TEXT, observed_symbol_source TEXT NOT NULL, structural_symbol TEXT, policy_id TEXT NOT NULL, policy_version TEXT NOT NULL, rule_id TEXT NOT NULL, candidate_rule_ids TEXT, outcome TEXT NOT NULL, reason_code TEXT NOT NULL);
CREATE TABLE traceloom_anchor_aux_slot (anchor_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, anchor_idx INTEGER NOT NULL, anchor_step_idx INTEGER NOT NULL, aux_start_step_idx INTEGER, aux_end_step_idx INTEGER, aux_event_count INTEGER, aux_dur_us REAL, raw_json TEXT);
CREATE TABLE traceloom_aux_link (anchor_id TEXT NOT NULL, aux_event_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, aux_order INTEGER NOT NULL, aux_step_idx INTEGER NOT NULL, link_type TEXT NOT NULL, reason TEXT, aux_kind TEXT, aux_dur_us REAL, raw_json TEXT);
CREATE TABLE traceloom_cuda_graph_replay (graph_event_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, graph_provider TEXT, graph_kind TEXT, graph_event_idx INTEGER NOT NULL, event_id TEXT NOT NULL, step_idx INTEGER NOT NULL, stream_id INTEGER, correlation_id TEXT, graph_id TEXT, graph_exec_id TEXT, context_id TEXT, start_ns INTEGER, end_ns INTEGER, dur_us REAL, enclosed_event_count INTEGER, enclosed_event_us REAL, enclosed_kernel_count INTEGER, enclosed_kernel_us REAL, raw_json TEXT);
CREATE TABLE traceloom_cuda_graph_envelope (envelope_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, graph_provider TEXT, graph_kind TEXT, envelope_idx INTEGER NOT NULL, graph_event_id TEXT NOT NULL, child_event_id TEXT NOT NULL, graph_step_idx INTEGER NOT NULL, child_step_idx INTEGER NOT NULL, relation TEXT NOT NULL, stream_relation TEXT, graph_id TEXT, graph_exec_id TEXT, graph_correlation_id TEXT, graph_start_ns INTEGER, graph_end_ns INTEGER, child_start_ns INTEGER, child_end_ns INTEGER, start_offset_us REAL, end_offset_us REAL, child_dur_us REAL, raw_json TEXT);
CREATE TABLE traceloom_aclgraph_reconstruction_region (region_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, graph_provider TEXT NOT NULL, candidate_id TEXT NOT NULL, region_order INTEGER NOT NULL, status TEXT NOT NULL, boundary_policy TEXT NOT NULL, order_policy TEXT NOT NULL, identity_policy TEXT NOT NULL, shape_policy TEXT NOT NULL, first_launch_occurrence_id INTEGER NOT NULL, last_launch_occurrence_id INTEGER NOT NULL, observed_launch_count INTEGER NOT NULL, expected_launch_count INTEGER NOT NULL, start_ns INTEGER NOT NULL, end_ns INTEGER NOT NULL, dur_us REAL NOT NULL, raw_json TEXT);
CREATE TABLE traceloom_graph_launch (launch_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, graph_provider TEXT NOT NULL, graph_event_id TEXT NOT NULL, anchor_id TEXT, replay_unit_id INTEGER NOT NULL, graph_template_id INTEGER NOT NULL, graph_launch_occurrence_id INTEGER NOT NULL, replay_body_template_id INTEGER NOT NULL, body_id INTEGER NOT NULL, member_order INTEGER NOT NULL, slot_order INTEGER, correlation_id TEXT, match_policy TEXT, association_policy TEXT, start_ns INTEGER, end_ns INTEGER, dur_us REAL, evidence_level TEXT NOT NULL);
CREATE TABLE traceloom_graph_body_member (member_id TEXT NOT NULL, launch_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, graph_provider TEXT NOT NULL, graph_event_id TEXT NOT NULL, replay_unit_id INTEGER NOT NULL, graph_template_id INTEGER NOT NULL, graph_launch_occurrence_id INTEGER NOT NULL, body_id INTEGER NOT NULL, replay_body_template_id INTEGER NOT NULL, member_order INTEGER NOT NULL, slot_order INTEGER, lane_ordinal INTEGER NOT NULL, task_ordinal INTEGER NOT NULL, kind TEXT NOT NULL, event_id TEXT NOT NULL, task_id INTEGER NOT NULL, source_table TEXT, source_row_id INTEGER, raw_task_id INTEGER, start_ns INTEGER, end_ns INTEGER, dur_us REAL, correlation_id TEXT, graph_node_id INTEGER, original_graph_node_id INTEGER, match_policy TEXT, association_policy TEXT, evidence_level TEXT NOT NULL);
CREATE TABLE traceloom_replay_cost_unit (cost_unit_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, replay_unit_id INTEGER NOT NULL, graph_template_id INTEGER NOT NULL, launch_member_count INTEGER NOT NULL, resolved_launch_count INTEGER NOT NULL, support_status TEXT NOT NULL, reason_codes TEXT NOT NULL);
CREATE TABLE traceloom_replay_cost_launch (launch_id TEXT NOT NULL, cost_unit_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, member_order INTEGER NOT NULL, graph_launch_occurrence_id INTEGER NOT NULL, composition_slot_id INTEGER NOT NULL, slot_role TEXT NOT NULL, slot_order INTEGER NOT NULL, replay_body_template_id INTEGER NOT NULL, body_id INTEGER NOT NULL, support_status TEXT NOT NULL, reason_code TEXT NOT NULL, member_count INTEGER NOT NULL, task_sum_ns INTEGER NOT NULL, busy_union_ns INTEGER NOT NULL, envelope_ns INTEGER NOT NULL, compute_ns INTEGER NOT NULL, communication_ns INTEGER NOT NULL, data_move_ns INTEGER NOT NULL, replay_unit_id INTEGER NOT NULL, graph_template_id INTEGER NOT NULL);
CREATE TABLE traceloom_replay_cost_stream (launch_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, stream_id INTEGER NOT NULL, lane_ordinal INTEGER NOT NULL, lane_consistent INTEGER NOT NULL, member_count INTEGER NOT NULL, task_sum_ns INTEGER NOT NULL, busy_union_ns INTEGER NOT NULL, compute_ns INTEGER NOT NULL, communication_ns INTEGER NOT NULL, data_move_ns INTEGER NOT NULL);
CREATE TABLE traceloom_replay_cost_member (member_id TEXT NOT NULL, launch_id TEXT NOT NULL, cost_unit_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, composition_slot_id INTEGER NOT NULL, slot_role TEXT NOT NULL, slot_order INTEGER NOT NULL, replay_body_template_id INTEGER NOT NULL, body_id INTEGER NOT NULL, stream_id INTEGER NOT NULL, lane_ordinal INTEGER NOT NULL, task_ordinal INTEGER NOT NULL, kind TEXT NOT NULL, event_id TEXT NOT NULL, identity TEXT NOT NULL, raw_task_id INTEGER NOT NULL, start_ns INTEGER NOT NULL, end_ns INTEGER NOT NULL, duration_ns INTEGER NOT NULL, relative_start_ns INTEGER NOT NULL, relative_end_ns INTEGER NOT NULL, scheduled_work_share_ppm INTEGER NOT NULL, scheduled_work_share_supported INTEGER NOT NULL, scheduled_work_denominator_body_task_sum_ns INTEGER NOT NULL);
CREATE TABLE traceloom_replay_cost_aggregate (aggregate_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, graph_template_id INTEGER NOT NULL, slot_role TEXT NOT NULL, aggregation_scope TEXT NOT NULL, replay_body_template_id INTEGER NOT NULL, stream_id INTEGER NOT NULL, task_ordinal INTEGER NOT NULL, identity TEXT NOT NULL, kind TEXT NOT NULL, member_occurrence_count INTEGER NOT NULL, replay_unit_count INTEGER NOT NULL, launch_member_count INTEGER NOT NULL, kind_consistent INTEGER NOT NULL, lane_consistent INTEGER NOT NULL, distribution_supported INTEGER NOT NULL, duration_p25_ns INTEGER NOT NULL, duration_median_ns INTEGER NOT NULL, duration_p75_ns INTEGER NOT NULL, scheduled_work_share_ppm INTEGER NOT NULL, scheduled_work_share_supported INTEGER NOT NULL, scheduled_work_denominator_body_task_sum_ns INTEGER NOT NULL);
CREATE TABLE traceloom_replay_cost_aggregate_member (aggregate_id TEXT NOT NULL, member_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, contributor_order INTEGER NOT NULL);
CREATE TABLE traceloom_replay_cost_issue (issue_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, code TEXT NOT NULL, replay_unit_id INTEGER NOT NULL, launch_id TEXT, detail TEXT NOT NULL);
CREATE TABLE traceloom_viz_node (node_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, view_name TEXT NOT NULL, local_node_id TEXT NOT NULL, path TEXT, node_type TEXT, kind TEXT, symbol TEXT, label TEXT, category TEXT, depth INTEGER, level INTEGER, repeat_label TEXT, repeat_count INTEGER, occurrence_count INTEGER, anchor_count INTEGER, anchors_per_occurrence REAL, first_anchor_idx INTEGER, last_anchor_idx INTEGER, compute_us REAL, comm_us REAL, idle_us REAL, total_us REAL, avg_compute_us REAL, avg_comm_us REAL, avg_idle_us REAL, avg_total_us REAL, self_us REAL, aux_events REAL, aux_us REAL, raw_json TEXT);
CREATE TABLE traceloom_viz_edge (parent_node_id TEXT NOT NULL, child_node_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, view_name TEXT NOT NULL, edge_order INTEGER NOT NULL, edge_kind TEXT, raw_json TEXT);
CREATE TABLE traceloom_viz_node_anchor (node_id TEXT NOT NULL, anchor_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, view_name TEXT NOT NULL, occurrence_idx INTEGER NOT NULL, anchor_order INTEGER NOT NULL, coverage_kind TEXT NOT NULL, repeat_context TEXT, compute_us REAL NOT NULL, comm_us REAL NOT NULL, idle_us REAL NOT NULL, total_us REAL NOT NULL, self_us REAL NOT NULL, aux_events REAL NOT NULL, aux_us REAL NOT NULL);
CREATE TABLE traceloom_anchor_primary_node (anchor_id TEXT NOT NULL, node_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, view_name TEXT NOT NULL, reason TEXT NOT NULL);
CREATE TABLE traceloom_loop_node (node_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, view_name TEXT NOT NULL, loop_rank INTEGER, repeat_label TEXT, repeat_count INTEGER, occurrence_count INTEGER, anchor_count INTEGER, total_us REAL, avg_total_us REAL, compute_us REAL, comm_us REAL, idle_us REAL, loop_total_pct REAL, raw_json TEXT);
CREATE TABLE traceloom_semantic_tree (tree_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, view_name TEXT NOT NULL, tree_kind TEXT NOT NULL, stem TEXT, root_node_id TEXT, schema_version TEXT, semantic_projection TEXT, macro_discovery TEXT, readable_macro_mode TEXT, auxiliary_attribution TEXT, raw_json TEXT);
CREATE TABLE traceloom_semantic_node (node_id TEXT NOT NULL, tree_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, view_name TEXT NOT NULL, tree_kind TEXT NOT NULL, local_node_id TEXT NOT NULL, parent_node_id TEXT, parent_local_node_id TEXT, preorder_idx INTEGER NOT NULL, sibling_order INTEGER NOT NULL, path TEXT, depth INTEGER, display_depth INTEGER, loop_depth INTEGER, node_type TEXT, semantic_kind TEXT, symbol TEXT, label TEXT, category TEXT, repeat_count INTEGER, occurrence_count INTEGER, anchor_count INTEGER, first_anchor_idx INTEGER, last_anchor_idx INTEGER, start_ns INTEGER, end_ns INTEGER, compute_us REAL, comm_us REAL, idle_us REAL, total_us REAL, avg_compute_us REAL, avg_comm_us REAL, avg_idle_us REAL, avg_total_us REAL, self_us REAL, aux_event_count REAL, aux_us REAL, hidden_aux_event_count REAL, hidden_aux_us REAL, raw_json TEXT);
CREATE TABLE traceloom_semantic_edge (parent_node_id TEXT NOT NULL, child_node_id TEXT NOT NULL, tree_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, view_name TEXT NOT NULL, tree_kind TEXT NOT NULL, edge_order INTEGER NOT NULL, edge_kind TEXT, raw_json TEXT);
CREATE TABLE traceloom_collective_global_link (candidate_collective_key TEXT NOT NULL, db_name TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, member_id TEXT NOT NULL, pair_id TEXT NOT NULL, local_node_id TEXT NOT NULL, occurrence_idx INTEGER NOT NULL, idx_in_occurrence INTEGER NOT NULL, op_type TEXT NOT NULL, anchor_id TEXT NOT NULL, event_id TEXT NOT NULL, source_table TEXT, source_key TEXT, connection_id TEXT, op_id TEXT, start_ns INTEGER, end_ns INTEGER, dur_us REAL, validation_status TEXT, confidence REAL);
CREATE TABLE traceloom_anchor_cost_breakdown (anchor_idx INTEGER NOT NULL, symbol TEXT NOT NULL, anchor_kind TEXT NOT NULL, total_us REAL NOT NULL, self_us REAL NOT NULL, aux_us REAL NOT NULL, graph_child_us REAL NOT NULL, residual_us REAL NOT NULL, raw_child_task_count INTEGER NOT NULL, top_ops TEXT NOT NULL, diagnostic_flags TEXT NOT NULL);
CREATE TABLE traceloom_structure_bubble_occurrence(
  bubble_id,
  db_idx INT,
  device_id INT,
  view_name TEXT,
  right_node_id TEXT,
  right_local_node_id TEXT,
  right_node_path TEXT,
  right_node_kind TEXT,
  right_node_symbol TEXT,
  right_occurrence_idx INT,
  repeat_context TEXT,
  structural_position_id TEXT,
  left_anchor_id TEXT,
  left_anchor_idx INT,
  left_anchor_symbol TEXT,
  left_anchor_role TEXT,
  left_anchor_start_ns INT,
  left_anchor_end_ns INT,
  right_anchor_id TEXT,
  right_anchor_idx INT,
  right_anchor_symbol TEXT,
  right_anchor_role TEXT,
  right_anchor_start_ns INT,
  right_anchor_end_ns INT,
  adjacent_anchor_gap_us,
  bubble_us,
  transition_compute_us,
  transition_comm_us,
  transition_total_us,
  transition_aux_events REAL,
  transition_aux_us,
  bubble_fraction_of_transition,
  host_interval_id TEXT,
  provider TEXT,
  host_clock_domain TEXT,
  scope_policy TEXT,
  host_observation_status TEXT,
  host_start_ns INT,
  host_end_ns INT,
  host_interval_us,
  api_association_semantics
);
CREATE TABLE traceloom_structure_bubble_position(
  db_idx INT,
  device_id INT,
  view_name TEXT,
  structural_position_id TEXT,
  right_node_id TEXT,
  right_local_node_id TEXT,
  right_node_path TEXT,
  right_node_kind TEXT,
  right_node_symbol TEXT,
  bubble_occurrence_count,
  supported_host_occurrence_count,
  missing_endpoint_occurrence_count,
  nonmonotonic_occurrence_count,
  other_unsupported_occurrence_count,
  total_bubble_us,
  avg_bubble_us,
  min_bubble_us,
  max_bubble_us,
  host_observation_coverage
);
CREATE TABLE traceloom_structure_bubble_api_occurrence(
  bubble_id,
  api_family TEXT,
  call_count INT,
  distinct_api_name_count INT,
  scheduled_call_us,
  scheduled_overlap_us
);
CREATE TABLE traceloom_structure_bubble_api_stats(
  db_idx INT,
  device_id INT,
  view_name TEXT,
  structural_position_id TEXT,
  right_node_id TEXT,
  right_local_node_id TEXT,
  right_node_symbol TEXT,
  bubble_occurrence_count,
  host_observable_occurrence_count,
  total_bubble_us,
  avg_bubble_us,
  min_bubble_us,
  max_bubble_us,
  api_family TEXT,
  presence_count,
  presence_fraction_of_all_bubbles,
  presence_fraction_of_observable_bubbles,
  host_observation_coverage,
  total_call_count,
  avg_calls_per_bubble,
  summed_distinct_api_names,
  avg_calls_per_observable_bubble,
  total_scheduled_call_us,
  avg_scheduled_call_us_per_bubble,
  total_scheduled_overlap_us,
  avg_scheduled_overlap_us_per_bubble,
  interpretation_note
);
ANALYZE sqlite_schema;
CREATE TABLE traceloom_evidence_role_policy (
  policy_id TEXT NOT NULL PRIMARY KEY, policy_version TEXT NOT NULL,
  manifest_schema TEXT NOT NULL, manifest_sha256 TEXT NOT NULL,
  provider_scopes TEXT NOT NULL, fallback_identity_role TEXT NOT NULL,
  fallback_cost_treatment TEXT NOT NULL,
  fallback_context_treatment TEXT NOT NULL,
  fallback_provenance_treatment TEXT NOT NULL,
  missing_evidence_behavior TEXT NOT NULL, input_format TEXT NOT NULL,
  input_sources TEXT NOT NULL, effective_config_sha256 TEXT NOT NULL,
  config_overrides TEXT NOT NULL, config_override_contract TEXT NOT NULL);
INSERT INTO traceloom_evidence_role_policy VALUES('traceloom.default.accelerator-task-projection','1','traceloom.evidence-role-policy/v1','a532a2630a94641243c0080a9f429a393b794ee8f9269e9c2e62e6a4a41fa73d','ascend,cuda,hygon','unknown_anchor','retained_for_attribution','retained','retained','continue_or_fallback','flat_tsv','fixture://rules/default_signal_classification_rules.tsv','a532a2630a94641243c0080a9f429a393b794ee8f9269e9c2e62e6a4a41fa73d','','explicit --classification-rules replaces environment/default; --extend-classification-rules is applied afterward; repeatable --classification-rule-override entries overwrite typed rule fields last');
CREATE TABLE traceloom_evidence_role_rule (
  policy_id TEXT NOT NULL, rule_id TEXT NOT NULL, rule_class TEXT NOT NULL,
  priority INTEGER NOT NULL, declaration_order INTEGER NOT NULL,
  provider_scope TEXT NOT NULL, source_domain TEXT NOT NULL,
  match_field TEXT NOT NULL, match_kind TEXT NOT NULL, pattern TEXT NOT NULL,
  role TEXT NOT NULL, required_fields TEXT NOT NULL,
  structural_participation TEXT NOT NULL, cost_treatment TEXT NOT NULL,
  context_treatment TEXT NOT NULL, provenance_treatment TEXT NOT NULL,
  missing_evidence_behavior TEXT NOT NULL,
  concrete_identity_behavior TEXT NOT NULL, note TEXT NOT NULL,
  source_line INTEGER NOT NULL,
  PRIMARY KEY(policy_id, rule_id));
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.model.maintaince.9af5400a','positive_policy',100,0,'ascend','task','task_type','exact','MODEL_MAINTAINCE','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Ascend maintenance control task',11);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.model.maintenance.9724cc1f','positive_policy',100,1,'ascend','task','task_type','exact','MODEL_MAINTENANCE','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Corrected maintenance spelling',12);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','hygon.aux.task.type.hip.kernel.aux.6c869864','positive_policy',100,2,'hygon','task','task_type','exact','HIP_KERNEL_AUX','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Hygon auxiliary kernel',13);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','cuda.aux.task.type.cuda.kernel.aux.c4ce98bf','positive_policy',100,3,'cuda','task','task_type','exact','CUDA_KERNEL_AUX','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','CUDA auxiliary kernel',14);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','cuda.aux.task.type.cuda.runtime.aux.2f99050d','positive_policy',100,4,'cuda','task','task_type','exact','CUDA_RUNTIME_AUX','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','CUDA runtime bookkeeping row',15);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','cuda.aux.task.type.cuda.memcpy.aux.3fd99cc0','positive_policy',100,5,'cuda','task','task_type','exact','CUDA_MEMCPY_AUX','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','CUDA memcpy bookkeeping row',16);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','cuda.aux.task.type.cuda.sync.aux.91b696b9','positive_policy',100,6,'cuda','task','task_type','exact','CUDA_SYNC_AUX','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','CUDA synchronization observation',17);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','cuda.aux.task.type.cuda.event.aux.d6c4be5a','positive_policy',100,7,'cuda','task','task_type','exact','CUDA_EVENT_AUX','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','CUDA event identity observation',18);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','cuda.anchor.task.type.cuda.kernel.51cc7472','positive_policy',95,8,'cuda','task','task_type','exact','CUDA_KERNEL','anchor','task_type','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Preserve unknown CUDA kernels as execution anchors',19);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','cuda.anchor.task.type.cuda.collective.kernel.d758d32a','positive_policy',95,9,'cuda','task','task_type','exact','CUDA_COLLECTIVE_KERNEL','anchor','task_type','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Preserve CUDA collective kernels as anchors',20);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.blob.hccl.aiv.sync.002d332c','positive_policy',90,10,'ascend','task','blob','contains','hccl_aiv_sync','auxiliary','blob','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','HCCL synchronization helper',21);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.blob.hccl.aic.sync.2aa09c61','positive_policy',90,11,'ascend','task','blob','contains','hccl_aic_sync','auxiliary','blob','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','HCCL synchronization helper',22);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.blob.aiv.sync.01fa9cb6','positive_policy',90,12,'ascend','task','blob','contains','aiv_sync','auxiliary','blob','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Device synchronization helper',23);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.blob.aic.sync.b6ee929c','positive_policy',90,13,'ascend','task','blob','contains','aic_sync','auxiliary','blob','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Device synchronization helper',24);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.anchor.operator.dispatchffncombinebf16.dccb9f76','positive_policy',85,14,'ascend','task','operator','exact','DispatchFFNCombineBF16','anchor','operator','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Fused MoE dispatch, FFN, and combine kernel',25);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.nccl.4b39d070','positive_policy',80,15,'any','task','blob','contains','nccl','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','NCCL collective',26);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.allreduce.2167e26c','positive_policy',80,16,'any','task','blob','contains','allreduce','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','AllReduce collective',27);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.all.reduce.b3275ab9','positive_policy',80,17,'any','task','blob','contains','all_reduce','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','AllReduce collective',28);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.allgather.c196ad8f','positive_policy',80,18,'any','task','blob','contains','allgather','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','AllGather collective',29);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.all.gather.32cd1271','positive_policy',80,19,'any','task','blob','contains','all_gather','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','AllGather collective',30);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.reducescatter.bebf2569','positive_policy',80,20,'any','task','blob','contains','reducescatter','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','ReduceScatter collective',31);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.reduce.scatter.27cfbf12','positive_policy',80,21,'any','task','blob','contains','reduce_scatter','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','ReduceScatter collective',32);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.broadcast.3676c494','positive_policy',80,22,'any','task','blob','contains','broadcast','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Broadcast collective',33);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.alltoall.02e38be4','positive_policy',80,23,'any','task','blob','contains','alltoall','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','AllToAll collective',34);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.all.to.all.ee8b7b7b','positive_policy',80,24,'any','task','blob','contains','all_to_all','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','AllToAll collective',35);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.all.to.all.06ba9429','positive_policy',80,25,'any','task','blob','contains','all-to-all','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','AllToAll collective',36);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.all2all.68223422','positive_policy',80,26,'any','task','blob','contains','all2all','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','AllToAll collective',37);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.transparent.task.type.ai.core.7897ff1d','positive_policy',70,27,'ascend','task','task_type','exact','AI_CORE','transparent','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','defer_to_identity_rules','Generic device execution container',38);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.model.execute.85a30ce3','positive_policy',70,28,'ascend','task','task_type','exact','MODEL_EXECUTE','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Runtime control task',39);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.capture.record.2d7abd67','positive_policy',70,29,'ascend','task','task_type','exact','CAPTURE_RECORD','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Graph capture control task',40);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.capture.wait.03d88a52','positive_policy',70,30,'ascend','task','task_type','exact','CAPTURE_WAIT','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Graph capture control task',41);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.event.record.5f138602','positive_policy',70,31,'ascend','task','task_type','exact','EVENT_RECORD','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Event control task',42);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.event.wait.28ab0f94','positive_policy',70,32,'ascend','task','task_type','exact','EVENT_WAIT','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Event control task',43);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.mem.write.value.3134c490','positive_policy',70,33,'ascend','task','task_type','exact','MEM_WRITE_VALUE','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Memory control task',44);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.memcpy.5aa0fdd9','positive_policy',70,34,'ascend','task','task_type','exact','MEMCPY','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Data movement helper',45);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','hygon.aux.task.type.memcpy.5aa0fdd9','positive_policy',70,35,'hygon','task','task_type','exact','MEMCPY','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Hygon data movement helper',46);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.memcpy.async.bb5e99f6','positive_policy',70,36,'ascend','task','task_type','exact','MEMCPY_ASYNC','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Data movement helper',47);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.notify.ad3d9279','positive_policy',70,37,'ascend','task','task_type','exact','NOTIFY','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Notification control task',48);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.notify.record.21a93893','positive_policy',70,38,'ascend','task','task_type','exact','NOTIFY_RECORD','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Notification control task',49);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.notify.wait.717e33e3','positive_policy',70,39,'ascend','task','task_type','exact','NOTIFY_WAIT','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Notification control task',50);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.sdma.5b6d3390','positive_policy',70,40,'ascend','task','task_type','exact','SDMA','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','DMA helper',51);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.task.timeout.set.6785cbc7','positive_policy',70,41,'ascend','task','task_type','exact','TASK_TIMEOUT_SET','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Runtime control task',52);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','ascend.aux.task.type.write.value.b56a626a','positive_policy',70,42,'ascend','task','task_type','exact','WRITE_VALUE','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Runtime control task',53);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.matmul.b362963b','positive_policy',60,43,'any','task','blob','contains','matmul','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Matrix multiplication',54);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.batchmatmul.5655fdd5','positive_policy',60,44,'any','task','blob','contains','batchmatmul','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Batched matrix multiplication',55);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.gemm.e8de2070','positive_policy',60,45,'any','task','blob','contains','gemm','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','General matrix multiplication',56);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.conv.46c882d1','positive_policy',60,46,'any','task','blob','contains','conv','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Convolution',57);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.flashattention.d2fb6028','positive_policy',60,47,'any','task','blob','contains','flashattention','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Flash attention',58);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.fusedinferattention.79e72c9f','positive_policy',60,48,'any','task','blob','contains','fusedinferattention','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Fused inference attention',59);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.pagedattention.715e4e06','positive_policy',60,49,'any','task','blob','contains','pagedattention','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Paged attention',60);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.attention.06fb6208','positive_policy',60,50,'any','task','blob','contains','attention','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Attention',61);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.rmsnorm.eb7e4edc','positive_policy',60,51,'any','task','blob','contains','rmsnorm','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','RMS normalization',62);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.rms.norm.bcf35436','positive_policy',60,52,'any','task','blob','contains','rms_norm','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','RMS normalization',63);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.layernorm.f7fe08f1','positive_policy',60,53,'any','task','blob','contains','layernorm','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Layer normalization',64);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.layer.norm.8136f71c','positive_policy',60,54,'any','task','blob','contains','layer_norm','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Layer normalization',65);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.swiglu.73e3dc97','positive_policy',60,55,'any','task','blob','contains','swiglu','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','SwiGLU',66);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.siluandmul.f32a17a4','positive_policy',60,56,'any','task','blob','contains','siluandmul','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','SiLU multiply',67);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.moe.caf80514','positive_policy',60,57,'any','task','blob','contains','moe','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Mixture of experts',68);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.ffn.00eef783','positive_policy',60,58,'any','task','blob','contains','ffn','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Feed-forward network',69);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.rotary.6d4c092d','positive_policy',60,59,'any','task','blob','contains','rotary','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Rotary embedding',70);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.rope.3710deb9','positive_policy',60,60,'any','task','blob','contains','rope','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Rotary embedding',71);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','any.anchor.blob.mamba.3b60c245','positive_policy',60,61,'any','task','blob','contains','mamba','anchor','blob','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Mamba operator',72);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','fallback.unknown_observation','fallback',0,62,'any','task','none','fallback','','unknown_anchor','','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Unknown-first fallback when no supported rule admits an event',0);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','system.protected_composite','protected_membership',0,0,'any','system','membership','typed','','protected_boundary','replay_unit_membership','atomic_boundary','retained_for_attribution','retained','retained','continue_or_fallback','apply','Provider composite membership protects generic discovery boundaries',0);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','system.communication_anchor','provider_relation',0,0,'any','system','membership','typed','','anchor','communication_membership','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Communication membership is represented by a communication anchor',0);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','system.event_reconciliation','provider_relation',0,0,'any','system','membership','typed','','anchor','event_reconciliation_membership','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','A reconciled timing envelope is represented by its canonical anchor',0);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','system.analysis_config_exclusion','analysis_config',0,0,'any','system','membership','typed','','auxiliary','task_type','excluded','retained_for_attribution','retained','retained','continue_or_fallback','apply','Explicit analysis configuration excludes this task type',0);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','system.unfiltered_task_anchor','analysis_config',0,0,'any','system','membership','typed','','anchor','task_event','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','Auxiliary filtering is disabled for this analysis',0);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','system.existing_event_anchor','structural_membership',0,0,'any','system','membership','typed','','anchor','anchor','identity','retained_for_attribution','retained','retained','continue_or_fallback','apply','A non-task normalized event owns an anchor',0);
INSERT INTO traceloom_evidence_role_rule VALUES('traceloom.default.accelerator-task-projection','system.unsupported_event_kind','unsupported',0,0,'any','system','membership','typed','','unsupported','normalized_event','not_applicable','retained_for_attribution','retained','retained','continue_or_fallback','apply','The normalized event is retained but is not a projection candidate',0);
CREATE TABLE traceloom_evidence_role_decision (
  decision_id TEXT NOT NULL PRIMARY KEY, db_idx INTEGER NOT NULL,
  device_id INTEGER NOT NULL, task_id INTEGER, event_id TEXT NOT NULL,
  source_ref_id INTEGER NOT NULL, source_domain TEXT NOT NULL,
  input_provider_scope TEXT NOT NULL, policy_id TEXT NOT NULL,
  policy_version TEXT NOT NULL, manifest_sha256 TEXT NOT NULL,
  policy_role TEXT, final_role TEXT NOT NULL, rule_id TEXT NOT NULL,
  rule_class TEXT NOT NULL, matched_rule INTEGER NOT NULL,
  priority INTEGER NOT NULL, declaration_order INTEGER NOT NULL,
  policy_structural_participation TEXT NOT NULL,
  effective_structural_participation TEXT NOT NULL,
  support_state TEXT NOT NULL, reason_code TEXT NOT NULL,
  available_fields TEXT NOT NULL, required_fields TEXT NOT NULL,
  missing_required_fields TEXT NOT NULL,
  missing_capability_rule_ids TEXT NOT NULL, cost_treatment TEXT NOT NULL,
  context_treatment TEXT NOT NULL, provenance_treatment TEXT NOT NULL,
  source_table TEXT NOT NULL, source_key TEXT NOT NULL,
  start_ns INTEGER NOT NULL, end_ns INTEGER NOT NULL,
  duration_ns INTEGER NOT NULL);
CREATE TABLE traceloom_evidence_role_placement (
  decision_id TEXT NOT NULL, placement_order INTEGER NOT NULL,
  placement_kind TEXT NOT NULL, placement_id TEXT NOT NULL,
  owner_id TEXT, relation_name TEXT NOT NULL, support_state TEXT NOT NULL,
  reason_code TEXT NOT NULL,
  PRIMARY KEY(decision_id, placement_order));
CREATE TABLE traceloom_protected_interval (
  protected_interval_id TEXT NOT NULL PRIMARY KEY,
  db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, kind TEXT NOT NULL,
  boundary_policy TEXT NOT NULL, first_anchor_id TEXT NOT NULL,
  last_anchor_id TEXT NOT NULL, replay_unit_id TEXT,
  evidence_source_ref_id INTEGER NOT NULL, start_ns INTEGER NOT NULL,
  end_ns INTEGER NOT NULL, support_state TEXT NOT NULL,
  reason_code TEXT NOT NULL);
CREATE TABLE traceloom_evidence_role_issue (
  issue_id TEXT NOT NULL PRIMARY KEY, decision_id TEXT NOT NULL,
  code TEXT NOT NULL, support_state TEXT NOT NULL,
  related_ids TEXT NOT NULL);
CREATE UNIQUE INDEX idx_traceloom_runtime_call_id ON traceloom_runtime_call(runtime_call_id);
CREATE UNIQUE INDEX idx_traceloom_device_work_id ON traceloom_device_work(device_work_id);
CREATE UNIQUE INDEX idx_traceloom_runtime_relation_id ON traceloom_runtime_device_relation(relation_id);
CREATE INDEX idx_traceloom_event_device_step ON traceloom_event(db_idx, device_id, step_idx);
CREATE INDEX idx_traceloom_event_id ON traceloom_event(event_id);
CREATE INDEX idx_traceloom_event_identity ON traceloom_event(event_id, db_idx, device_id);
CREATE INDEX idx_traceloom_event_source_lookup ON traceloom_event_source(source_table, source_key);
CREATE INDEX idx_traceloom_runtime_call_time ON traceloom_runtime_call(provider, clock_domain, start_ns, end_ns);
CREATE INDEX idx_traceloom_runtime_call_correlation ON traceloom_runtime_call(provider, correlation_id);
CREATE INDEX idx_traceloom_device_work_event ON traceloom_device_work(event_id);
CREATE INDEX idx_traceloom_device_work_graph ON traceloom_device_work(graph_launch_occurrence_id);
CREATE INDEX idx_traceloom_runtime_relation_call ON traceloom_runtime_device_relation(runtime_call_id, support_state);
CREATE INDEX idx_traceloom_runtime_relation_work ON traceloom_runtime_device_relation(device_work_id, support_state);
CREATE INDEX idx_traceloom_anchor_runtime_anchor ON traceloom_anchor_runtime_relation(anchor_id, relation_id);
CREATE INDEX idx_traceloom_anchor_runtime_call ON traceloom_anchor_runtime_relation(runtime_call_id, anchor_id);
CREATE UNIQUE INDEX idx_traceloom_anchor_host_interval_id ON traceloom_anchor_host_interval(interval_id);
CREATE INDEX idx_traceloom_anchor_host_interval_left ON traceloom_anchor_host_interval(db_idx, device_id, left_anchor_id);
CREATE UNIQUE INDEX idx_traceloom_anchor_host_activity_interval ON traceloom_anchor_host_activity(interval_id, observed_order);
CREATE INDEX idx_traceloom_anchor_host_activity_call ON traceloom_anchor_host_activity(runtime_call_id, interval_id);
CREATE UNIQUE INDEX idx_traceloom_anchor_host_api_summary ON traceloom_anchor_host_api_summary(interval_id, api_family);
CREATE INDEX idx_traceloom_anchor_host_interval_right ON traceloom_anchor_host_interval(db_idx, device_id, right_anchor_id);
CREATE INDEX idx_traceloom_anchor_device_idx ON traceloom_anchor(db_idx, device_id, anchor_idx);
CREATE INDEX idx_traceloom_anchor_key ON traceloom_anchor(anchor_id, db_idx, device_id);
CREATE UNIQUE INDEX idx_traceloom_symbol_rule_identity ON traceloom_symbol_normalization_rule(policy_id, policy_version, rule_id);
CREATE UNIQUE INDEX idx_traceloom_anchor_symbol_normalization_anchor ON traceloom_anchor_symbol_normalization(anchor_id, db_idx, device_id);
CREATE INDEX idx_traceloom_symbol_normalization_symbol ON traceloom_anchor_symbol_normalization(db_idx, device_id, structural_symbol, observed_symbol);
CREATE INDEX idx_traceloom_symbol_normalization_rule ON traceloom_anchor_symbol_normalization(rule_id, outcome);
CREATE INDEX idx_traceloom_symbol_normalization_source ON traceloom_anchor_symbol_normalization(source_path, source_table, source_key);
CREATE INDEX idx_traceloom_aux_anchor ON traceloom_aux_link(anchor_id);
CREATE INDEX idx_traceloom_cuda_graph_replay_exec ON traceloom_cuda_graph_replay(db_idx, device_id, graph_exec_id);
CREATE INDEX idx_traceloom_cuda_graph_envelope_graph ON traceloom_cuda_graph_envelope(graph_event_id);
CREATE INDEX idx_traceloom_cuda_graph_envelope_child ON traceloom_cuda_graph_envelope(child_event_id);
CREATE INDEX idx_traceloom_aclgraph_region_status ON traceloom_aclgraph_reconstruction_region(db_idx, device_id, status);
CREATE INDEX idx_traceloom_graph_launch_node ON traceloom_graph_launch(db_idx, device_id, graph_event_id);
CREATE INDEX idx_traceloom_graph_launch_anchor ON traceloom_graph_launch(anchor_id);
CREATE INDEX idx_traceloom_graph_launch_identity ON traceloom_graph_launch(launch_id, db_idx, device_id);
CREATE INDEX idx_traceloom_graph_launch_anchor_identity ON traceloom_graph_launch(anchor_id, db_idx, device_id);
CREATE INDEX idx_traceloom_graph_body_member_launch ON traceloom_graph_body_member(launch_id);
CREATE INDEX idx_traceloom_graph_body_member_launch_identity ON traceloom_graph_body_member(launch_id, db_idx, device_id);
CREATE INDEX idx_traceloom_graph_body_member_event ON traceloom_graph_body_member(event_id);
CREATE INDEX idx_traceloom_graph_body_member_event_identity ON traceloom_graph_body_member(event_id, db_idx, device_id);
CREATE INDEX idx_traceloom_graph_body_member_node ON traceloom_graph_body_member(graph_node_id);
CREATE INDEX idx_traceloom_replay_cost_member_event ON traceloom_replay_cost_member(event_id, db_idx, device_id);
CREATE INDEX idx_traceloom_replay_cost_member_launch ON traceloom_replay_cost_member(launch_id, db_idx, device_id, lane_ordinal, task_ordinal);
CREATE INDEX idx_traceloom_replay_cost_aggregate_hotspot ON traceloom_replay_cost_aggregate(db_idx, device_id, duration_median_ns DESC);
CREATE INDEX idx_traceloom_replay_cost_contributor ON traceloom_replay_cost_aggregate_member(aggregate_id, contributor_order);
CREATE INDEX idx_traceloom_node_anchor_node ON traceloom_viz_node_anchor(node_id);
CREATE INDEX idx_traceloom_viz_node_id ON traceloom_viz_node(node_id);
CREATE INDEX idx_traceloom_node_anchor_occurrence ON traceloom_viz_node_anchor(node_id, db_idx, device_id, view_name, occurrence_idx);
CREATE INDEX idx_traceloom_node_anchor_anchor ON traceloom_viz_node_anchor(anchor_id);
CREATE INDEX idx_traceloom_semantic_node_tree_order ON traceloom_semantic_node(tree_id, preorder_idx);
CREATE INDEX idx_traceloom_semantic_node_parent ON traceloom_semantic_node(parent_node_id);
CREATE INDEX idx_traceloom_semantic_edge_tree ON traceloom_semantic_edge(tree_id, edge_order);
CREATE INDEX idx_traceloom_collective_key ON traceloom_collective_global_link(candidate_collective_key);
CREATE INDEX idx_traceloom_collective_pair ON traceloom_collective_global_link(pair_id, occurrence_idx, op_type, idx_in_occurrence);
CREATE VIEW traceloom_v_runtime_device AS SELECT r.*, COALESCE(c.provider, w.provider) AS provider, c.clock_domain, c.source_table AS runtime_source_table, c.source_key AS runtime_source_key, c.start_ns AS runtime_start_ns, c.end_ns AS runtime_end_ns, c.dur_us AS runtime_dur_us, c.api_name, c.api_type, c.process_id, c.thread_id, c.global_tid, c.context_id AS runtime_context_id, w.device_id, w.work_kind, w.event_id, w.task_id, w.graph_launch_occurrence_id, w.source_table AS device_source_table, w.source_key AS device_source_key, w.start_ns AS device_start_ns, w.end_ns AS device_end_ns, w.dur_us AS device_dur_us, w.symbol AS device_symbol FROM traceloom_runtime_device_relation r LEFT JOIN traceloom_runtime_call c ON c.runtime_call_id = r.runtime_call_id LEFT JOIN traceloom_device_work w ON w.device_work_id = r.device_work_id;
CREATE VIEW traceloom_v_sync_runtime_call AS SELECT device_work_id AS sync_action_id, device_symbol AS sync_kind, * FROM traceloom_v_runtime_device WHERE (provider = 'cuda' AND device_source_table = 'CUPTI_ACTIVITY_KIND_SYNCHRONIZATION') OR (provider = 'ascend' AND device_source_table IN ('TASK', 'AscendTask') AND device_symbol IN ('EVENT_RECORD', 'EVENT_WAIT'));
CREATE VIEW traceloom_v_anchor_runtime_call AS SELECT a.anchor_id, a.db_idx, a.device_id, a.anchor_idx, a.symbol AS anchor_symbol, a.role AS anchor_role, a.start_ns AS anchor_start_ns, a.end_ns AS anchor_end_ns, l.endpoint_kind, d.* FROM traceloom_anchor a JOIN traceloom_anchor_runtime_relation l ON l.anchor_id = a.anchor_id JOIN traceloom_v_runtime_device d ON d.relation_id = l.relation_id;
CREATE VIEW traceloom_v_node_runtime_call AS SELECT na.node_id, n.local_node_id, na.view_name, na.occurrence_idx, na.anchor_order, na.coverage_kind, na.repeat_context, a.anchor_idx, d.* FROM traceloom_viz_node_anchor na JOIN traceloom_viz_node n ON n.node_id = na.node_id AND n.db_idx = na.db_idx AND n.device_id = na.device_id AND n.view_name = na.view_name JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id AND a.db_idx = na.db_idx AND a.device_id = na.device_id JOIN traceloom_anchor_runtime_relation l ON l.anchor_id = na.anchor_id JOIN traceloom_v_runtime_device d ON d.relation_id = l.relation_id;
CREATE VIEW traceloom_v_aux_runtime_call AS SELECT l.anchor_id, l.aux_event_id, l.aux_order, l.aux_kind, l.aux_dur_us, d.* FROM traceloom_aux_link l JOIN traceloom_device_work w ON w.event_id = l.aux_event_id JOIN traceloom_v_runtime_device d ON d.device_work_id = w.device_work_id;
CREATE VIEW traceloom_v_anchor_host_interval AS SELECT * FROM traceloom_anchor_host_interval;
CREATE VIEW traceloom_v_anchor_host_activity AS SELECT i.*, c.runtime_call_id AS observed_runtime_call_id, c.api_name, c.api_type, c.start_ns AS observed_start_ns, c.end_ns AS observed_end_ns, c.dur_us AS observed_dur_us, ROUND((MIN(c.end_ns, i.host_end_ns) - MAX(c.start_ns, i.host_start_ns)) / 1000.0, 3) AS observed_overlap_us, CASE WHEN c.start_ns >= i.host_start_ns AND c.end_ns <= i.host_end_ns THEN 'contained' ELSE 'boundary_overlap' END AS interval_relation, c.process_id AS observed_process_id, c.thread_id AS observed_thread_id, c.source_table AS observed_source_table, c.source_key AS observed_source_key, a.observed_order FROM traceloom_anchor_host_activity a JOIN traceloom_v_anchor_host_interval i ON i.interval_id = a.interval_id JOIN traceloom_runtime_call c ON c.runtime_call_id = a.runtime_call_id;
CREATE VIEW traceloom_v_node_host_interval AS SELECT na.node_id, n.local_node_id, na.view_name, na.occurrence_idx, na.anchor_order, na.coverage_kind, na.repeat_context, a.anchor_idx, next.anchor_idx AS right_anchor_idx, next.symbol AS right_anchor_symbol, next.role AS right_anchor_role, ROUND((h.host_end_ns - h.host_start_ns) / 1000.0, 3) AS host_interval_us, 'after_anchor_interval' AS placement_semantics, h.* FROM traceloom_viz_node_anchor na JOIN traceloom_viz_node n ON n.node_id = na.node_id AND n.db_idx = na.db_idx AND n.device_id = na.device_id AND n.view_name = na.view_name JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id AND a.db_idx = na.db_idx AND a.device_id = na.device_id JOIN traceloom_v_anchor_host_interval h ON h.left_anchor_id = na.anchor_id AND h.db_idx = na.db_idx AND h.device_id = na.device_id JOIN traceloom_anchor next ON next.anchor_id = h.right_anchor_id AND next.db_idx = h.db_idx AND next.device_id = h.device_id;
CREATE VIEW traceloom_v_node_host_activity AS SELECT i.*, c.runtime_call_id AS observed_runtime_call_id, c.api_name, c.api_type, c.start_ns AS observed_start_ns, c.end_ns AS observed_end_ns, c.dur_us AS observed_dur_us, ROUND((MIN(c.end_ns, i.host_end_ns) - MAX(c.start_ns, i.host_start_ns)) / 1000.0, 3) AS observed_overlap_us, CASE WHEN c.start_ns >= i.host_start_ns AND c.end_ns <= i.host_end_ns THEN 'contained' ELSE 'boundary_overlap' END AS interval_relation, c.process_id AS observed_process_id, c.thread_id AS observed_thread_id, c.source_table AS observed_source_table, c.source_key AS observed_source_key, a.observed_order FROM traceloom_v_node_host_interval i JOIN traceloom_anchor_host_activity a ON a.interval_id = i.interval_id JOIN traceloom_runtime_call c ON c.runtime_call_id = a.runtime_call_id;
CREATE UNIQUE INDEX idx_traceloom_structure_bubble_id ON traceloom_structure_bubble_occurrence(bubble_id);
CREATE INDEX idx_traceloom_structure_bubble_transition ON traceloom_structure_bubble_occurrence(db_idx, device_id, view_name, structural_position_id, bubble_us DESC);
CREATE INDEX idx_traceloom_structure_bubble_host_interval ON traceloom_structure_bubble_occurrence(host_interval_id);
CREATE VIEW traceloom_v_structure_bubble_occurrence AS SELECT * FROM traceloom_structure_bubble_occurrence;
CREATE UNIQUE INDEX idx_traceloom_structure_bubble_position ON traceloom_structure_bubble_position(db_idx, device_id, view_name, structural_position_id);
CREATE INDEX idx_traceloom_structure_bubble_position_hotspot ON traceloom_structure_bubble_position(total_bubble_us DESC, bubble_occurrence_count DESC);
CREATE VIEW traceloom_v_structure_bubble_position AS SELECT * FROM traceloom_structure_bubble_position;
CREATE VIEW traceloom_v_structure_bubble_runtime_call AS SELECT bubble.*, call.runtime_call_id, call.api_name, call.api_type, call.start_ns AS runtime_start_ns, call.end_ns AS runtime_end_ns, call.dur_us AS runtime_dur_us, ROUND((MIN(call.end_ns, bubble.host_end_ns) - MAX(call.start_ns, bubble.host_start_ns)) / 1000.0, 3) AS observed_overlap_us, CASE WHEN call.start_ns >= bubble.host_start_ns AND call.end_ns <= bubble.host_end_ns THEN 'contained' ELSE 'boundary_overlap' END AS interval_relation, call.process_id, call.thread_id, call.source_table AS runtime_source_table, call.source_key AS runtime_source_key, activity.observed_order, CASE WHEN LOWER(COALESCE(call.api_name, '')) GLOB 'acl*' OR LOWER(COALESCE(call.api_name, '')) GLOB 'cuda*' OR LOWER(COALESCE(call.api_name, '')) GLOB 'hip*' THEN 'public' ELSE 'provider_internal_or_unknown' END AS api_layer, CASE WHEN LOWER(COALESCE(call.api_name, '')) LIKE '%wait%' THEN 'wait' WHEN LOWER(COALESCE(call.api_name, '')) LIKE '%synchronize%' THEN 'synchronize' WHEN LOWER(COALESCE(call.api_name, '')) LIKE '%query%' THEN 'query' WHEN LOWER(COALESCE(call.api_name, '')) LIKE '%eventrecord%' OR LOWER(COALESCE(call.api_name, '')) LIKE '%recordevent%' THEN 'event_record' WHEN (LOWER(COALESCE(call.api_name, '')) LIKE '%eventcreate%' OR LOWER(COALESCE(call.api_name, '')) LIKE '%createevent%' OR LOWER(COALESCE(call.api_name, '')) LIKE '%eventdestroy%' OR LOWER(COALESCE(call.api_name, '')) LIKE '%destroyevent%') THEN 'event_lifecycle' WHEN LOWER(COALESCE(call.api_name, '')) LIKE '%graphlaunch%' OR LOWER(COALESCE(call.api_name, '')) LIKE '%aclmdlriexecuteasync%' THEN 'graph_launch' WHEN LOWER(COALESCE(call.api_name, '')) LIKE '%launch%' THEN 'launch' WHEN LOWER(COALESCE(call.api_name, '')) LIKE '%memcpy%' OR LOWER(COALESCE(call.api_name, '')) LIKE '%memset%' OR LOWER(COALESCE(call.api_name, '')) LIKE '%inplacecopy%' THEN 'memory' WHEN LOWER(COALESCE(call.api_name, '')) LIKE '%capture%' OR LOWER(COALESCE(call.api_name, '')) LIKE '%graph%' THEN 'graph_control' ELSE 'other' END AS api_family FROM traceloom_structure_bubble_occurrence bubble JOIN traceloom_anchor_host_activity activity ON activity.interval_id = bubble.host_interval_id JOIN traceloom_runtime_call call ON call.runtime_call_id = activity.runtime_call_id WHERE bubble.host_observation_status = 'supported_ordered';
CREATE UNIQUE INDEX idx_traceloom_structure_bubble_api_occurrence ON traceloom_structure_bubble_api_occurrence(bubble_id, api_family);
CREATE VIEW traceloom_v_structure_bubble_api_occurrence AS SELECT * FROM traceloom_structure_bubble_api_occurrence;
CREATE UNIQUE INDEX idx_traceloom_structure_bubble_api_stats ON traceloom_structure_bubble_api_stats(db_idx, device_id, view_name, structural_position_id, api_family);
CREATE INDEX idx_traceloom_structure_bubble_api_hotspot ON traceloom_structure_bubble_api_stats(total_bubble_us DESC, bubble_occurrence_count DESC);
CREATE VIEW traceloom_v_structure_bubble_api_stats AS SELECT * FROM traceloom_structure_bubble_api_stats;
CREATE VIEW traceloom_v_structure_bubble_host_context AS SELECT position.*, stats.api_family, stats.presence_count, stats.presence_fraction_of_all_bubbles, stats.presence_fraction_of_observable_bubbles, stats.total_call_count, stats.avg_calls_per_bubble, stats.summed_distinct_api_names, stats.avg_calls_per_observable_bubble, stats.total_scheduled_call_us, stats.avg_scheduled_call_us_per_bubble, stats.total_scheduled_overlap_us, stats.avg_scheduled_overlap_us_per_bubble, COALESCE(stats.interpretation_note, 'no compatible host API-family observation; inspect typed host support counts') AS interpretation_note FROM traceloom_structure_bubble_position position LEFT JOIN traceloom_structure_bubble_api_stats stats ON stats.db_idx = position.db_idx AND stats.device_id = position.device_id AND stats.view_name = position.view_name AND stats.structural_position_id = position.structural_position_id;
CREATE VIEW traceloom_v_event_reconciliation AS SELECT m.*, d.policy_id, d.policy_version, d.rule_id, d.status, d.reason_code, d.canonical_event_id, d.envelope_event_id, d.canonical_anchor_id, d.canonical_start_ns, d.canonical_end_ns, d.contained_fraction, e.symbol AS observed_symbol, e.start_ns AS observed_start_ns, e.end_ns AS observed_end_ns, e.dur_us AS observed_dur_us, a.symbol AS canonical_symbol, a.start_ns AS canonical_anchor_start_ns, a.end_ns AS canonical_anchor_end_ns, a.dur_us AS canonical_anchor_dur_us FROM traceloom_event_reconciliation_member m JOIN traceloom_event_reconciliation_decision d ON d.decision_id = m.decision_id AND d.db_idx = m.db_idx LEFT JOIN traceloom_event e ON e.event_id = m.event_id AND e.db_idx = m.db_idx LEFT JOIN traceloom_anchor a ON a.anchor_id = d.canonical_anchor_id AND a.db_idx = d.db_idx;
CREATE VIEW traceloom_v_cuda_graph_replay AS SELECT g.*, e.symbol, e.label, e.task_type, e.semantic_role, e.semantic_role_reason, a.anchor_idx FROM traceloom_cuda_graph_replay g JOIN traceloom_event e ON e.event_id = g.event_id LEFT JOIN traceloom_anchor a ON a.event_id = e.event_id;
CREATE VIEW traceloom_v_cuda_graph_envelope AS SELECT ge.*, graph_anchor.anchor_idx AS graph_anchor_idx, graph.label AS graph_label, graph.stream_id AS graph_stream_id, child.label AS child_label, child.task_type AS child_task_type, child.source_table AS child_source_table, child.stream_id AS child_stream_id, child.symbol AS child_symbol, child.semantic_role AS child_semantic_role FROM traceloom_cuda_graph_envelope ge JOIN traceloom_event graph ON graph.event_id = ge.graph_event_id LEFT JOIN traceloom_anchor graph_anchor ON graph_anchor.event_id = graph.event_id JOIN traceloom_event child ON child.event_id = ge.child_event_id;
CREATE VIEW traceloom_v_node_graph_body_member AS SELECT na.node_id AS node_id, na.view_name AS view_name, na.occurrence_idx AS occurrence_idx, (SELECT COUNT(*) FROM traceloom_viz_node_anchor na2 WHERE na2.node_id = na.node_id AND na2.db_idx = na.db_idx AND na2.device_id = na.device_id AND na2.view_name = na.view_name AND na2.occurrence_idx = na.occurrence_idx AND na2.anchor_order < na.anchor_order) AS idx_in_occurrence, na.anchor_order AS anchor_order, na.coverage_kind AS coverage_kind, na.repeat_context AS repeat_context, na.compute_us AS anchor_compute_us, na.comm_us AS anchor_comm_us, na.idle_us AS anchor_idle_us, na.total_us AS anchor_total_us, na.self_us AS anchor_self_us, na.aux_events AS anchor_aux_events, na.aux_us AS anchor_aux_us, l.launch_id AS node_launch_id, l.graph_event_id AS node_event_id, l.anchor_id AS node_anchor_id, l.replay_unit_id AS node_replay_unit_id, l.graph_template_id AS node_graph_template_id, l.graph_launch_occurrence_id AS node_graph_launch_occurrence_id, l.member_order AS node_member_order, l.slot_order AS node_slot_order, l.correlation_id AS launch_correlation_id, l.match_policy AS launch_match_policy, l.association_policy AS launch_association_policy, l.start_ns AS launch_start_ns, l.end_ns AS launch_end_ns, l.dur_us AS launch_dur_us, m.member_id, m.db_idx, m.device_id, m.graph_provider, m.replay_unit_id, m.graph_template_id, m.graph_launch_occurrence_id, m.body_id, m.replay_body_template_id, m.member_order, m.slot_order, m.lane_ordinal, m.task_ordinal, m.kind, m.event_id, m.task_id, m.source_table, m.source_row_id, m.raw_task_id, m.start_ns, m.end_ns, m.dur_us, m.correlation_id, m.graph_node_id, m.original_graph_node_id, m.match_policy, m.association_policy, m.evidence_level, e.symbol AS member_symbol, e.label AS member_label, e.task_type AS member_task_type, e.semantic_role AS member_semantic_role FROM traceloom_graph_launch l JOIN traceloom_viz_node_anchor na ON na.anchor_id = l.anchor_id AND na.db_idx = l.db_idx AND na.device_id = l.device_id JOIN traceloom_graph_body_member m ON m.launch_id = l.launch_id AND m.db_idx = l.db_idx AND m.device_id = l.device_id JOIN traceloom_event e ON e.event_id = m.event_id AND e.db_idx = m.db_idx AND e.device_id = m.device_id;
CREATE VIEW traceloom_v_node_replay_cost_member AS SELECT g.node_id, g.occurrence_idx, g.view_name, g.coverage_kind, g.node_anchor_id, g.node_member_order, g.node_slot_order, c.*, g.source_table, g.source_row_id, g.graph_node_id, g.original_graph_node_id, g.evidence_level FROM traceloom_v_node_graph_body_member g JOIN traceloom_replay_cost_member c ON c.member_id = g.member_id AND c.db_idx = g.db_idx AND c.device_id = g.device_id WHERE g.coverage_kind = 'self';
CREATE VIEW traceloom_v_replay_position_realization_member AS SELECT launch.anchor_id AS position_anchor_id, member.launch_id, member.db_idx, member.device_id, member.slot_order AS position_order, member.member_id, (SELECT COUNT(*) FROM traceloom_replay_cost_member prior WHERE prior.launch_id = member.launch_id AND prior.db_idx = member.db_idx AND prior.device_id = member.device_id AND (prior.start_ns, prior.end_ns, prior.lane_ordinal, prior.task_ordinal, prior.member_id) < (member.start_ns, member.end_ns, member.lane_ordinal, member.task_ordinal, member.member_id)) AS observed_order, CASE WHEN (SELECT COUNT(*) FROM traceloom_replay_cost_member prior WHERE prior.launch_id = member.launch_id AND prior.db_idx = member.db_idx AND prior.device_id = member.device_id AND (prior.start_ns, prior.end_ns, prior.lane_ordinal, prior.task_ordinal, prior.member_id) < (member.start_ns, member.end_ns, member.lane_ordinal, member.task_ordinal, member.member_id)) = 0 THEN 'first' ELSE 'after_previous' END AS observed_relation_to_previous, member.stream_id, member.lane_ordinal, member.task_ordinal, member.identity, member.kind, role.policy_role, role.final_role, member.duration_ns, member.scheduled_work_share_ppm, member.event_id, body.source_table, body.source_row_id FROM traceloom_replay_cost_member member JOIN traceloom_graph_launch launch ON launch.launch_id = member.launch_id AND launch.db_idx = member.db_idx AND launch.device_id = member.device_id JOIN traceloom_graph_body_member body ON body.launch_id = member.launch_id AND body.member_id = member.member_id AND body.db_idx = member.db_idx AND body.device_id = member.device_id LEFT JOIN traceloom_evidence_role_decision role ON role.event_id = member.event_id AND role.db_idx = member.db_idx AND role.device_id = member.device_id;
CREATE VIEW traceloom_tree_node_anchor AS SELECT na.node_id, n.local_node_id, na.anchor_id, na.db_idx, na.device_id, na.view_name, na.occurrence_idx, na.anchor_order, na.coverage_kind, na.repeat_context, na.compute_us, na.comm_us, na.idle_us, na.total_us, na.self_us, na.aux_events, na.aux_us FROM traceloom_viz_node_anchor na JOIN traceloom_viz_node n ON n.node_id = na.node_id AND n.db_idx = na.db_idx AND n.device_id = na.device_id AND n.view_name = na.view_name;
CREATE VIEW traceloom_tree_node_occurrence AS WITH anchor_span AS (SELECT na.node_id, na.db_idx, na.device_id, na.view_name, na.occurrence_idx, MIN(a.anchor_idx) AS anchor_start_idx, MAX(a.anchor_idx) AS anchor_end_idx, COUNT(*) AS anchor_count, MIN(a.start_ns) AS start_ns, MAX(a.end_ns) AS end_ns, SUM(na.compute_us) AS compute_us, SUM(na.comm_us) AS comm_us, SUM(na.idle_us) AS idle_us, SUM(na.total_us) AS total_us, SUM(na.self_us) AS self_us, SUM(na.aux_events) AS aux_events, SUM(na.aux_us) AS aux_us, MIN(na.repeat_context) AS repeat_context FROM traceloom_viz_node_anchor na JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id AND a.db_idx = na.db_idx AND a.device_id = na.device_id GROUP BY na.node_id, na.db_idx, na.device_id, na.view_name, na.occurrence_idx) SELECT a.node_id, n.local_node_id, a.db_idx, a.device_id, a.view_name, a.occurrence_idx, a.repeat_context, a.anchor_start_idx, a.anchor_end_idx, a.anchor_count, a.start_ns, a.end_ns, ROUND(COALESCE(a.compute_us, 0.0), 3) AS compute_us, ROUND(COALESCE(a.comm_us, 0.0), 3) AS comm_us, ROUND(COALESCE(a.idle_us, 0.0), 3) AS idle_us, ROUND(COALESCE(a.total_us, 0.0), 3) AS total_us, ROUND(COALESCE(a.self_us, 0.0), 3) AS self_us, COALESCE(a.aux_events, 0) AS aux_events, ROUND(COALESCE(a.aux_us, 0.0), 3) AS aux_us FROM anchor_span a JOIN traceloom_viz_node n ON n.node_id = a.node_id AND n.db_idx = a.db_idx AND n.device_id = a.device_id AND n.view_name = a.view_name;
CREATE VIEW traceloom_v_node_anchor_cost AS SELECT na.node_id, na.anchor_id, na.occurrence_idx, na.anchor_order, e.dur_us AS anchor_dur_us, e.role AS anchor_role, e.symbol AS anchor_symbol, e.label AS anchor_label FROM traceloom_viz_node_anchor na JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id JOIN traceloom_event e ON e.event_id = a.event_id;
CREATE VIEW traceloom_v_node_aux_cost AS SELECT na.node_id, al.anchor_id, al.aux_event_id, al.aux_order, e.dur_us AS aux_dur_us, e.role AS aux_role, e.symbol AS aux_symbol, e.label AS aux_label FROM traceloom_viz_node_anchor na JOIN traceloom_aux_link al ON al.anchor_id = na.anchor_id JOIN traceloom_event e ON e.event_id = al.aux_event_id;
CREATE VIEW traceloom_v_node_cost AS SELECT n.*, COALESCE(anchor_cost.anchor_dur_us, 0.0) AS sql_anchor_us, COALESCE(aux_cost.aux_dur_us, 0.0) AS sql_aux_us FROM traceloom_viz_node n LEFT JOIN (SELECT node_id, SUM(anchor_dur_us) AS anchor_dur_us FROM traceloom_v_node_anchor_cost GROUP BY node_id) anchor_cost ON anchor_cost.node_id = n.node_id LEFT JOIN (SELECT node_id, SUM(aux_dur_us) AS aux_dur_us FROM traceloom_v_node_aux_cost GROUP BY node_id) aux_cost ON aux_cost.node_id = n.node_id;
CREATE VIEW traceloom_v_node_children AS SELECT e.parent_node_id, e.child_node_id, e.edge_order, child.* FROM traceloom_viz_edge e JOIN traceloom_viz_node child ON child.node_id = e.child_node_id;
CREATE VIEW traceloom_v_tree_node AS WITH RECURSIVE tree AS (SELECT n.node_id, CAST(NULL AS TEXT) AS parent_node_id, n.db_idx, n.device_id, n.view_name, n.local_node_id, CAST(SUBSTR(n.local_node_id, 2) AS INTEGER) AS display_order, n.path, n.depth AS tree_depth, n.level AS depth, CASE WHEN n.kind = 'repeat' THEN 1 ELSE 0 END AS loop_depth, n.node_type, n.kind, n.symbol, n.label, n.category, n.repeat_label, n.repeat_count, n.occurrence_count, n.anchor_count, n.anchors_per_occurrence, n.anchors_per_occurrence AS avg_anchor, n.first_anchor_idx, n.last_anchor_idx, n.compute_us, n.comm_us, n.idle_us, n.total_us, n.avg_compute_us, n.avg_comm_us, n.avg_idle_us, n.avg_total_us, COALESCE(n.compute_us, 0.0) + COALESCE(n.comm_us, 0.0) AS active_us, ROUND((COALESCE(n.compute_us, 0.0) + COALESCE(n.comm_us, 0.0)) / CASE WHEN COALESCE(n.occurrence_count, 0) = 0 THEN 1 ELSE n.occurrence_count * CASE WHEN n.kind = 'repeat' AND COALESCE(n.repeat_count, 0) > 0 THEN n.repeat_count ELSE 1 END END, 3) AS avg_active_us, n.self_us, n.aux_events, n.aux_us, ROUND(COALESCE(n.aux_us, 0.0) / CASE WHEN COALESCE(n.occurrence_count, 0) = 0 THEN 1 ELSE n.occurrence_count * CASE WHEN n.kind = 'repeat' AND COALESCE(n.repeat_count, 0) > 0 THEN n.repeat_count ELSE 1 END END, 3) AS avg_aux_us, ROUND(CASE WHEN COALESCE(n.total_us, 0.0) = 0.0 THEN 0.0 ELSE COALESCE(n.comm_us, 0.0) / n.total_us END, 6) AS comm_pct, ROUND(CASE WHEN COALESCE(n.total_us, 0.0) = 0.0 THEN 0.0 ELSE COALESCE(n.idle_us, 0.0) / n.total_us END, 6) AS idle_pct FROM traceloom_viz_node n WHERE NOT EXISTS (SELECT 1 FROM traceloom_viz_edge e WHERE e.child_node_id = n.node_id) UNION ALL SELECT child.node_id, e.parent_node_id, child.db_idx, child.device_id, child.view_name, child.local_node_id, CAST(SUBSTR(child.local_node_id, 2) AS INTEGER) AS display_order, child.path, child.depth AS tree_depth, child.level AS depth, tree.loop_depth + CASE WHEN child.kind = 'repeat' THEN 1 ELSE 0 END AS loop_depth, child.node_type, child.kind, child.symbol, child.label, child.category, child.repeat_label, child.repeat_count, child.occurrence_count, child.anchor_count, child.anchors_per_occurrence, child.anchors_per_occurrence AS avg_anchor, child.first_anchor_idx, child.last_anchor_idx, child.compute_us, child.comm_us, child.idle_us, child.total_us, child.avg_compute_us, child.avg_comm_us, child.avg_idle_us, child.avg_total_us, COALESCE(child.compute_us, 0.0) + COALESCE(child.comm_us, 0.0) AS active_us, ROUND((COALESCE(child.compute_us, 0.0) + COALESCE(child.comm_us, 0.0)) / CASE WHEN COALESCE(child.occurrence_count, 0) = 0 THEN 1 ELSE child.occurrence_count * CASE WHEN child.kind = 'repeat' AND COALESCE(child.repeat_count, 0) > 0 THEN child.repeat_count ELSE 1 END END, 3) AS avg_active_us, child.self_us, child.aux_events, child.aux_us, ROUND(COALESCE(child.aux_us, 0.0) / CASE WHEN COALESCE(child.occurrence_count, 0) = 0 THEN 1 ELSE child.occurrence_count * CASE WHEN child.kind = 'repeat' AND COALESCE(child.repeat_count, 0) > 0 THEN child.repeat_count ELSE 1 END END, 3) AS avg_aux_us, ROUND(CASE WHEN COALESCE(child.total_us, 0.0) = 0.0 THEN 0.0 ELSE COALESCE(child.comm_us, 0.0) / child.total_us END, 6) AS comm_pct, ROUND(CASE WHEN COALESCE(child.total_us, 0.0) = 0.0 THEN 0.0 ELSE COALESCE(child.idle_us, 0.0) / child.total_us END, 6) AS idle_pct FROM tree JOIN traceloom_viz_edge e ON e.parent_node_id = tree.node_id JOIN traceloom_viz_node child ON child.node_id = e.child_node_id) SELECT * FROM tree;
CREATE VIEW traceloom_v_anchor_symbol_lineage AS SELECT d.*, r.precedence, r.provider_scope, r.source_domain, r.match_mode, r.match_expression, r.required_fields, r.description AS rule_description, a.role AS anchor_role, a.start_ns, a.end_ns, a.dur_us FROM traceloom_anchor_symbol_normalization d JOIN traceloom_symbol_normalization_rule r ON r.policy_id = d.policy_id AND r.policy_version = d.policy_version AND r.rule_id = d.rule_id JOIN traceloom_anchor a ON a.anchor_id = d.anchor_id AND a.db_idx = d.db_idx AND a.device_id = d.device_id;
CREATE VIEW traceloom_v_symbol_normalization_placement AS SELECT d.anchor_id, d.db_idx, d.device_id, d.anchor_idx, d.event_id, d.observed_symbol, d.observed_symbol_source, d.structural_symbol, d.policy_id, d.policy_version, d.rule_id, d.outcome, d.reason_code, d.candidate_rule_ids, d.source_path, d.source_table, d.source_key, na.node_id, n.local_node_id, na.view_name, na.occurrence_idx, na.anchor_order, na.coverage_kind, na.repeat_context, a.dur_us AS anchor_dur_us FROM traceloom_anchor_symbol_normalization d JOIN traceloom_anchor a ON a.anchor_id = d.anchor_id AND a.db_idx = d.db_idx AND a.device_id = d.device_id JOIN traceloom_viz_node_anchor na ON na.anchor_id = d.anchor_id AND na.db_idx = d.db_idx AND na.device_id = d.device_id JOIN traceloom_viz_node n ON n.node_id = na.node_id AND n.db_idx = na.db_idx AND n.device_id = na.device_id AND n.view_name = na.view_name;
CREATE VIEW traceloom_v_symbol_variant_cost AS SELECT db_idx, device_id, view_name, node_id, local_node_id, anchor_order, structural_symbol, observed_symbol, observed_symbol_source, rule_id, outcome, COUNT(*) AS occurrence_count, ROUND(SUM(anchor_dur_us), 3) AS total_us, ROUND(AVG(anchor_dur_us), 3) AS avg_us, ROUND(MIN(anchor_dur_us), 3) AS min_us, ROUND(MAX(anchor_dur_us), 3) AS max_us FROM traceloom_v_symbol_normalization_placement WHERE coverage_kind = 'self' GROUP BY db_idx, device_id, view_name, node_id, local_node_id, anchor_order, structural_symbol, observed_symbol, observed_symbol_source, rule_id, outcome;
CREATE VIEW traceloom_v_semantic_tree_node AS SELECT n.*, parent.local_node_id AS parent_local_id, parent.label AS parent_label, CASE WHEN COALESCE(n.total_us, 0.0) = 0.0 THEN 0.0 ELSE ROUND(COALESCE(n.comm_us, 0.0) / n.total_us, 6) END AS comm_pct, CASE WHEN COALESCE(n.total_us, 0.0) = 0.0 THEN 0.0 ELSE ROUND(COALESCE(n.idle_us, 0.0) / n.total_us, 6) END AS idle_pct FROM traceloom_semantic_node n LEFT JOIN traceloom_semantic_node parent ON parent.node_id = n.parent_node_id;
CREATE VIEW traceloom_v_semantic_tree_readable AS SELECT n.tree_id, n.db_idx, n.device_id, n.view_name, n.tree_kind, n.preorder_idx, n.local_node_id, n.parent_local_node_id, n.path, n.display_depth, printf('%*s', COALESCE(n.display_depth, 0) * 2, '') || '- [' || COALESCE(NULLIF(n.path, ''), 'root') || '] ' || n.local_node_id || ' ' || CASE WHEN n.node_type = 'Repeat' THEN 'Repeat x' || COALESCE(n.repeat_count, 1) WHEN n.node_type = 'Seq' THEN 'Seq' ELSE COALESCE(NULLIF(n.node_type, ''), 'Node') END || ' | ' || COALESCE(NULLIF(n.label, ''), NULLIF(n.symbol, ''), n.semantic_kind, '') || ' | anchors=' || COALESCE(n.anchor_count, 0) || ' total_us=' || printf('%.3f', COALESCE(n.total_us, 0.0)) || CASE WHEN COALESCE(n.hidden_aux_event_count, 0.0) > 0.0 THEN ' hidden_aux=' || printf('%.0f', n.hidden_aux_event_count) || ' hidden_aux_us=' || printf('%.3f', COALESCE(n.hidden_aux_us, 0.0)) ELSE '' END AS line FROM traceloom_semantic_node n;
CREATE UNIQUE INDEX idx_traceloom_evidence_role_decision_event
  ON traceloom_evidence_role_decision(db_idx, event_id);
CREATE INDEX idx_traceloom_evidence_role_decision_filter
  ON traceloom_evidence_role_decision(
    db_idx, input_provider_scope, final_role, support_state);
CREATE INDEX idx_traceloom_evidence_role_decision_rule
  ON traceloom_evidence_role_decision(policy_id, rule_id, support_state);
CREATE INDEX idx_traceloom_evidence_role_decision_source
  ON traceloom_evidence_role_decision(source_table, source_key, db_idx);
CREATE INDEX idx_traceloom_evidence_role_placement_member
  ON traceloom_evidence_role_placement(
    placement_kind, placement_id, decision_id);
CREATE INDEX idx_traceloom_evidence_role_placement_owner
  ON traceloom_evidence_role_placement(
    owner_id, placement_kind, decision_id);
CREATE INDEX idx_traceloom_evidence_role_issue_code
  ON traceloom_evidence_role_issue(code, support_state, decision_id);
CREATE INDEX idx_traceloom_protected_interval_range
  ON traceloom_protected_interval(
    db_idx, device_id, start_ns, end_ns, protected_interval_id);
CREATE VIEW traceloom_v_evidence_role_decision AS
SELECT d.*, e.dur_us, e.symbol, e.label, e.raw_label, e.op_type,
       e.compute_task_type, e.task_type,
       s.source_ordinal, s.source_role,
       json_extract(s.raw_json, '$.source_path') AS source_path,
       s.raw_json AS source_raw_json
FROM traceloom_evidence_role_decision d
JOIN traceloom_event e
  ON e.event_id = d.event_id AND e.db_idx = d.db_idx
 AND e.device_id = d.device_id
LEFT JOIN traceloom_event_source s
  ON s.event_id = d.event_id AND s.db_idx = d.db_idx
 AND s.device_id = d.device_id
 AND s.source_ordinal = 0;
CREATE VIEW traceloom_v_evidence_role_placement AS
SELECT d.db_idx, d.device_id, d.decision_id, d.event_id,
       d.input_provider_scope, d.policy_id, d.rule_id, d.rule_class,
       d.policy_role, d.final_role, d.support_state AS decision_support_state,
       d.reason_code AS decision_reason_code, d.duration_ns,
       p.placement_order, p.placement_kind, p.placement_id, p.owner_id,
       p.relation_name, p.support_state AS placement_support_state,
       p.reason_code AS placement_reason_code,
       d.source_table, d.source_key
FROM traceloom_evidence_role_decision d
JOIN traceloom_evidence_role_placement p
  ON p.decision_id = d.decision_id;
CREATE VIEW traceloom_v_evidence_role_cost_coverage AS
SELECT db_idx, input_provider_scope, policy_id, policy_version,
       final_role, support_state, COUNT(*) AS event_count,
       SUM(duration_ns) AS retained_duration_ns,
       SUM(CASE WHEN effective_structural_participation = 'identity'
                THEN duration_ns ELSE 0 END) AS identity_duration_ns,
       SUM(CASE WHEN effective_structural_participation <> 'identity'
                THEN duration_ns ELSE 0 END) AS non_identity_duration_ns
FROM traceloom_evidence_role_decision
GROUP BY db_idx, input_provider_scope, policy_id, policy_version,
         final_role, support_state;
CREATE VIEW traceloom_v_evidence_role_structure AS
      SELECT d.decision_id, d.event_id, d.final_role, p.placement_kind,
       p.placement_id, n.node_id, n.occurrence_idx, n.view_name,
       n.coverage_kind
FROM traceloom_evidence_role_decision d
JOIN traceloom_evidence_role_placement p
  ON p.decision_id = d.decision_id AND p.placement_kind = 'anchor'
JOIN traceloom_viz_node_anchor n
  ON n.anchor_id = p.placement_id
 AND n.db_idx = d.db_idx AND n.device_id = d.device_id
UNION ALL
SELECT d.decision_id, d.event_id, d.final_role, p.placement_kind,
       p.placement_id, n.node_id, n.occurrence_idx, n.view_name,
       n.coverage_kind
FROM traceloom_evidence_role_decision d
JOIN traceloom_evidence_role_placement p
  ON p.decision_id = d.decision_id
 AND p.placement_kind = 'auxiliary_link'
JOIN traceloom_viz_node_anchor n
  ON n.anchor_id = p.owner_id
 AND n.db_idx = d.db_idx AND n.device_id = d.device_id
UNION ALL
SELECT d.decision_id, d.event_id, d.final_role, p.placement_kind,
       p.placement_id, n.node_id, n.occurrence_idx, n.view_name,
       n.coverage_kind
FROM traceloom_evidence_role_decision d
JOIN traceloom_evidence_role_placement p
  ON p.decision_id = d.decision_id
 AND p.placement_kind = 'graph_body_member'
JOIN traceloom_v_node_graph_body_member n
  ON n.member_id = p.placement_id
 AND n.db_idx = d.db_idx AND n.device_id = d.device_id
UNION ALL
SELECT d.decision_id, d.event_id, d.final_role, p.placement_kind,
       p.placement_id, n.node_id, n.occurrence_idx, n.view_name,
       n.coverage_kind
FROM traceloom_evidence_role_decision d
JOIN traceloom_evidence_role_placement p
  ON p.decision_id = d.decision_id
 AND p.placement_kind = 'replay_unit'
JOIN traceloom_v_node_graph_body_member n
  ON n.replay_unit_id = CAST(substr(p.placement_id, 13) AS INTEGER)
 AND n.db_idx = d.db_idx AND n.device_id = d.device_id
UNION ALL
SELECT d.decision_id, d.event_id, d.final_role, p.placement_kind,
       p.placement_id, n.node_id, n.occurrence_idx, n.view_name,
       n.coverage_kind
FROM traceloom_evidence_role_decision d
JOIN traceloom_evidence_role_placement p
  ON p.decision_id = d.decision_id
 AND p.placement_kind = 'protected_interval'
JOIN traceloom_protected_interval i
  ON i.protected_interval_id = p.placement_id
 AND i.db_idx = d.db_idx AND i.device_id = d.device_id
JOIN traceloom_v_node_graph_body_member n
  ON n.replay_unit_id = CAST(substr(i.replay_unit_id, 13) AS INTEGER)
 AND n.db_idx = d.db_idx AND n.device_id = d.device_id;
COMMIT;
