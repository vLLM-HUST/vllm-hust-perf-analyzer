#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/compat/anchor_cost_breakdown_rows.h"
#include "traceloom/compat/schema.h"

namespace traceloom::compat {

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
  std::vector<AnchorSqlRow> anchors;
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

struct NodeCoverageSqlRows {
  std::vector<VizNodeSqlRow> nodes;
  std::vector<VizEdgeSqlRow> edges;
  std::vector<VizNodeAnchorSqlRow> node_anchors;
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

struct GraphReplaySqlRows {
  std::vector<EventSqlRow> events;
  std::vector<AnchorSqlRow> anchors;
  std::vector<GraphReplaySqlRow> graph_replays;
  std::vector<GraphEnvelopeSqlRow> graph_envelopes;
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

struct SemanticTreeSqlRows {
  std::vector<SemanticTreeHeaderSqlRow> trees;
  std::vector<SemanticNodeSqlRow> nodes;
};

void materialize_compatibility_schema(const std::string& sqlite_path);

void materialize_compatibility_schema(
    const std::string& sqlite_path,
    const std::vector<CompatTableSchema>& schemas);

void materialize_report_compatibility_views(const std::string& sqlite_path);

void replace_anchor_cost_breakdown_rows(
    const std::string& sqlite_path,
    const std::vector<AnchorCostBreakdownSqlRow>& rows);

void replace_anchor_aux_rows(const std::string& sqlite_path,
                             const AnchorAuxSqlRows& rows);

void replace_node_coverage_rows(const std::string& sqlite_path,
                                const NodeCoverageSqlRows& rows);

void replace_graph_replay_rows(const std::string& sqlite_path,
                               const GraphReplaySqlRows& rows);

void replace_semantic_tree_rows(const std::string& sqlite_path,
                                const SemanticTreeSqlRows& rows);

}  // namespace traceloom::compat
