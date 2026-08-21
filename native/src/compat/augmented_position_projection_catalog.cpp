#include "augmented_position_projection_catalog.h"

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
  for (const auto& row : rows) {
    std::string sql = "INSERT INTO " + table + " VALUES(";
    for (std::size_t index = 0; index < row.size(); ++index) {
      if (index != 0) {
        sql += ',';
      }
      sql += quote_literal(row[index]);
    }
    sqlite_exec(db, sql + ')', context);
  }
}

}  // namespace

void materialize_position_projection_catalog(sqlite3* db) {
  sqlite_exec(
      db,
      "INSERT INTO traceloom_metadata(key, value) VALUES"
      "('structural_coordinate_model', "
      "'hierarchical_position_occurrence_v1')",
      "failed to insert HPO model metadata");

  insert_values(
      db, "traceloom_analysis_surface",
      {
          {"hierarchical_position", "traceloom_v_position",
           "one reusable hierarchical Position definition",
           "select a stable structural coordinate at any refinement depth",
           "SELECT * FROM traceloom_v_position ORDER BY db_idx, tree_id, "
           "preorder_idx;"},
          {"position_refinement", "traceloom_position_refinement",
           "one locally ordered child Position slot",
           "expand a composite Position without confusing slot order with "
           "measured member order",
           "SELECT * FROM traceloom_position_refinement ORDER BY db_idx, "
           "tree_id, parent_position_id, slot_ordinal;"},
          {"position_occurrence", "traceloom_v_position_occurrence",
           "one measured Occurrence of one Position",
           "select one or compare all realizations while retaining rooted "
           "Position and Occurrence paths",
           "SELECT * FROM traceloom_v_position_occurrence ORDER BY db_idx, "
           "tree_id, position_id, occurrence_idx;"},
          {"position_direct_member", "traceloom_v_position_member",
           "one direct measured member of one Position Occurrence",
           "expand a realization to child Occurrences or terminal event "
           "evidence without using transitive coverage",
           "SELECT * FROM traceloom_v_position_member ORDER BY db_idx, "
           "tree_id, parent_occurrence_id, slot_ordinal, member_order;"},
          {"execution_tree_edge", "traceloom_v_tree_edge",
           "one concrete ordered parent-to-child Occurrence edge",
           "read realized tree order without exposing storage slot and member "
           "axes",
           "SELECT * FROM traceloom_v_tree_edge ORDER BY db_idx, tree_id, "
           "parent_occurrence_id, edge_order;"},
          {"execution_tree_edge_role", "traceloom_v_tree_edge_role",
           "one contextual structural equivalence class of tree edges",
           "discover the only edge populations that are valid to aggregate",
           "SELECT * FROM traceloom_v_tree_edge_role ORDER BY db_idx, tree_id, "
           "parent_tree_path, first_edge_order;"},
          {"execution_tree_edge_cost", "traceloom_v_tree_edge_cost",
           "one concrete tree edge with its child Occurrence cost lenses",
           "compare equivalent edges while retaining exact child Occurrence "
           "coordinates",
           "SELECT * FROM traceloom_v_tree_edge_cost ORDER BY db_idx, tree_id, "
           "parent_occurrence_idx, edge_order;"},
          {"replay_hierarchical_position",
           "traceloom_v_replay_body_position_definition",
           "one reusable hierarchical Position in a replay-body domain",
           "query recursive replay structure without promoting grammar "
           "patterns to analytical entities",
           "SELECT * FROM traceloom_v_replay_body_position_definition ORDER "
           "BY domain_id, display_depth, position_id;"},
          {"replay_position_direct_member",
           "traceloom_v_replay_body_position_direct_member",
           "one direct child Occurrence or terminal replay Position",
           "drill through replay hierarchy while preserving exact aggregate "
           "and source coordinates",
           "SELECT * FROM traceloom_v_replay_body_position_direct_member "
           "ORDER BY domain_id, parent_occurrence_id, slot_ordinal, "
           "member_order;"},
      },
      "failed to insert HPO analysis surface");

  insert_values(
      db, "traceloom_projection_recipe",
      {
          {"hpo_positions", "6", "hierarchical_position",
           "candidate_scopes", "folded", "device", "position_cost",
           "(none)", "select a reusable Position at any structural depth",
           "SELECT * FROM traceloom_v_position ORDER BY total_us DESC, "
           "db_idx, tree_id, preorder_idx;"},
          {"hpo_refinements", "7", "hierarchical_position", "definition",
           "immediate_child_slots", "device", "position_definition",
           ":position_id", "expand one composite Position into locally "
           "ordered child Position slots",
           "SELECT r.*, p.node_type, p.symbol, p.label, p.repeat_count FROM "
           "traceloom_position_refinement r JOIN traceloom_v_position p ON "
           "p.position_id=r.child_position_id AND p.db_idx=r.db_idx AND "
           "p.tree_id=r.tree_id WHERE r.parent_position_id=:position_id "
           "ORDER BY r.slot_ordinal;"},
          {"hpo_occurrences", "8", "hierarchical_position",
           "one_or_all_occurrences", "realized", "device",
           "occurrence_cost", ":position_id, :occurrence_id (NULL selects "
           "all)", "inspect one or all measured realizations of one Position",
           "SELECT * FROM traceloom_v_position_occurrence WHERE "
           "position_id=:position_id AND (:occurrence_id IS NULL OR "
           "occurrence_id=:occurrence_id) ORDER BY occurrence_idx;"},
          {"hpo_members", "9", "position_occurrence", "one_occurrence",
           "direct_members", "device", "member_evidence", ":occurrence_id",
           "expand one Occurrence to direct child Occurrences or terminal "
           "events while keeping slot and member order distinct",
           "SELECT * FROM traceloom_v_position_member WHERE "
           "parent_occurrence_id=:occurrence_id ORDER BY slot_ordinal, "
           "member_order;"},
          {"tree_edge_roles", "10", "hierarchical_position",
           "structural_edge_classes", "folded_roles", "device",
           "edge_population_support", ":position_id",
           "list contextual edge roles under one selected structural node",
           "SELECT * FROM traceloom_v_tree_edge_role WHERE "
           "parent_position_id=:position_id ORDER BY parent_tree_path, "
           "first_edge_order;"},
          {"tree_edges", "11", "position_occurrence", "one_occurrence",
           "ordered_child_edges", "device", "child_occurrence_identity",
           ":occurrence_id", "read one concrete child-edge stream in measured "
           "tree order",
           "SELECT * FROM traceloom_v_tree_edge WHERE "
           "parent_occurrence_id=:occurrence_id ORDER BY edge_order;"},
          {"equivalent_tree_edges", "12", "execution_tree_edge_role",
           "one_or_all_parent_occurrences", "equivalent_edge_population",
           "device", "child_occurrence_cost",
           ":edge_role_id, :occurrence_id (NULL selects all parents)",
           "compare only concrete edges that share one contextual role",
           "SELECT * FROM traceloom_v_tree_edge_cost WHERE "
           "edge_role_id=:edge_role_id AND (:occurrence_id IS NULL OR "
           "parent_occurrence_id=:occurrence_id) ORDER BY "
           "parent_occurrence_idx, edge_order;"},
          {"replay_hpo_positions", "41", "replay_body_domain",
           "definitions", "folded", "device", "position_definition",
           ":domain_id", "select hierarchical Positions recovered in one "
           "exact replay-body domain",
           "SELECT * FROM traceloom_v_replay_body_position_definition WHERE "
           "domain_id=:domain_id ORDER BY display_depth, position_id;"},
          {"replay_hpo_refinements", "42", "hierarchical_position",
           "definition", "immediate_child_slots", "device",
           "position_definition", ":position_id", "expand one replay-body "
           "Position into ordered child slots",
           "SELECT * FROM traceloom_replay_body_position_refinement WHERE "
           "parent_position_id=:position_id ORDER BY slot_ordinal;"},
          {"replay_hpo_occurrences", "43", "hierarchical_position",
           "all_occurrences", "realized", "device", "occurrence_cost",
           ":position_id", "compare all measured Occurrences of one "
           "replay-body Position",
           "SELECT * FROM traceloom_v_replay_body_position_occurrence WHERE "
           "position_id=:position_id ORDER BY occurrence_index;"},
          {"replay_hpo_members", "44", "position_occurrence",
           "one_occurrence", "direct_members", "device",
           "member_cost_and_lineage", ":occurrence_id", "expand one replay "
           "Occurrence without flattening child hierarchy",
           "SELECT * FROM traceloom_v_replay_body_position_direct_member "
           "WHERE parent_occurrence_id=:occurrence_id ORDER BY slot_ordinal, "
           "member_order;"},
      },
      "failed to insert HPO projection recipe");

  insert_values(
      db, "traceloom_projection_parameter",
      {
          {"hpo_refinements", "10", "position_id", "TEXT", "0",
           "hierarchical_position_id", "traceloom_v_position", "position_id",
           "selected composite Position"},
          {"hpo_occurrences", "10", "position_id", "TEXT", "0",
           "hierarchical_position_id", "traceloom_v_position", "position_id",
           "selected Position"},
          {"hpo_occurrences", "20", "occurrence_id", "TEXT", "1",
           "position_occurrence_id", "traceloom_position_occurrence",
           "occurrence_id", "NULL selects the complete Occurrence population"},
          {"hpo_members", "10", "occurrence_id", "TEXT", "0",
           "position_occurrence_id", "traceloom_position_occurrence",
           "occurrence_id", "selected measured Position realization"},
          {"tree_edge_roles", "10", "position_id", "TEXT", "0",
           "hierarchical_position_id", "traceloom_v_position", "position_id",
           "selected structural parent"},
          {"tree_edges", "10", "occurrence_id", "TEXT", "0",
           "position_occurrence_id", "traceloom_v_position_occurrence",
           "occurrence_id", "selected concrete parent realization"},
          {"equivalent_tree_edges", "10", "edge_role_id", "TEXT", "0",
           "execution_tree_edge_role_id", "traceloom_v_tree_edge_role",
           "edge_role_id", "selected contextual edge equivalence class"},
          {"equivalent_tree_edges", "20", "occurrence_id", "TEXT", "1",
           "position_occurrence_id", "traceloom_v_position_occurrence",
           "occurrence_id", "NULL selects the role across all parent "
           "Occurrences"},
          {"replay_hpo_positions", "10", "domain_id", "TEXT", "0",
           "replay_body_domain_id", "traceloom_replay_body_pattern_domain",
           "domain_id", "selected exact replay-body domain"},
          {"replay_hpo_refinements", "10", "position_id", "TEXT", "0",
           "hierarchical_position_id",
           "traceloom_v_replay_body_position_definition", "position_id",
           "selected replay-body composite Position"},
          {"replay_hpo_occurrences", "10", "position_id", "TEXT", "0",
           "hierarchical_position_id",
           "traceloom_v_replay_body_position_definition", "position_id",
           "selected replay-body Position"},
          {"replay_hpo_members", "10", "occurrence_id", "TEXT", "0",
           "position_occurrence_id",
           "traceloom_v_replay_body_position_occurrence", "occurrence_id",
           "selected replay-body Position Occurrence"},
      },
      "failed to insert HPO projection parameter");

  insert_values(
      db, "traceloom_projection_coordinate",
      {
          {"hpo_positions", "10", "position_id",
           "hierarchical_position_id", "selected structural Position"},
          {"hpo_refinements", "10", "parent_position_id",
           "hierarchical_position_id", "current composite Position"},
          {"hpo_refinements", "20", "child_position_id",
           "hierarchical_position_id", "child Position available to expand"},
          {"hpo_occurrences", "10", "position_id",
           "hierarchical_position_id", "selected structural Position"},
          {"hpo_occurrences", "20", "occurrence_id",
           "position_occurrence_id", "measured realization available to expand"},
          {"hpo_members", "10", "parent_position_id",
           "hierarchical_position_id", "owning Position"},
          {"hpo_members", "20", "parent_occurrence_id",
           "position_occurrence_id", "owning Occurrence"},
          {"hpo_members", "30", "child_position_id",
           "hierarchical_position_id", "direct child Position when present"},
          {"hpo_members", "40", "child_occurrence_id",
           "position_occurrence_id", "direct child Occurrence when present"},
          {"hpo_members", "50", "event_id", "normalized_event_id",
           "terminal event available for source audit"},
          {"tree_edge_roles", "10", "parent_position_id",
           "hierarchical_position_id", "selected structural parent"},
          {"tree_edge_roles", "20", "edge_role_id",
           "execution_tree_edge_role_id", "context-safe aggregation key"},
          {"tree_edge_roles", "30", "child_position_id",
           "hierarchical_position_id", "child structure available to expand"},
          {"tree_edges", "10", "edge_id", "execution_tree_edge_id",
           "selected concrete tree edge"},
          {"tree_edges", "20", "edge_role_id",
           "execution_tree_edge_role_id", "edge population available to "
           "compare"},
          {"tree_edges", "30", "parent_occurrence_id",
           "position_occurrence_id", "owning parent Occurrence"},
          {"tree_edges", "40", "child_position_id",
           "hierarchical_position_id", "child structural node"},
          {"tree_edges", "50", "child_occurrence_id",
           "position_occurrence_id", "child Occurrence available to expand"},
          {"equivalent_tree_edges", "10", "edge_id",
           "execution_tree_edge_id", "concrete member of the selected role"},
          {"equivalent_tree_edges", "20", "edge_role_id",
           "execution_tree_edge_role_id", "held structural equivalence class"},
          {"equivalent_tree_edges", "30", "parent_occurrence_id",
           "position_occurrence_id", "owning parent Occurrence"},
          {"equivalent_tree_edges", "40", "child_position_id",
           "hierarchical_position_id", "child structural node"},
          {"equivalent_tree_edges", "50", "child_occurrence_id",
           "position_occurrence_id", "measured child available to expand"},
          {"replay_hpo_positions", "10", "position_id",
           "hierarchical_position_id", "selected replay-body Position"},
          {"replay_hpo_refinements", "10", "parent_position_id",
           "hierarchical_position_id", "current replay-body Position"},
          {"replay_hpo_refinements", "20", "child_position_id",
           "hierarchical_position_id", "child replay-body Position"},
          {"replay_hpo_occurrences", "10", "position_id",
           "hierarchical_position_id", "selected replay-body Position"},
          {"replay_hpo_occurrences", "20", "occurrence_id",
           "position_occurrence_id", "replay-body Position Occurrence"},
          {"replay_hpo_members", "10", "parent_position_id",
           "hierarchical_position_id", "owning replay-body Position"},
          {"replay_hpo_members", "20", "parent_occurrence_id",
           "position_occurrence_id", "owning replay-body Occurrence"},
          {"replay_hpo_members", "30", "child_position_id",
           "hierarchical_position_id", "direct child Position when present"},
          {"replay_hpo_members", "40", "child_occurrence_id",
           "position_occurrence_id", "direct child Occurrence when present"},
          {"replay_hpo_members", "50", "terminal_aggregate_id",
           "replay_cost_aggregate_id", "terminal replay cost coordinate"},
      },
      "failed to insert HPO projection coordinate");
}

}  // namespace traceloom::compat::detail
#endif
