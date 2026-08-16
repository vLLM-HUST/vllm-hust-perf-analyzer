#include "augmented_projection_catalog.h"

#include <string>
#include <vector>

#include "sidecar_sqlite_utils.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
namespace traceloom::compat::detail {

void materialize_projection_catalog(
    sqlite3* db,
    const RawPackagingResult& packaging) {
  const std::vector<std::vector<std::string>> surfaces = {
      {"composable_projection", "traceloom_projection_recipe",
       "one reusable analytical projection recipe",
       "select a scope and compose population, resolution, observation "
       "domain, and measure lens",
       "SELECT projection_name, scope_kind, population_mode, resolution, "
       "observation_domain, measure_lens, selector_parameters, purpose "
       "FROM traceloom_projection_recipe ORDER BY display_order;"},
      {"projection_parameter", "traceloom_projection_parameter",
       "one named parameter accepted by one projection recipe",
       "discover typed selectors and the public relation that supplies "
       "candidate coordinates",
       "SELECT * FROM traceloom_projection_parameter ORDER BY "
       "projection_name, parameter_order;"},
      {"projection_coordinate", "traceloom_projection_coordinate",
       "one reusable coordinate returned by one projection recipe",
       "discover which result columns remain valid inputs to later "
       "projections",
       "SELECT * FROM traceloom_projection_coordinate ORDER BY "
       "projection_name, coordinate_order;"},
      {"projection_continuation", "traceloom_v_projection_continuation",
       "one ready coordinate transfer between projection recipes",
       "discover compatible next queries without parsing example SQL or "
       "reconstructing scope identity",
       "SELECT * FROM traceloom_v_projection_continuation ORDER BY "
       "source_projection, target_projection, source_column;"},
      {"tree_map", "traceloom_v_tree_node", "one structural node",
       "read the hierarchical cost map from coarse loops to leaves",
       "SELECT * FROM traceloom_v_tree_node ORDER BY db_idx, device_id, "
       "view_name, display_order;"},
      {"tree_occurrence", "traceloom_tree_node_occurrence",
       "one structural node occurrence",
       "compare repeated instances without losing hierarchy",
       "SELECT * FROM traceloom_tree_node_occurrence ORDER BY db_idx, "
       "device_id, view_name, node_id, occurrence_idx;"},
      {"node_cost", "traceloom_v_node_cost", "one structural node cost",
       "rank and compare overlap-safe cost lenses by structural node",
       "SELECT * FROM traceloom_v_node_cost ORDER BY total_us DESC, "
       "db_idx, device_id, view_name, node_id;"},
      {"normalized_event", "traceloom_event", "one normalized event",
       "inspect fine-grained timing and operator evidence",
       "SELECT * FROM traceloom_event ORDER BY db_idx, device_id, step_idx;"},
      {"event_reconciliation_policy",
       "traceloom_event_reconciliation_policy",
       "one effective event-reconciliation policy",
       "audit the manifest identity, digest, and unmatched behavior used "
       "for this analysis",
       "SELECT * FROM traceloom_event_reconciliation_policy ORDER BY "
       "policy_id;"},
      {"event_reconciliation_rule",
       "traceloom_event_reconciliation_rule",
       "one effective event-reconciliation rule",
       "inspect the exact provider predicate, containment threshold, and "
       "rule origin admitted by the effective policy",
       "SELECT * FROM traceloom_event_reconciliation_rule ORDER BY "
       "priority DESC, rule_id;"},
      {"event_reconciliation_decision",
       "traceloom_event_reconciliation_decision",
       "one candidate event-reconciliation group",
       "find reconciled, independent, ambiguous, and conflicting candidate "
       "groups before drilling into member contributions",
       "SELECT status, reason_code, COUNT(*) AS decision_count FROM "
       "traceloom_event_reconciliation_decision GROUP BY status, "
       "reason_code ORDER BY status, reason_code;"},
      {"event_reconciliation_member",
       "traceloom_event_reconciliation_member",
       "one normalized event in one reconciliation decision",
       "audit which original observation contributes timing, structural "
       "symbol, or cost and retain its raw-source locator",
       "SELECT * FROM traceloom_event_reconciliation_member ORDER BY "
       "db_idx, decision_id, member_order LIMIT 200;"},
      {"event_reconciliation", "traceloom_v_event_reconciliation",
       "one candidate observation in one sparse reconciliation decision",
       "audit when multiple profiler rows contribute timing, symbol, and "
       "cost to one canonical anchor without deleting raw evidence",
       "SELECT * FROM traceloom_v_event_reconciliation ORDER BY db_idx, "
       "decision_id, member_order;"},
      {"evidence_role_policy", "traceloom_evidence_role_policy",
       "one effective projection policy",
       "audit the flat input table identity and explicit config precedence",
       "SELECT * FROM traceloom_evidence_role_policy ORDER BY policy_id;"},
      {"evidence_role_rule", "traceloom_evidence_role_rule",
       "one effective policy or system rule",
       "explain stable rule identifiers, predicates, capabilities, and "
       "retention treatments",
       "SELECT * FROM traceloom_evidence_role_rule ORDER BY policy_id, "
       "priority DESC, declaration_order, rule_id;"},
      {"evidence_role_decision", "traceloom_v_evidence_role_decision",
       "one normalized event projection decision",
       "walk from an event or raw-source locator to its typed role outcome",
       "SELECT * FROM traceloom_v_evidence_role_decision WHERE event_id = "
       "'event-0';"},
      {"evidence_role_placement", "traceloom_v_evidence_role_placement",
       "one role-decision structural placement",
       "walk in either direction between a role decision and retained "
       "anchor, auxiliary, graph, replay, or boundary membership",
       "SELECT * FROM traceloom_v_evidence_role_placement WHERE "
       "placement_id = 'anchor-0' OR owner_id = 'anchor-0' ORDER BY "
       "decision_id, placement_order;"},
      {"evidence_role_structure", "traceloom_v_evidence_role_structure",
       "one role-decision placement in recovered structure",
       "locate anchor, omitted, and protected evidence in tree occurrences",
       "SELECT * FROM traceloom_v_evidence_role_structure WHERE event_id = "
       "'event-0' ORDER BY node_id, occurrence_idx, placement_kind;"},
      {"evidence_role_cost_coverage",
       "traceloom_v_evidence_role_cost_coverage",
       "one provider, policy, role, and support-state cost aggregate",
       "compare retained cost inside and outside identity matching",
       "SELECT * FROM traceloom_v_evidence_role_cost_coverage ORDER BY "
       "db_idx, input_provider_scope, final_role, support_state;"},
      {"evidence_role_issue", "traceloom_evidence_role_issue",
       "one typed projection audit issue",
       "find conflicts, missing placement, and unsupported outcomes",
       "SELECT * FROM traceloom_evidence_role_issue ORDER BY code, "
       "decision_id, issue_id;"},
      {"protected_interval", "traceloom_protected_interval",
       "one typed generic-discovery boundary",
       "audit exact and typed-open protected composites without inferring "
       "membership from timestamps",
       "SELECT * FROM traceloom_protected_interval ORDER BY db_idx, "
       "device_id, start_ns, protected_interval_id;"},
      {"exact_replay_partition_status",
       "traceloom_v_exact_replay_partition_status",
       "one device-tree replay-partition support result",
       "check whether exact replay intervals form one ordered disjoint "
       "partition domain before reading its cost segments",
       "SELECT * FROM traceloom_v_exact_replay_partition_status ORDER BY "
       "db_idx, device_id, tree_id;"},
      {"exact_replay_partition", "traceloom_v_exact_replay_partition",
       "one open, replay, or between-replays device segment",
       "compare a complete right-anchored cost partition induced by exact "
       "replay boundaries without reconstructing intervals in client SQL",
       "SELECT tree_id,db_idx,device_id,segment_order,coordinate_kind,"
       "coordinate_index,segment_label,anchor_count,compute_us,comm_us,"
       "idle_us,total_us,aux_us FROM "
       "traceloom_v_exact_replay_partition ORDER BY db_idx,device_id,"
       "tree_id,segment_order;"},
      {"symbol_normalization_rule",
       "traceloom_symbol_normalization_rule",
       "one versioned structural-symbol rule",
       "audit explicit provider aliases and typed fallback behavior",
       "SELECT * FROM traceloom_symbol_normalization_rule ORDER BY "
       "policy_id, policy_version, precedence, rule_id;"},
      {"anchor_symbol_lineage", "traceloom_v_anchor_symbol_lineage",
       "one structural anchor symbol decision",
       "explain an anchor's observed provider symbol, structural symbol, "
       "rule, and source locator",
       "SELECT * FROM traceloom_v_anchor_symbol_lineage ORDER BY db_idx, "
       "device_id, anchor_idx;"},
      {"symbol_normalization_placement",
       "traceloom_v_symbol_normalization_placement",
       "one symbol decision in one recovered structural placement",
       "walk between backend identity and family occurrence position",
       "SELECT * FROM traceloom_v_symbol_normalization_placement WHERE "
       "coverage_kind = 'self' ORDER BY db_idx, device_id, node_id, "
       "occurrence_idx, anchor_order;"},
      {"symbol_variant_cost", "traceloom_v_symbol_variant_cost",
       "one structural position and observed backend symbol",
       "compare counts and cost distributions across backend variants",
       "SELECT * FROM traceloom_v_symbol_variant_cost ORDER BY total_us "
       "DESC, db_idx, device_id, node_id, anchor_order, "
       "observed_symbol;"},
      {"runtime_device_relation", "traceloom_v_runtime_device",
       "one runtime-call/device-work relation outcome",
       "inspect direct provider correlation, explicit cardinality, and open "
       "or ambiguous outcomes",
       "SELECT * FROM traceloom_v_runtime_device ORDER BY db_idx, provider, "
       "runtime_start_ns, device_start_ns, relation_id LIMIT 200;"},
      {"synchronization_action", "traceloom_v_sync_runtime_call",
       "one profiler-visible synchronization action/runtime relation",
       "inspect typed synchronization observations and their exact, "
       "deterministic, ambiguous, or rejected runtime endpoints",
       "SELECT * FROM traceloom_v_sync_runtime_call ORDER BY db_idx, "
       "device_start_ns, sync_action_id, runtime_start_ns LIMIT 200;"},
      {"anchor_runtime_call", "traceloom_v_anchor_runtime_call",
       "one structural anchor/runtime-call relation",
       "walk backward from anchor or graph launch to observed host runtime",
       "SELECT * FROM traceloom_v_anchor_runtime_call ORDER BY db_idx, "
       "device_id, anchor_idx, runtime_start_ns, relation_id LIMIT 200;"},
      {"node_runtime_call", "traceloom_v_node_runtime_call",
       "one tree-node occurrence/anchor/runtime-call placement",
       "query host/device relations inside recovered structure in either "
       "direction",
       "SELECT * FROM traceloom_v_node_runtime_call WHERE coverage_kind = "
       "'self' ORDER BY db_idx, device_id, node_id, occurrence_idx, "
       "anchor_order, runtime_start_ns LIMIT 200;"},
      {"aux_runtime_call", "traceloom_v_aux_runtime_call",
       "one auxiliary-device-event/runtime-call relation",
       "walk backward from device auxiliary work to observed host runtime",
       "SELECT * FROM traceloom_v_aux_runtime_call ORDER BY db_idx, "
       "device_id, anchor_id, aux_order, runtime_start_ns LIMIT 200;"},
      {"anchor_host_interval", "traceloom_v_anchor_host_interval",
       "one adjacent-anchor pair with host runtime endpoints",
       "inspect whether adjacent device anchors delimit a queryable host "
       "runtime interval",
       "SELECT * FROM traceloom_v_anchor_host_interval ORDER BY db_idx, "
       "device_id, left_anchor_id LIMIT 200;"},
      {"node_host_interval", "traceloom_v_node_host_interval",
       "one node-occurrence/anchor position and typed host interval",
       "retain every structural coordinate while inspecting supported or "
       "unsupported adjacent-anchor host endpoints",
       "SELECT * FROM traceloom_v_node_host_interval WHERE coverage_kind = "
       "'self' ORDER BY db_idx, device_id, node_id, occurrence_idx, "
       "anchor_order LIMIT 200;"},
      {"anchor_host_activity", "traceloom_v_anchor_host_activity",
       "one observed runtime call overlapping an anchor-delimited host "
       "interval",
       "inspect profiler-visible host runtime behavior between device "
       "structure endpoints without assigning an idle cause",
       "SELECT * FROM traceloom_v_anchor_host_activity WHERE left_anchor_id "
       "= (SELECT left_anchor_id FROM traceloom_anchor_host_interval WHERE "
       "support_state = 'supported_ordered' ORDER BY db_idx, device_id, "
       "host_start_ns LIMIT 1) ORDER BY observed_start_ns, "
       "observed_runtime_call_id LIMIT 200;"},
      {"node_host_activity", "traceloom_v_node_host_activity",
       "one node-occurrence/anchor-delimited observed runtime call",
       "compare profiler-visible host runtime distributions after the same "
       "recovered structural position across occurrences",
       "SELECT * FROM traceloom_v_node_host_activity WHERE coverage_kind = "
       "'self' AND node_id = (SELECT node_id FROM "
       "traceloom_v_tree_node WHERE node_type = 'Atom' AND "
       "occurrence_count > 1 "
       "ORDER BY total_us DESC LIMIT 1) ORDER BY occurrence_idx, "
       "anchor_order, observed_order LIMIT 200;"},
      {"structure_bubble", "traceloom_v_structure_bubble_occurrence",
       "one uncovered device interval before one structural occurrence",
       "rank overlap-safe device bubbles and inspect their supported host "
       "observation scope without assigning a cause",
       "SELECT * FROM traceloom_v_structure_bubble_occurrence ORDER BY "
       "bubble_us DESC, bubble_id LIMIT 200;"},
      {"structure_bubble_position",
       "traceloom_v_structure_bubble_position",
       "one recurrent structural bubble position",
       "rank overlap-safe bubble populations while retaining typed host "
       "support counts for every position",
       "SELECT * FROM traceloom_v_structure_bubble_position ORDER BY "
       "total_bubble_us DESC, structural_position_id;"},
      {"structure_bubble_api_distribution",
       "traceloom_v_structure_bubble_api_stats",
       "one structural position and public runtime API family",
       "compare bubble costs with upstream API-family occurrence, count, "
       "duration, and observation-coverage distributions",
       "SELECT * FROM traceloom_v_structure_bubble_api_stats ORDER BY "
       "total_bubble_us DESC, structural_position_id, api_family;"},
      {"structure_bubble_host_context",
       "traceloom_v_structure_bubble_host_context",
       "one recurrent bubble position and optional host API family",
       "change observation domain without dropping unsupported-only or "
       "supported-but-empty structural positions",
       "SELECT * FROM traceloom_v_structure_bubble_host_context ORDER BY "
       "total_bubble_us DESC, structural_position_id, api_family;"},
      {"structure_bubble_runtime_call",
       "traceloom_v_structure_bubble_runtime_call",
       "one profiler-visible runtime call in one bubble observation scope",
       "drill from a selected bubble distribution to exact runtime calls "
       "and source locators without causal attribution",
       "SELECT * FROM traceloom_v_structure_bubble_runtime_call WHERE "
       "bubble_id = (SELECT bubble_id FROM "
       "traceloom_v_structure_bubble_occurrence ORDER BY bubble_us DESC "
       "LIMIT 1) ORDER BY observed_order LIMIT 200;"},
      {"event_source", "traceloom_v_event_source_locator",
       "one event-to-raw-source link",
       "resolve normalized evidence to the embedded profiler table",
       "SELECT * FROM traceloom_v_event_source_locator ORDER BY db_idx, "
       "device_id, event_id, source_ordinal;"},
      {"runtime_call_source", "traceloom_v_runtime_call_source_locator",
       "one runtime call-to-raw-source locator",
       "resolve host runtime observations to embedded profiler rows",
       "SELECT * FROM traceloom_v_runtime_call_source_locator ORDER BY "
       "db_idx, provider, start_ns, runtime_call_id;"},
      {"device_work_source", "traceloom_v_device_work_source_locator",
       "one device-work-to-raw-source locator",
       "resolve correlated device work to embedded profiler rows",
       "SELECT * FROM traceloom_v_device_work_source_locator ORDER BY "
       "db_idx, device_id, start_ns, device_work_id;"},
      {"raw_table", "traceloom_raw_table", "one embedded profiler table",
       "discover collision-free raw evidence storage",
       "SELECT * FROM traceloom_raw_table ORDER BY source_id, source_table;"},
  };
  for (const auto& surface : surfaces) {
    sqlite_exec(db,
                "INSERT INTO traceloom_analysis_surface VALUES(" +
                    quote_literal(surface[0]) + "," +
                    quote_literal(surface[1]) + "," +
                    quote_literal(surface[2]) + "," +
                    quote_literal(surface[3]) + "," +
                    quote_literal(surface[4]) + ")",
                "failed to insert analysis surface row");
  }
  const std::vector<std::vector<std::string>> projection_recipes = {
      {"scope_catalog", "5", "structural_node", "candidate_scopes",
       "folded", "device", "node_cost", "(none)",
       "rank and select reusable structural scopes before composing more "
       "specific projections",
       "SELECT node_id, parent_node_id, local_node_id, db_idx, device_id, "
       "view_name, display_order, path, symbol, label, node_type, "
       "repeat_count, occurrence_count, anchor_count, first_anchor_idx, "
       "last_anchor_idx, total_us, avg_total_us FROM "
       "traceloom_v_tree_node ORDER BY total_us DESC, db_idx, device_id, "
       "view_name, display_order;"},
      {"scope_occurrences", "10", "structural_node",
       "one_or_all_occurrences", "folded", "device",
       "occurrence_cost",
       ":node_id, :occurrence_idx (NULL selects all)",
       "inspect one realized occurrence or the full cost population of the "
       "same structural scope",
       "SELECT node_id, local_node_id, occurrence_idx, repeat_context, "
       "start_ns, end_ns, anchor_count, compute_us, comm_us, idle_us, "
       "total_us, self_us, aux_us FROM traceloom_tree_node_occurrence "
       "WHERE node_id = :node_id AND (:occurrence_idx IS NULL OR "
       "occurrence_idx = :occurrence_idx) ORDER BY occurrence_idx;"},
      {"scope_hierarchy", "20", "structural_node", "definition",
       "immediate_children", "device", "node_cost",
       ":node_id",
       "keep a selected scope folded while reading its ordered child "
       "patterns and costs",
       "SELECT parent_node_id, child_node_id, edge_order, local_node_id, "
       "label, node_type, repeat_count, occurrence_count, total_us, "
       "avg_total_us FROM traceloom_v_node_children WHERE parent_node_id = "
       ":node_id ORDER BY edge_order;"},
      {"scope_members", "30", "structural_node",
       "one_or_all_occurrences", "anchors_and_events", "device",
       "member_cost",
       ":node_id, :occurrence_idx (NULL selects all)",
       "expand a selected scope to ordered anchors and normalized event "
       "evidence",
       "SELECT na.node_id, na.occurrence_idx, na.anchor_order, "
       "na.coverage_kind, a.anchor_id, a.anchor_idx, a.symbol, e.event_id, "
       "e.stream_id, e.start_ns, e.end_ns, e.dur_us, e.role, "
       "e.semantic_role FROM traceloom_tree_node_anchor na JOIN "
       "traceloom_anchor a ON a.anchor_id = na.anchor_id LEFT JOIN "
       "traceloom_event e ON e.event_id = a.event_id WHERE na.node_id = "
       ":node_id AND (:occurrence_idx IS NULL OR na.occurrence_idx = "
       ":occurrence_idx) ORDER BY na.occurrence_idx, na.anchor_order;"},
      {"position_population", "50", "structural_node",
       "all_occurrences", "aligned_positions", "device",
       "position_cost_distribution", ":node_id",
       "compare corresponding ordered positions across every occurrence of "
       "the selected scope",
       "SELECT na.node_id, na.anchor_order, na.coverage_kind, a.symbol, "
       "count(DISTINCT na.occurrence_idx) AS occurrence_count, "
       "avg(na.total_us) AS avg_total_us, min(na.total_us) AS min_total_us, "
       "max(na.total_us) AS max_total_us, avg(na.compute_us) AS "
       "avg_compute_us, avg(na.comm_us) AS avg_comm_us, avg(na.idle_us) AS "
       "avg_idle_us, avg(na.aux_us) AS avg_aux_us FROM "
       "traceloom_tree_node_anchor na JOIN traceloom_anchor a ON "
       "a.anchor_id = na.anchor_id WHERE na.node_id = :node_id GROUP BY "
       "na.node_id, na.anchor_order, na.coverage_kind, a.symbol ORDER BY "
       "na.anchor_order, a.symbol;"},
      {"position_occurrences", "55", "structural_position",
       "one_or_all_occurrences", "aligned_position_members", "device",
       "position_occurrence_cost",
       ":node_id, :anchor_order, :occurrence_idx (NULL selects all)",
       "inspect the occurrence population behind one aligned structural "
       "position, then select an outlier without losing its coordinates",
       "SELECT na.node_id, na.occurrence_idx, na.anchor_order, "
       "na.coverage_kind, na.anchor_id, a.symbol, e.event_id, e.stream_id, "
       "e.start_ns, e.end_ns, e.dur_us, na.compute_us, na.comm_us, "
       "na.idle_us, na.total_us, na.aux_us FROM "
       "traceloom_tree_node_anchor na JOIN traceloom_anchor a ON "
       "a.anchor_id = na.anchor_id LEFT JOIN traceloom_event e ON "
       "e.event_id = a.event_id WHERE na.node_id = :node_id AND "
       "na.anchor_order = :anchor_order AND (:occurrence_idx IS NULL OR "
       "na.occurrence_idx = :occurrence_idx) ORDER BY na.occurrence_idx;"},
      {"scope_host_windows", "58", "structural_node",
       "one_or_all_occurrences", "anchor_pair_windows", "device_and_host",
       "typed_host_interval_support",
       ":node_id, :occurrence_idx (NULL selects all)",
       "project every adjacent-anchor position in a selected device scope "
       "to a supported or explicitly unsupported host interval",
       "SELECT node_id, occurrence_idx, anchor_order, coverage_kind, "
       "interval_id, left_anchor_id, right_anchor_id, right_anchor_symbol, "
       "support_state, provider, clock_domain, host_start_ns, host_end_ns, "
       "host_interval_us, left_endpoint_count, right_endpoint_count FROM "
       "traceloom_v_node_host_interval WHERE node_id = :node_id AND "
       "(:occurrence_idx IS NULL OR occurrence_idx = :occurrence_idx) "
       "ORDER BY occurrence_idx, anchor_order;"},
      {"scope_host_context", "60", "structural_node",
       "one_or_all_occurrences", "anchor_pair_windows", "host",
       "runtime_api_distribution",
       ":node_id, :occurrence_idx (NULL selects all)",
       "project the same device scope into typed host windows and compare "
       "profiler-visible runtime API distributions without hiding "
       "unsupported or empty windows",
       "SELECT i.node_id, i.occurrence_idx, i.anchor_order, "
       "i.right_anchor_symbol, i.coverage_kind, i.interval_id, "
       "i.support_state, i.host_interval_us, c.api_name, "
       "count(a.runtime_call_id) AS observed_calls, "
       "COALESCE(ROUND(sum((MIN(c.end_ns, i.host_end_ns) - "
       "MAX(c.start_ns, i.host_start_ns)) / 1000.0), 3), 0.0) AS "
       "scheduled_overlap_us FROM traceloom_v_node_host_interval i LEFT "
       "JOIN traceloom_anchor_host_activity a ON a.interval_id = "
       "i.interval_id LEFT JOIN traceloom_runtime_call c ON "
       "c.runtime_call_id = a.runtime_call_id WHERE i.node_id = :node_id AND "
       "(:occurrence_idx IS NULL OR i.occurrence_idx = :occurrence_idx) "
       "GROUP BY i.node_id, i.occurrence_idx, i.anchor_order, "
       "i.right_anchor_symbol, i.coverage_kind, i.interval_id, "
       "i.support_state, i.host_interval_us, c.api_name ORDER BY "
       "i.occurrence_idx, i.anchor_order, scheduled_overlap_us DESC;"},
      {"bubble_hotspots", "65", "structural_position",
       "all_occurrences", "position_summary", "device_and_host",
       "bubble_cost_and_host_support", "(none)",
       "rank recurrent uncovered-device positions while retaining typed "
       "host-observation coverage",
       "SELECT structural_position_id, right_local_node_id, "
       "right_node_path, right_node_symbol, bubble_occurrence_count, "
       "supported_host_occurrence_count, missing_endpoint_occurrence_count, "
       "nonmonotonic_occurrence_count, "
       "other_unsupported_occurrence_count, host_observation_coverage, "
       "total_bubble_us, avg_bubble_us, min_bubble_us, max_bubble_us FROM "
       "traceloom_v_structure_bubble_position ORDER BY total_bubble_us "
       "DESC, bubble_occurrence_count DESC;"},
      {"bubble_occurrences", "68", "structural_position",
       "one_or_all_occurrences", "bubble_occurrences", "device_and_host",
       "bubble_cost_and_host_support",
       ":structural_position_id, :bubble_id (NULL selects all)",
       "inspect one recurrent bubble population or select one occurrence "
       "for host-window and source drill-down",
       "SELECT structural_position_id, bubble_id, right_node_id, "
       "right_occurrence_idx, left_anchor_id, right_anchor_id, bubble_us, "
       "transition_total_us, host_interval_id, host_observation_status, "
       "host_interval_us, provider, host_start_ns, host_end_ns FROM "
       "traceloom_v_structure_bubble_occurrence WHERE "
       "structural_position_id = :structural_position_id AND (:bubble_id "
       "IS NULL OR bubble_id = :bubble_id) ORDER BY bubble_us DESC, "
       "right_occurrence_idx;"},
      {"bubble_host_context", "70", "structural_position",
       "all_occurrences", "bubble_population", "device_and_host",
       "bubble_and_runtime_api_distribution",
       ":structural_position_id",
       "compare recurrent uncovered-device cost with supported upstream "
       "host API-family observations without assigning a cause or hiding "
       "unsupported-only positions",
       "SELECT structural_position_id, bubble_occurrence_count, "
       "supported_host_occurrence_count, missing_endpoint_occurrence_count, "
       "nonmonotonic_occurrence_count, host_observation_coverage, "
       "total_bubble_us, avg_bubble_us, api_family, presence_count, "
       "avg_calls_per_observable_bubble, "
       "avg_scheduled_overlap_us_per_bubble FROM "
       "traceloom_v_structure_bubble_host_context WHERE "
       "structural_position_id = :structural_position_id ORDER BY "
       "total_bubble_us DESC, api_family;"},
      {"host_window_calls", "75", "host_interval",
       "one_interval", "literal_runtime_calls", "host",
       "runtime_call_observations", ":interval_id",
       "expand one typed host interval to literal observed runtime calls; "
       "an unsupported or empty interval remains a row",
       "SELECT i.interval_id, i.support_state, i.provider, i.clock_domain, "
       "i.host_start_ns, i.host_end_ns, c.runtime_call_id, c.api_name, "
       "c.api_type, c.start_ns AS observed_start_ns, c.end_ns AS "
       "observed_end_ns, c.dur_us AS observed_dur_us, "
       "ROUND((MIN(c.end_ns, i.host_end_ns) - MAX(c.start_ns, "
       "i.host_start_ns)) / 1000.0, 3) AS observed_overlap_us, "
       "CASE WHEN c.runtime_call_id IS NULL THEN NULL WHEN c.start_ns >= "
       "i.host_start_ns AND c.end_ns <= i.host_end_ns THEN 'contained' "
       "ELSE 'boundary_overlap' END AS "
       "interval_relation, a.observed_order FROM "
       "traceloom_v_anchor_host_interval i LEFT JOIN "
       "traceloom_anchor_host_activity a ON a.interval_id = i.interval_id "
       "LEFT JOIN traceloom_runtime_call c ON c.runtime_call_id = "
       "a.runtime_call_id WHERE i.interval_id = :interval_id ORDER BY "
       "a.observed_order;"},
      {"runtime_call_audit", "78", "runtime_call", "one_call",
       "source_rows", "profiler_evidence", "raw_observation",
       ":runtime_call_id",
       "audit a projected host runtime call through its embedded profiler "
       "source locator",
       "SELECT * FROM traceloom_v_runtime_call_source_locator WHERE "
       "runtime_call_id = :runtime_call_id;"},
      {"device_window_events", "80", "bounded_device_window",
       "one_window", "events", "device", "event_duration",
       ":db_idx, :device_id, :start_ns, :end_ns",
       "inspect normalized events overlapping a user-selected device "
       "window without promoting that window to a recovered pattern",
       "SELECT event_id, db_idx, device_id, step_idx, symbol, role, "
       "semantic_role, stream_id, start_ns, end_ns, dur_us, source_table, "
       "source_key FROM "
       "traceloom_event WHERE db_idx = :db_idx AND device_id = :device_id "
       "AND start_ns < :end_ns AND end_ns > :start_ns ORDER BY start_ns, "
       "end_ns, stream_id, event_id;"},
      {"event_reconciliation_audit", "85", "normalized_event",
       "one_event", "reconciliation_members", "device",
       "identity_contribution", ":event_id",
       "inspect whether an event stayed independent or contributed timing, "
       "symbol, or cost to one canonical structural anchor",
       "SELECT * FROM traceloom_v_event_reconciliation WHERE event_id = "
       ":event_id OR canonical_event_id = :event_id OR envelope_event_id = "
       ":event_id ORDER BY decision_id, member_order;"},
      {"event_audit", "90", "normalized_event", "one_event",
       "source_rows", "profiler_evidence", "raw_observation",
       ":event_id",
       "audit any projected event through its embedded profiler source "
       "locator",
       "SELECT * FROM traceloom_v_event_source_locator WHERE event_id = "
       ":event_id ORDER BY source_ordinal;"},
  };
  for (const auto& recipe : projection_recipes) {
    sqlite_exec(db,
                "INSERT INTO traceloom_projection_recipe VALUES(" +
                    quote_literal(recipe[0]) + "," + recipe[1] + "," +
                    quote_literal(recipe[2]) + "," +
                    quote_literal(recipe[3]) + "," +
                    quote_literal(recipe[4]) + "," +
                    quote_literal(recipe[5]) + "," +
                    quote_literal(recipe[6]) + "," +
                    quote_literal(recipe[7]) + "," +
                    quote_literal(recipe[8]) + "," +
                    quote_literal(recipe[9]) + ")",
                "failed to insert projection recipe row");
  }
  const std::vector<std::vector<std::string>> projection_parameters = {
      {"scope_occurrences", "10", "node_id", "TEXT", "0",
       "structural_node_id", "traceloom_v_tree_node", "node_id",
       "selected structural scope"},
      {"scope_occurrences", "20", "occurrence_idx", "INTEGER", "1",
       "structural_occurrence_index", "traceloom_tree_node_occurrence",
       "occurrence_idx",
       "NULL selects all occurrences; a value selects one execution"},
      {"scope_hierarchy", "10", "node_id", "TEXT", "0",
       "structural_node_id", "traceloom_v_tree_node", "node_id",
       "selected structural scope"},
      {"scope_members", "10", "node_id", "TEXT", "0",
       "structural_node_id", "traceloom_v_tree_node", "node_id",
       "selected structural scope"},
      {"scope_members", "20", "occurrence_idx", "INTEGER", "1",
       "structural_occurrence_index", "traceloom_tree_node_occurrence",
       "occurrence_idx",
       "NULL selects all occurrences; a value selects one execution"},
      {"position_population", "10", "node_id", "TEXT", "0",
       "structural_node_id", "traceloom_v_tree_node", "node_id",
       "selected structural scope whose ordered positions are aligned"},
      {"position_occurrences", "10", "node_id", "TEXT", "0",
       "structural_node_id", "traceloom_v_tree_node", "node_id",
       "selected structural scope"},
      {"position_occurrences", "20", "anchor_order", "INTEGER", "0",
       "structural_anchor_order", "traceloom_tree_node_anchor",
       "anchor_order", "selected aligned position inside the scope"},
      {"position_occurrences", "30", "occurrence_idx", "INTEGER", "1",
       "structural_occurrence_index", "traceloom_tree_node_anchor",
       "occurrence_idx",
       "NULL selects the position population; a value selects one member"},
      {"scope_host_windows", "10", "node_id", "TEXT", "0",
       "structural_node_id", "traceloom_v_tree_node", "node_id",
       "selected structural scope"},
      {"scope_host_windows", "20", "occurrence_idx", "INTEGER", "1",
       "structural_occurrence_index", "traceloom_tree_node_occurrence",
       "occurrence_idx",
       "NULL selects all occurrences; a value selects one execution"},
      {"scope_host_context", "10", "node_id", "TEXT", "0",
       "structural_node_id", "traceloom_v_tree_node", "node_id",
       "selected structural scope"},
      {"scope_host_context", "20", "occurrence_idx", "INTEGER", "1",
       "structural_occurrence_index", "traceloom_tree_node_occurrence",
       "occurrence_idx",
       "NULL selects all occurrences; a value selects one execution"},
      {"bubble_occurrences", "10", "structural_position_id", "TEXT",
       "0", "structural_position_id",
       "traceloom_v_structure_bubble_position", "structural_position_id",
       "selected recurrent bubble position"},
      {"bubble_occurrences", "20", "bubble_id", "TEXT", "1",
       "bubble_id", "traceloom_v_structure_bubble_occurrence",
       "bubble_id",
       "NULL selects all bubbles; a value selects one occurrence"},
      {"bubble_host_context", "10", "structural_position_id", "TEXT",
       "0", "structural_position_id",
       "traceloom_v_structure_bubble_position",
       "structural_position_id", "selected recurrent bubble position"},
      {"host_window_calls", "10", "interval_id", "TEXT", "0",
       "host_interval_id", "traceloom_v_anchor_host_interval",
       "interval_id", "selected typed host interval"},
      {"runtime_call_audit", "10", "runtime_call_id", "TEXT", "0",
       "runtime_call_id", "traceloom_runtime_call", "runtime_call_id",
       "selected observed host runtime call"},
      {"device_window_events", "10", "db_idx", "INTEGER", "0",
       "database_index", "traceloom_event", "db_idx",
       "source database coordinate"},
      {"device_window_events", "20", "device_id", "INTEGER", "0",
       "device_id", "traceloom_event", "device_id", "device coordinate"},
      {"device_window_events", "30", "start_ns", "INTEGER", "0",
       "time_start_ns", "traceloom_event", "start_ns",
       "inclusive window start"},
      {"device_window_events", "40", "end_ns", "INTEGER", "0",
       "time_end_ns", "traceloom_event", "end_ns",
       "exclusive window end"},
      {"event_reconciliation_audit", "10", "event_id", "TEXT", "0",
       "normalized_event_id", "traceloom_event", "event_id",
       "selected normalized event"},
      {"event_audit", "10", "event_id", "TEXT", "0",
       "normalized_event_id", "traceloom_event", "event_id",
       "selected normalized event"},
  };
  for (const auto& parameter : projection_parameters) {
    sqlite_exec(
        db,
        "INSERT INTO traceloom_projection_parameter VALUES(" +
            quote_literal(parameter[0]) + "," + parameter[1] + "," +
            quote_literal(parameter[2]) + "," +
            quote_literal(parameter[3]) + "," + parameter[4] + "," +
            quote_literal(parameter[5]) + "," +
            quote_literal(parameter[6]) + "," +
            quote_literal(parameter[7]) + "," +
            quote_literal(parameter[8]) + ")",
        "failed to insert projection parameter row");
  }
  const std::vector<std::vector<std::string>> projection_coordinates = {
      {"scope_catalog", "10", "node_id", "structural_node_id",
       "selected structural scope"},
      {"scope_catalog", "20", "db_idx", "database_index",
       "source database coordinate"},
      {"scope_catalog", "30", "device_id", "device_id",
       "device coordinate"},
      {"scope_catalog", "40", "view_name", "structural_view_name",
       "structural projection identity"},
      {"scope_catalog", "50", "parent_node_id", "structural_node_id",
       "immediate parent reusable as another structural scope"},
      {"scope_occurrences", "10", "node_id", "structural_node_id",
       "selected structural scope"},
      {"scope_occurrences", "20", "occurrence_idx",
       "structural_occurrence_index", "selected realized occurrence"},
      {"scope_hierarchy", "10", "parent_node_id", "structural_node_id",
       "current folded scope"},
      {"scope_hierarchy", "20", "child_node_id", "structural_node_id",
       "ordered child usable as a new scope"},
      {"scope_members", "10", "node_id", "structural_node_id",
       "selected structural scope"},
      {"scope_members", "20", "occurrence_idx",
       "structural_occurrence_index", "selected realized occurrence"},
      {"scope_members", "30", "anchor_order", "structural_anchor_order",
       "ordered position inside the selected scope"},
      {"scope_members", "40", "anchor_id", "anchor_id",
       "selected structural anchor"},
      {"scope_members", "50", "event_id", "normalized_event_id",
       "normalized event available for source audit"},
      {"position_population", "10", "node_id", "structural_node_id",
       "selected structural scope"},
      {"position_population", "20", "anchor_order",
       "structural_anchor_order", "aligned position inside the scope"},
      {"position_occurrences", "10", "node_id", "structural_node_id",
       "selected structural scope"},
      {"position_occurrences", "20", "occurrence_idx",
       "structural_occurrence_index", "selected position occurrence"},
      {"position_occurrences", "30", "anchor_order",
       "structural_anchor_order", "aligned position inside the scope"},
      {"position_occurrences", "40", "anchor_id", "anchor_id",
       "selected structural anchor"},
      {"position_occurrences", "50", "event_id", "normalized_event_id",
       "normalized event available for source audit"},
      {"scope_host_windows", "10", "node_id", "structural_node_id",
       "selected structural scope"},
      {"scope_host_windows", "20", "occurrence_idx",
       "structural_occurrence_index", "selected realized occurrence"},
      {"scope_host_windows", "30", "anchor_order",
       "structural_anchor_order", "device position delimiting the window"},
      {"scope_host_windows", "40", "interval_id", "host_interval_id",
       "typed host interval available for call drill-down"},
      {"scope_host_windows", "50", "left_anchor_id", "anchor_id",
       "left device endpoint"},
      {"scope_host_windows", "60", "right_anchor_id", "anchor_id",
       "right device endpoint"},
      {"scope_host_context", "10", "node_id", "structural_node_id",
       "selected structural scope"},
      {"scope_host_context", "20", "occurrence_idx",
       "structural_occurrence_index", "selected realized occurrence"},
      {"scope_host_context", "30", "anchor_order",
       "structural_anchor_order", "device position delimiting the window"},
      {"scope_host_context", "40", "interval_id", "host_interval_id",
       "typed host interval available for literal-call drill-down"},
      {"bubble_hotspots", "10", "structural_position_id",
       "structural_position_id", "recurrent bubble position"},
      {"bubble_occurrences", "10", "structural_position_id",
       "structural_position_id", "selected recurrent bubble position"},
      {"bubble_occurrences", "20", "bubble_id", "bubble_id",
       "selected bubble occurrence"},
      {"bubble_occurrences", "30", "right_node_id",
       "structural_node_id", "right-hand structural node"},
      {"bubble_occurrences", "40", "left_anchor_id", "anchor_id",
       "left device endpoint"},
      {"bubble_occurrences", "50", "right_anchor_id", "anchor_id",
       "right device endpoint"},
      {"bubble_occurrences", "60", "host_interval_id",
       "host_interval_id", "typed upstream host interval"},
      {"bubble_host_context", "10", "structural_position_id",
       "structural_position_id", "selected recurrent bubble position"},
      {"host_window_calls", "10", "interval_id", "host_interval_id",
       "selected typed host interval"},
      {"host_window_calls", "20", "runtime_call_id", "runtime_call_id",
       "observed runtime call available for source audit"},
      {"runtime_call_audit", "10", "runtime_call_id", "runtime_call_id",
       "selected observed host runtime call"},
      {"device_window_events", "10", "event_id", "normalized_event_id",
       "normalized event available for source audit"},
      {"device_window_events", "20", "db_idx", "database_index",
       "source database coordinate"},
      {"device_window_events", "30", "device_id", "device_id",
       "device coordinate"},
      {"device_window_events", "40", "start_ns", "time_start_ns",
       "event start usable as a bounded window start"},
      {"device_window_events", "50", "end_ns", "time_end_ns",
       "event end usable as a bounded window end"},
      {"event_reconciliation_audit", "10", "event_id",
       "normalized_event_id", "observed reconciliation member"},
      {"event_reconciliation_audit", "20", "canonical_event_id",
       "normalized_event_id", "canonical event when reconciliation is supported"},
      {"event_reconciliation_audit", "30", "envelope_event_id",
       "normalized_event_id", "timing-envelope event when present"},
      {"event_audit", "10", "event_id", "normalized_event_id",
       "selected normalized event"},
  };
  for (const auto& coordinate : projection_coordinates) {
    sqlite_exec(
        db,
        "INSERT INTO traceloom_projection_coordinate VALUES(" +
            quote_literal(coordinate[0]) + "," + coordinate[1] + "," +
            quote_literal(coordinate[2]) + "," +
            quote_literal(coordinate[3]) + "," +
            quote_literal(coordinate[4]) + ")",
        "failed to insert projection coordinate row");
  }
  sqlite_exec(db,
              "INSERT INTO traceloom_metadata(key, value) VALUES"
              "('analytical_projection_contract', "
              "'scope_population_resolution_domain_lens_coordinates_v2'),"
              "('raw_source_database_count', " +
                  quote_literal(std::to_string(packaging.sources.size())) +
                  "),('raw_table_count', " +
                  quote_literal(std::to_string(packaging.tables.size())) +
                  ")",
              "failed to add augmented catalog metadata");
}

}  // namespace traceloom::compat::detail
#endif
