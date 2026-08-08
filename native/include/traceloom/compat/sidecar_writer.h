#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/compat/anchor_cost_breakdown_rows.h"
#include "traceloom/compat/schema.h"

namespace traceloom::compat {

struct MetadataSqlRow {
  std::string key;
  std::string value;
};

struct EventSqlRow {
  std::string event_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::uint32_t step_idx = 0;
  std::string source_table;
  std::string source_key;
  std::int64_t stream_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  double dur_us = 0.0;
  std::string category;
  std::string role;
  std::string semantic_role;
  std::string semantic_role_reason;
  std::string symbol;
  std::string label;
  std::string raw_label;
  std::string op_type;
  std::string compute_task_type;
  std::string family;
  std::string task_type;
  std::string raw_json;
};

struct EventSourceSqlRow {
  std::string event_id;
  std::uint32_t source_ordinal = 0;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string source_table;
  std::string source_key;
  std::string source_role;
  std::string raw_json;
};

struct EventSqlRows {
  std::vector<EventSqlRow> events;
  std::vector<EventSourceSqlRow> event_sources;
};

struct AnchorSqlRow {
  std::string anchor_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::uint32_t anchor_idx = 0;
  std::string event_id;
  std::uint32_t step_idx = 0;
  std::string symbol;
  std::string role;
  std::string label;
  std::string family;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  double dur_us = 0.0;
};

struct AuxLinkSqlRow {
  std::string anchor_id;
  std::string aux_event_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::uint32_t aux_order = 0;
  std::uint32_t aux_step_idx = 0;
  std::string link_type;
  std::string reason;
  std::string aux_kind;
  double aux_dur_us = 0.0;
  std::string raw_json;
};

struct AnchorAuxSlotSqlRow {
  std::string anchor_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::uint32_t anchor_idx = 0;
  std::uint32_t anchor_step_idx = 0;
  std::uint32_t aux_start_step_idx = 0;
  std::uint32_t aux_end_step_idx = 0;
  std::uint32_t aux_event_count = 0;
  double aux_dur_us = 0.0;
  std::string raw_json;
};

struct AnchorAuxSqlRows {
  std::vector<EventSqlRow> events;
  std::vector<EventSourceSqlRow> event_sources;
  std::vector<AnchorSqlRow> anchors;
  std::vector<AnchorAuxSlotSqlRow> aux_slots;
  std::vector<AuxLinkSqlRow> aux_links;
};

struct AuxAttributionSqlRows {
  std::vector<AnchorAuxSlotSqlRow> aux_slots;
  std::vector<AuxLinkSqlRow> aux_links;
};

struct VizNodeSqlRow {
  std::string node_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string view_name = "default";
  std::string local_node_id;
  std::string path;
  std::string node_type;
  std::string kind;
  std::string symbol;
  std::string label;
  std::string category;
  std::uint32_t depth = 0;
  std::uint32_t level = 0;
  std::string repeat_label;
  // Zero materializes as SQL NULL for non-repeat nodes.
  std::uint32_t repeat_count = 0;
  std::uint32_t occurrence_count = 1;
  std::uint32_t anchor_count = 0;
  double anchors_per_occurrence = 0.0;
  std::uint32_t first_anchor_idx = 0;
  std::uint32_t last_anchor_idx = 0;
  double compute_us = 0.0;
  double comm_us = 0.0;
  double idle_us = 0.0;
  double total_us = 0.0;
  double avg_compute_us = 0.0;
  double avg_comm_us = 0.0;
  double avg_idle_us = 0.0;
  double avg_total_us = 0.0;
  double self_us = 0.0;
  double aux_events = 0.0;
  double aux_us = 0.0;
  std::string raw_json;
};

struct VizNodeAnchorSqlRow {
  std::string node_id;
  std::string anchor_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string view_name = "default";
  std::uint32_t occurrence_idx = 0;
  std::uint32_t anchor_order = 0;
  std::string coverage_kind = "self";
  std::string repeat_context;
  // Additive cost packet owned by this anchor in this node occurrence.
  // Aux values are evidence overlays and are not part of total_us.
  double compute_us = 0.0;
  double comm_us = 0.0;
  double idle_us = 0.0;
  double total_us = 0.0;
  double self_us = 0.0;
  double aux_events = 0.0;
  double aux_us = 0.0;
};

struct VizEdgeSqlRow {
  std::string parent_node_id;
  std::string child_node_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string view_name = "default";
  std::uint32_t edge_order = 0;
  std::string edge_kind;
  std::string raw_json;
};

struct AnchorPrimaryNodeSqlRow {
  std::string anchor_id;
  std::string node_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string view_name = "default";
  std::string reason = "smallest_covering_node";
};

struct LoopNodeSqlRow {
  std::string node_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string view_name = "default";
  std::uint32_t loop_rank = 0;
  std::string repeat_label;
  std::uint32_t repeat_count = 0;
  std::uint32_t occurrence_count = 0;
  std::uint32_t anchor_count = 0;
  double total_us = 0.0;
  double avg_total_us = 0.0;
  double compute_us = 0.0;
  double comm_us = 0.0;
  double idle_us = 0.0;
  double loop_total_pct = 0.0;
  std::string raw_json;
};

struct NodeCoverageSqlRows {
  std::vector<VizNodeSqlRow> nodes;
  std::vector<VizEdgeSqlRow> edges;
  std::vector<VizNodeAnchorSqlRow> node_anchors;
  std::vector<AnchorPrimaryNodeSqlRow> anchor_primary_nodes;
  std::vector<LoopNodeSqlRow> loop_nodes;
};

struct LoopTreeSqlRows {
  std::vector<VizNodeSqlRow> nodes;
  std::vector<VizEdgeSqlRow> edges;
  std::vector<LoopNodeSqlRow> loop_nodes;
};

struct NodeAnchorCoverageSqlRows {
  std::vector<VizNodeAnchorSqlRow> node_anchors;
  std::vector<AnchorPrimaryNodeSqlRow> anchor_primary_nodes;
};

struct GraphReplaySqlRow {
  std::string graph_event_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string graph_provider = "cuda";
  std::string graph_kind = "cuda_graph_replay";
  std::uint32_t graph_event_idx = 0;
  std::string event_id;
  std::uint32_t step_idx = 0;
  std::int64_t stream_id = 0;
  std::string correlation_id;
  std::string graph_id;
  std::string graph_exec_id;
  std::string context_id;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  double dur_us = 0.0;
  std::uint32_t enclosed_event_count = 0;
  double enclosed_event_us = 0.0;
  std::uint32_t enclosed_kernel_count = 0;
  double enclosed_kernel_us = 0.0;
  std::string raw_json;
};

struct GraphEnvelopeSqlRow {
  std::string envelope_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string graph_provider = "cuda";
  std::string graph_kind = "cuda_graph_replay";
  std::uint32_t envelope_idx = 0;
  std::string graph_event_id;
  std::string child_event_id;
  std::uint32_t graph_step_idx = 0;
  std::uint32_t child_step_idx = 0;
  std::string relation;
  std::string stream_relation;
  std::string graph_id;
  std::string graph_exec_id;
  std::string graph_correlation_id;
  std::int64_t graph_start_ns = 0;
  std::int64_t graph_end_ns = 0;
  std::int64_t child_start_ns = 0;
  std::int64_t child_end_ns = 0;
  double start_offset_us = 0.0;
  double end_offset_us = 0.0;
  double child_dur_us = 0.0;
  std::string raw_json;
};

struct GraphReconstructionRegionSqlRow {
  std::string region_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string graph_provider = "aclgraph";
  std::string candidate_id;
  std::uint32_t region_order = 0;
  std::string status;
  std::string boundary_policy;
  std::string order_policy;
  std::string identity_policy;
  std::string shape_policy;
  std::uint32_t first_launch_occurrence_id = 0;
  std::uint32_t last_launch_occurrence_id = 0;
  std::uint32_t observed_launch_count = 0;
  std::uint32_t expected_launch_count = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  double dur_us = 0.0;
  std::string raw_json;
};

struct GraphReplaySqlRows {
  std::vector<EventSqlRow> events;
  std::vector<EventSourceSqlRow> event_sources;
  std::vector<AnchorSqlRow> anchors;
  std::vector<GraphReplaySqlRow> graph_replays;
  std::vector<GraphEnvelopeSqlRow> graph_envelopes;
  std::vector<GraphReconstructionRegionSqlRow> reconstruction_regions;
};

struct GraphReplayEvidenceSqlRows {
  std::vector<GraphReplaySqlRow> graph_replays;
  std::vector<GraphEnvelopeSqlRow> graph_envelopes;
  std::vector<GraphReconstructionRegionSqlRow> reconstruction_regions;
};

struct CollectiveGlobalLinkSqlRow {
  std::string candidate_collective_key;
  std::string db_name;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string member_id;
  std::string pair_id;
  std::string local_node_id;
  std::uint32_t occurrence_idx = 0;
  std::uint32_t idx_in_occurrence = 0;
  std::string op_type;
  std::string anchor_id;
  std::string event_id;
  std::string source_table;
  std::string source_key;
  std::string connection_id;
  std::string op_id;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  double dur_us = 0.0;
  std::string validation_status;
  double confidence = 0.0;
};

struct GlobalCollectiveSummarySqlRow {
  std::string candidate_collective_key;
  std::string pair_id;
  std::uint32_t occurrence_idx = 0;
  std::string op_type;
  std::uint32_t idx_in_occurrence = 0;
  std::uint32_t member_count = 0;
  std::uint32_t expected_world_size = 0;
  double start_skew_us = 0.0;
  double duration_skew_us = 0.0;
  std::string connection_ids;
  std::string op_ids;
  std::string members;
  std::string missing_members;
  std::string validation_status;
  double confidence = 0.0;
};

struct GlobalCollectiveMemberSqlRow {
  std::string candidate_collective_key;
  std::string db_name;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string member_id;
  std::string pair_id;
  std::string local_node_id;
  std::uint32_t occurrence_idx = 0;
  std::uint32_t idx_in_occurrence = 0;
  std::string op_type;
  std::string anchor_id;
  std::string event_id;
  std::string source_table;
  std::string source_key;
  std::string connection_id;
  std::string op_id;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  double dur_us = 0.0;
  std::string validation_status;
  double confidence = 0.0;
};

struct GlobalCollectiveSqlRows {
  std::vector<GlobalCollectiveSummarySqlRow> summaries;
  std::vector<GlobalCollectiveMemberSqlRow> members;
};

struct SemanticTreeHeaderSqlRow {
  std::string tree_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string view_name = "anchor_tree";
  std::string tree_kind = "semantic";
  std::string stem;
  std::string root_node_id;
  std::string schema_version;
  std::string semantic_projection;
  std::string macro_discovery;
  std::string readable_macro_mode;
  std::string auxiliary_attribution;
  std::string raw_json;
};

struct SemanticNodeSqlRow {
  std::string node_id;
  std::string tree_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string view_name = "anchor_tree";
  std::string tree_kind = "semantic";
  std::string local_node_id;
  std::string parent_node_id;
  std::string parent_local_node_id;
  std::uint32_t preorder_idx = 0;
  std::uint32_t sibling_order = 0;
  std::string path;
  std::uint32_t depth = 0;
  std::uint32_t display_depth = 0;
  std::uint32_t loop_depth = 0;
  std::string node_type;
  std::string semantic_kind;
  std::string symbol;
  std::string label;
  std::string category;
  // Zero materializes as SQL NULL for non-repeat nodes.
  std::uint32_t repeat_count = 0;
  std::uint32_t occurrence_count = 0;
  std::uint32_t anchor_count = 0;
  std::uint32_t first_anchor_idx = 0;
  std::uint32_t last_anchor_idx = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  double compute_us = 0.0;
  double comm_us = 0.0;
  double idle_us = 0.0;
  double total_us = 0.0;
  double avg_compute_us = 0.0;
  double avg_comm_us = 0.0;
  double avg_idle_us = 0.0;
  double avg_total_us = 0.0;
  double self_us = 0.0;
  double aux_event_count = 0.0;
  double aux_us = 0.0;
  double hidden_aux_event_count = 0.0;
  double hidden_aux_us = 0.0;
  std::string raw_json;
};

struct SemanticEdgeSqlRow {
  std::string parent_node_id;
  std::string child_node_id;
  std::string tree_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string view_name = "anchor_tree";
  std::string tree_kind = "semantic";
  std::uint32_t edge_order = 0;
  std::string edge_kind = "child";
  std::string raw_json;
};

struct SemanticTreeSqlRows {
  std::vector<SemanticTreeHeaderSqlRow> trees;
  std::vector<SemanticNodeSqlRow> nodes;
  std::vector<SemanticEdgeSqlRow> edges;
};

struct SemanticGraphSqlRows {
  std::vector<SemanticNodeSqlRow> nodes;
  std::vector<SemanticEdgeSqlRow> edges;
};

struct RunMetadataSqlRow {
  std::string run_id;
  std::string analysis_status;
  bool has_span = false;
  std::int64_t span_start_ns = 0;
  std::int64_t span_end_ns = 0;
  std::string contract_version;
  std::string semantic_rules_version;
  std::string semantic_rules_sha256;
  std::string attribution_rule_version;
  std::string host_api_rules_version;
  std::string host_api_rules_sha256;
  std::string collection_status;
  std::uint32_t db_idx = 0;
  std::string source_kind;
  std::string source_path;
  std::string metadata_json;
};

struct DeviceIntervalSqlRow {
  std::string interval_id;
  std::string run_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::uint32_t interval_order = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::uint64_t duration_ns = 0;
  double duration_us = 0.0;
  std::string interval_kind;
  std::uint64_t source_count = 0;
  std::string clock_domain;
  std::string contract_version;
  std::string semantic_rules_version;
  std::string attribution_rule_version;
};

struct StreamStateSqlRow {
  std::string state_id;
  std::string run_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::uint64_t stream_id = 0;
  std::uint32_t state_order = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::uint64_t duration_ns = 0;
  double duration_us = 0.0;
  std::string state;
  std::uint64_t source_count = 0;
  std::string stream_universe_kind;
  std::uint64_t stream_universe_size = 0;
  std::uint64_t observed_stream_count = 0;
  bool observed_universe_scan_complete = false;
  std::string collection_status;
  std::string clock_domain;
  std::string contract_version;
  std::string semantic_rules_version;
  std::string attribution_rule_version;
};

struct ClockMarkerSqlRow {
  std::string clock_marker_id;
  std::string run_id;
  std::string clock_model_id;
  std::uint32_t db_idx = 0;
  std::string marker_id;
  std::int64_t host_before_ns = 0;
  std::int64_t host_after_ns = 0;
  std::int64_t host_midpoint_ns = 0;
  bool has_profiler_host_interval = false;
  std::int64_t profiler_host_start_ns = 0;
  std::int64_t profiler_host_end_ns = 0;
  std::int64_t profiler_host_midpoint_ns = 0;
  std::int64_t device_timestamp_ns = 0;
  std::uint64_t host_pid = 0;
  std::uint64_t host_tid = 0;
  std::uint32_t device_id = 0;
  bool has_stream_id = false;
  std::uint64_t stream_id = 0;
  bool has_connection_id = false;
  std::int64_t connection_id = -1;
  std::string call_site;
  std::int64_t return_status = 0;
  std::string marker_state;
  std::string resolution_method;
  bool has_resolution_residual = false;
  double resolution_residual_ns = 0.0;
  std::string source_kind;
  std::string source_table;
  std::string source_key;
  std::string contract_version;
  bool has_record_host_bracket = false;
  std::int64_t record_after_ns = 0;
  std::int64_t record_midpoint_ns = 0;
};

struct ClockModelSqlRow {
  std::string clock_model_id;
  std::string run_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string source_clock_domain;
  std::string intermediate_clock_domain;
  std::string target_clock_domain;
  std::string mapping_kind;
  std::string scale;
  std::string offset_ns;
  std::string reference_host_ns;
  std::string reference_device_ns;
  double drift_ppm = 0.0;
  bool has_profiler_host_mapping = false;
  std::string marker_to_device_scale;
  std::string marker_to_device_offset_ns;
  std::string reference_marker_host_ns;
  std::string marker_reference_device_ns;
  double marker_to_device_drift_ppm = 0.0;
  std::string profiler_to_marker_scale;
  std::string profiler_to_marker_offset_ns;
  std::string reference_profiler_host_ns;
  std::string profiler_reference_marker_ns;
  double profiler_to_marker_drift_ppm = 0.0;
  std::string fit_method;
  std::string fit_method_version;
  std::uint64_t fit_random_seed = 0;
  std::uint64_t input_marker_count = 0;
  std::uint64_t inlier_marker_count = 0;
  std::uint64_t rejected_marker_count = 0;
  std::uint64_t fit_marker_count = 0;
  std::uint64_t validation_marker_count = 0;
  double absolute_residual_p50_ns = 0.0;
  double absolute_residual_p95_ns = 0.0;
  double absolute_residual_max_ns = 0.0;
  double bracket_uncertainty_p95_ns = 0.0;
  double host_clock_absolute_residual_p50_ns = 0.0;
  double host_clock_absolute_residual_p95_ns = 0.0;
  double host_clock_absolute_residual_max_ns = 0.0;
  double host_clock_uncertainty_p95_ns = 0.0;
  double composed_absolute_residual_p50_ns = 0.0;
  double composed_absolute_residual_p95_ns = 0.0;
  double composed_absolute_residual_max_ns = 0.0;
  std::uint64_t direct_overlap_marker_count = 0;
  std::uint64_t ordinal_affine_fallback_marker_count = 0;
  std::uint64_t epsilon_ns = 0;
  std::string alignment_status;
  std::string reason;
  std::string profiler_caller_observation_kind;
  std::string marker_device_observation_kind;
  std::string intercept_ns;
  double profiler_to_caller_bracket_uncertainty_p95_ns = 0.0;
};

struct HostApiEventSqlRow {
  std::string api_event_id;
  std::string run_id;
  std::uint32_t db_idx = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::uint64_t duration_ns = 0;
  double duration_us = 0.0;
  std::uint64_t global_tid = 0;
  std::int64_t connection_id = -1;
  std::string api_type;
  std::string api_name;
  std::string api_family;
  bool has_device_id = false;
  std::uint32_t device_id = 0;
  std::string source_kind;
  std::string source_table;
  std::string source_key;
  std::string clock_domain = "profiler_host";
  std::string contract_version;
  std::string host_api_rules_version;
};

struct TaskApiLinkSqlRow {
  std::string task_api_link_id;
  std::string run_id;
  std::string api_event_id;
  std::string trace_event_id;
  std::uint32_t db_idx = 0;
  bool has_device_id = false;
  std::uint32_t device_id = 0;
  bool has_stream_id = false;
  std::uint64_t stream_id = 0;
  std::int64_t connection_id = -1;
  std::string link_status;
  std::string api_name;
  std::string task_type;
};

struct IdleCandidateSqlRow {
  std::string candidate_id;
  std::string run_id;
  std::string gap_interval_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::uint32_t candidate_order = 0;
  std::string candidate_category;
  std::string candidate_level;
  std::string candidate_relation;
  std::string candidate_status;
  std::string reason;
  std::string alignment_status;
  std::uint64_t source_count = 0;
  std::string contract_version;
  std::string attribution_rule_version;
};

struct IdleExplanationSqlRow {
  std::string idle_explanation_id;
  std::string run_id;
  std::string gap_interval_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::uint32_t explanation_order = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::uint64_t duration_ns = 0;
  double duration_us = 0.0;
  std::string category;
  std::string evidence_level;
  std::string evidence_relation;
  std::string alignment_status;
  std::string collection_status;
  std::string reason;
  std::uint64_t source_count = 0;
  std::string clock_domain;
  std::string contract_version;
  std::string semantic_rules_version;
  std::string attribution_rule_version;
};

struct EvidenceLinkSqlRow {
  std::string owner_kind;
  std::string owner_id;
  std::uint32_t evidence_ordinal = 0;
  std::string source_kind;
  std::string source_table;
  std::string source_key;
  std::string relation;
  std::string evidence_level;
  bool has_overlap = false;
  std::int64_t overlap_start_ns = 0;
  std::int64_t overlap_end_ns = 0;
  bool has_stream_id = false;
  std::uint64_t stream_id = 0;
  std::string state;
  std::string trace_event_id;
  std::string matched_rule_id;
};

struct AnchorIdleExplanationSqlRow {
  std::string anchor_id;
  std::string run_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::uint32_t anchor_idx = 0;
  std::string category;
  std::string evidence_level;
  std::uint64_t slice_count = 0;
  std::uint64_t duration_ns = 0;
  double duration_us = 0.0;
};

struct NodeIdleExplanationSqlRow {
  std::string node_id;
  std::string run_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string view_name;
  std::string category;
  std::string evidence_level;
  std::uint64_t slice_count = 0;
  std::uint64_t duration_ns = 0;
  double duration_us = 0.0;
};

struct IdleEvidenceSqlRows {
  std::vector<RunMetadataSqlRow> run_metadata;
  std::vector<DeviceIntervalSqlRow> device_intervals;
  std::vector<StreamStateSqlRow> stream_states;
  std::vector<ClockMarkerSqlRow> clock_markers;
  std::vector<ClockModelSqlRow> clock_models;
  std::vector<HostApiEventSqlRow> host_api_events;
  std::vector<TaskApiLinkSqlRow> task_api_links;
  std::vector<IdleCandidateSqlRow> idle_candidates;
  std::vector<IdleExplanationSqlRow> idle_explanations;
  std::vector<EvidenceLinkSqlRow> evidence_links;
  std::vector<AnchorIdleExplanationSqlRow> anchor_attribution;
  std::vector<NodeIdleExplanationSqlRow> node_attribution;
};

void materialize_compatibility_schema(const std::string& sqlite_path);

void materialize_compatibility_schema(
    const std::string& sqlite_path,
    const std::vector<CompatTableSchema>& schemas);

void materialize_report_compatibility_views(const std::string& sqlite_path);

void materialize_global_collective_compatibility_schema(
    const std::string& sqlite_path);

void replace_metadata_rows(const std::string& sqlite_path,
                           const std::vector<MetadataSqlRow>& rows);

void replace_event_rows(const std::string& sqlite_path,
                        const EventSqlRows& rows);

void replace_timeline_rows(const std::string& sqlite_path,
                           const std::vector<EventSqlRow>& rows);

void replace_event_source_rows(
    const std::string& sqlite_path,
    const std::vector<EventSourceSqlRow>& rows);

void replace_anchor_rows(const std::string& sqlite_path,
                         const std::vector<AnchorSqlRow>& rows);

void replace_anchor_cost_breakdown_rows(
    const std::string& sqlite_path,
    const std::vector<AnchorCostBreakdownSqlRow>& rows);

void replace_aux_attribution_rows(const std::string& sqlite_path,
                                  const AuxAttributionSqlRows& rows);

void replace_anchor_aux_rows(const std::string& sqlite_path,
                             const AnchorAuxSqlRows& rows);

void replace_loop_tree_rows(const std::string& sqlite_path,
                            const LoopTreeSqlRows& rows);

void replace_node_anchor_coverage_rows(
    const std::string& sqlite_path,
    const NodeAnchorCoverageSqlRows& rows);

void replace_node_coverage_rows(const std::string& sqlite_path,
                                const NodeCoverageSqlRows& rows);

void replace_graph_replay_evidence_rows(
    const std::string& sqlite_path,
    const GraphReplayEvidenceSqlRows& rows);

void replace_graph_replay_rows(const std::string& sqlite_path,
                               const GraphReplaySqlRows& rows);

void replace_collective_global_link_rows(
    const std::string& sqlite_path,
    const std::vector<CollectiveGlobalLinkSqlRow>& rows);

void replace_global_collective_summary_rows(
    const std::string& sqlite_path,
    const std::vector<GlobalCollectiveSummarySqlRow>& rows);

void replace_global_collective_member_rows(
    const std::string& sqlite_path,
    const std::vector<GlobalCollectiveMemberSqlRow>& rows);

void replace_global_collective_rows(const std::string& sqlite_path,
                                    const GlobalCollectiveSqlRows& rows);

void replace_semantic_tree_catalog_rows(
    const std::string& sqlite_path,
    const std::vector<SemanticTreeHeaderSqlRow>& rows);

void replace_semantic_graph_rows(const std::string& sqlite_path,
                                 const SemanticGraphSqlRows& rows);

void replace_semantic_tree_rows(const std::string& sqlite_path,
                                const SemanticTreeSqlRows& rows);

void replace_idle_evidence_rows(const std::string& sqlite_path,
                                const IdleEvidenceSqlRows& rows);

}  // namespace traceloom::compat
