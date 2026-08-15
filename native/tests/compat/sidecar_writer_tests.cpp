#include "sidecar_writer_test_support.h"
#include "traceloom/compat/schema.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/testing/test_util.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

int main() {
  using namespace traceloom::testing::sidecar_writer_test;
  using traceloom::testing::require;

  const std::string legacy_db_path = temp_db_path();
  execute_sql(
      legacy_db_path,
      "CREATE TABLE traceloom_viz_node_anchor ("
      "node_id TEXT NOT NULL, anchor_id TEXT NOT NULL, "
      "db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, "
      "view_name TEXT NOT NULL, occurrence_idx INTEGER NOT NULL, "
      "anchor_order INTEGER NOT NULL, coverage_kind TEXT NOT NULL, "
      "repeat_context TEXT)");
  traceloom::compat::materialize_compatibility_schema(
      legacy_db_path, {traceloom::compat::viz_node_anchor_table_schema()});
  require_columns_match_schema(
      legacy_db_path, traceloom::compat::viz_node_anchor_table_schema());
  std::remove(legacy_db_path.c_str());

  const std::string db_path = temp_db_path();
  traceloom::compat::materialize_compatibility_schema(db_path);
  traceloom::compat::materialize_compatibility_schema(db_path);

  std::vector<std::string> expected_tables;
  const std::vector<traceloom::compat::CompatTableSchema> table_schemas =
      traceloom::compat::compatibility_table_schemas();
  for (const traceloom::compat::CompatTableSchema& table_schema :
       table_schemas) {
    expected_tables.push_back(table_schema.name);
  }
  std::sort(expected_tables.begin(), expected_tables.end());
  require(expected_tables ==
          std::vector<std::string>({
              "traceloom_aclgraph_reconstruction_region",
              "traceloom_anchor",
              "traceloom_anchor_aux_slot",
              "traceloom_anchor_cost_breakdown",
              "traceloom_anchor_host_activity",
              "traceloom_anchor_host_api_summary",
              "traceloom_anchor_host_interval",
              "traceloom_anchor_primary_node",
              "traceloom_anchor_runtime_relation",
              "traceloom_anchor_symbol_normalization",
              "traceloom_aux_link",
              "traceloom_collective_global_link",
              "traceloom_cuda_graph_envelope",
              "traceloom_cuda_graph_replay",
              "traceloom_device_work",
              "traceloom_event",
              "traceloom_event_reconciliation_decision",
              "traceloom_event_reconciliation_member",
              "traceloom_event_reconciliation_policy",
              "traceloom_event_reconciliation_rule",
              "traceloom_event_source",
              "traceloom_graph_body_member",
              "traceloom_graph_launch",
              "traceloom_loop_node",
              "traceloom_metadata",
              "traceloom_replay_cost_aggregate",
              "traceloom_replay_cost_aggregate_member",
              "traceloom_replay_cost_issue",
              "traceloom_replay_cost_launch",
              "traceloom_replay_cost_member",
              "traceloom_replay_cost_stream",
              "traceloom_replay_cost_unit",
              "traceloom_runtime_call",
              "traceloom_runtime_device_relation",
              "traceloom_semantic_edge",
              "traceloom_semantic_node",
              "traceloom_semantic_tree",
              "traceloom_symbol_normalization_policy",
              "traceloom_symbol_normalization_rule",
              "traceloom_viz_edge",
              "traceloom_viz_node",
              "traceloom_viz_node_anchor",
          }));
  require(load_sqlite_master_names(db_path, "table") == expected_tables);

  for (const traceloom::compat::CompatTableSchema& table_schema :
       table_schemas) {
    require_columns_match_schema(db_path, table_schema);
  }

  const std::string global_db_path = temp_db_path();
  traceloom::compat::materialize_global_collective_compatibility_schema(
      global_db_path);
  std::vector<std::string> expected_global_tables;
  const std::vector<traceloom::compat::CompatTableSchema> global_schemas =
      traceloom::compat::global_collective_table_schemas();
  for (const traceloom::compat::CompatTableSchema& table_schema :
       global_schemas) {
    expected_global_tables.push_back(table_schema.name);
  }
  std::sort(expected_global_tables.begin(), expected_global_tables.end());
  require(expected_global_tables ==
          std::vector<std::string>({
              "traceloom_global_collective_member",
              "traceloom_global_collective_summary",
          }));
  require(load_sqlite_master_names(global_db_path, "table") ==
          expected_global_tables);
  for (const traceloom::compat::CompatTableSchema& table_schema :
       global_schemas) {
    require_columns_match_schema(global_db_path, table_schema);
  }
  require(load_sqlite_master_names(global_db_path, "index") ==
          std::vector<std::string>({
              "idx_global_collective_member_key",
              "idx_global_collective_status",
          }));
  std::remove(global_db_path.c_str());

  traceloom::compat::replace_metadata_rows(
      db_path,
      {
          {"source_db", "/tmp/msprof.db"},
          {"traceloom_schema_version", "augmented_db_v1"},
      });
  std::vector<StoredMetadataRow> metadata_rows = load_metadata_rows(db_path);
  require(metadata_rows.size() == 2);
  require(metadata_rows[0].key == "source_db");
  require(metadata_rows[0].value == "/tmp/msprof.db");
  require(metadata_rows[1].key == "traceloom_schema_version");
  require(metadata_rows[1].value == "augmented_db_v1");

  traceloom::compat::replace_metadata_rows(
      db_path, {{"traceloom_schema_version", "compat-v2"}});
  metadata_rows = load_metadata_rows(db_path);
  require(metadata_rows.size() == 1);
  require(metadata_rows[0].key == "traceloom_schema_version");
  require(metadata_rows[0].value == "compat-v2");

  traceloom::compat::EventSqlRows event_rows;
  traceloom::compat::EventSqlRow event;
  event.event_id = "event-1";
  event.step_idx = 1;
  event.source_table = "TASK";
  event.source_key = "task-1";
  event.stream_id = 7;
  event.start_ns = 100;
  event.end_ns = 300;
  event.dur_us = 0.2;
  event.category = "exec";
  event.role = "compute";
  event.semantic_role = "anchor";
  event.symbol = "MatMul";
  event_rows.events.push_back(event);
  traceloom::compat::EventSourceSqlRow event_source;
  event_source.event_id = event.event_id;
  event_source.source_ordinal = 0;
  event_source.source_table = event.source_table;
  event_source.source_key = event.source_key;
  event_source.source_role = "primary";
  event_rows.event_sources.push_back(event_source);
  traceloom::compat::replace_event_rows(db_path, event_rows);
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_event") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_event_source") == 1);
  require(run_scalar_text(db_path,
                          "SELECT symbol FROM traceloom_event "
                          "WHERE event_id = 'event-1'") == "MatMul");
  event_rows.event_sources.clear();
  traceloom::compat::replace_event_rows(db_path, event_rows);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_event_source") == 0);
  traceloom::compat::replace_event_source_rows(db_path, {event_source});
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_event_source") == 1);
  traceloom::compat::EventSqlRow timeline_event = event;
  timeline_event.symbol = "LayerNorm";
  timeline_event.label = "LayerNorm";
  traceloom::compat::replace_timeline_rows(db_path, {timeline_event});
  require(run_scalar_text(db_path,
                          "SELECT symbol FROM traceloom_event "
                          "WHERE event_id = 'event-1'") == "LayerNorm");
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_event_source") == 1);
  traceloom::compat::replace_event_source_rows(db_path, {});
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_event_source") == 0);

  std::vector<traceloom::compat::AnchorSqlRow> anchor_rows(2);
  anchor_rows[0].anchor_id = "anchor-1";
  anchor_rows[0].anchor_idx = 1;
  anchor_rows[0].event_id = "event-1";
  anchor_rows[0].step_idx = 1;
  anchor_rows[0].symbol = "MatMul";
  anchor_rows[0].role = "compute";
  anchor_rows[0].label = "MatMul";
  anchor_rows[0].family = "compute";
  anchor_rows[0].start_ns = 100;
  anchor_rows[0].end_ns = 300;
  anchor_rows[0].dur_us = 0.2;
  anchor_rows[1] = anchor_rows[0];
  anchor_rows[1].anchor_id = "anchor-2";
  anchor_rows[1].anchor_idx = 2;
  anchor_rows[1].step_idx = 2;
  traceloom::compat::replace_anchor_rows(db_path, anchor_rows);
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_anchor") ==
          2);
  require(run_scalar_text(db_path,
                          "SELECT symbol FROM traceloom_anchor "
                          "WHERE anchor_id = 'anchor-1'") == "MatMul");
  traceloom::compat::replace_anchor_rows(db_path, {anchor_rows.front()});
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_anchor") ==
          1);

  traceloom::compat::AuxAttributionSqlRows aux_rows;
  traceloom::compat::AnchorAuxSlotSqlRow aux_slot;
  aux_slot.anchor_id = "anchor-1";
  aux_slot.anchor_idx = 1;
  aux_slot.anchor_step_idx = 1;
  aux_slot.aux_start_step_idx = 0;
  aux_slot.aux_end_step_idx = 0;
  aux_slot.aux_event_count = 1;
  aux_slot.aux_dur_us = 0.1;
  aux_rows.aux_slots.push_back(aux_slot);
  traceloom::compat::AuxLinkSqlRow aux_link;
  aux_link.anchor_id = "anchor-1";
  aux_link.aux_event_id = "event-aux-1";
  aux_link.aux_order = 0;
  aux_link.aux_step_idx = 0;
  aux_link.link_type = "prelude";
  aux_link.reason = "step_precedes_anchor";
  aux_link.aux_kind = "runtime";
  aux_link.aux_dur_us = 0.1;
  aux_rows.aux_links.push_back(aux_link);
  traceloom::compat::replace_aux_attribution_rows(db_path, aux_rows);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_anchor_aux_slot") == 1);
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_aux_link") ==
          1);
  traceloom::compat::replace_aux_attribution_rows(
      db_path, traceloom::compat::AuxAttributionSqlRows{});
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_anchor_aux_slot") == 0);
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_aux_link") ==
          0);

  run_graph_projection_tests(db_path, expected_tables, table_schemas);

  std::remove(db_path.c_str());
  return 0;
}
