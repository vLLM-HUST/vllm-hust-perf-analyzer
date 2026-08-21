#include "traceloom/compat/schema.h"

namespace traceloom::compat {

const CompatTableSchema& replay_body_pattern_run_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_replay_body_pattern_run",
      {{"run_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"support_status", CompatColumnType::kText, false},
       {"reason_codes", CompatColumnType::kText, false},
       {"grammar_mode", CompatColumnType::kText, false},
       {"source_relation", CompatColumnType::kText, false},
       {"position_count", CompatColumnType::kInteger, false},
       {"supported_domain_count", CompatColumnType::kInteger, false},
       {"rejected_domain_count", CompatColumnType::kInteger, false}}};
  return schema;
}

const CompatTableSchema& replay_body_pattern_domain_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_replay_body_pattern_domain",
      {{"domain_id", CompatColumnType::kText, false},
       {"run_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"graph_template_id", CompatColumnType::kInteger, false},
       {"slot_role", CompatColumnType::kText, false},
       {"aggregation_scope", CompatColumnType::kText, false},
       {"replay_body_template_id", CompatColumnType::kInteger, false},
       {"stream_id", CompatColumnType::kInteger, false},
       {"support_status", CompatColumnType::kText, false},
       {"reason_code", CompatColumnType::kText, false},
       {"position_count", CompatColumnType::kInteger, false},
       {"grammar_mode", CompatColumnType::kText, false},
       {"grammar_stop_reason", CompatColumnType::kText, false},
       {"grammar_live_node_count", CompatColumnType::kInteger, false},
       {"grammar_macro_def_count", CompatColumnType::kInteger, false},
       {"grammar_description_element_count", CompatColumnType::kInteger,
        false},
       {"grammar_description_ratio", CompatColumnType::kReal, false},
       {"grammar_step_count", CompatColumnType::kInteger, false},
       {"pattern_definition_count", CompatColumnType::kInteger, false},
       {"pattern_occurrence_count", CompatColumnType::kInteger, false}}};
  return schema;
}

const CompatTableSchema& replay_body_pattern_definition_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_replay_body_pattern_definition",
      {{"pattern_id", CompatColumnType::kText, false},
       {"domain_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"local_pattern_id", CompatColumnType::kText, false},
       {"node_kind", CompatColumnType::kText, false},
       {"label", CompatColumnType::kText, false},
       {"category", CompatColumnType::kText, false},
       {"repeat_count", CompatColumnType::kInteger, true},
       {"display_depth", CompatColumnType::kInteger, false},
       {"loop_depth", CompatColumnType::kInteger, false},
       {"occurrence_count", CompatColumnType::kInteger, false},
       {"positions_per_occurrence", CompatColumnType::kInteger, false},
       {"visibility_reason", CompatColumnType::kText, false}}};
  return schema;
}

const CompatTableSchema& replay_body_pattern_occurrence_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_replay_body_pattern_occurrence",
      {{"occurrence_id", CompatColumnType::kText, false},
       {"pattern_id", CompatColumnType::kText, false},
       {"domain_id", CompatColumnType::kText, false},
       {"parent_occurrence_id", CompatColumnType::kText, true},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"occurrence_index", CompatColumnType::kInteger, false},
       {"edge_order", CompatColumnType::kInteger, false},
       {"repeat_iteration", CompatColumnType::kInteger, false},
       {"position_start", CompatColumnType::kInteger, false},
       {"position_end_exclusive", CompatColumnType::kInteger, false},
       {"position_count", CompatColumnType::kInteger, false},
       {"duration_p25_sum_ns", CompatColumnType::kInteger, false},
       {"duration_median_sum_ns", CompatColumnType::kInteger, false},
       {"duration_p75_sum_ns", CompatColumnType::kInteger, false},
       {"scheduled_work_share_ppm_sum", CompatColumnType::kInteger, false},
       {"scheduled_work_share_supported", CompatColumnType::kInteger,
        false}}};
  return schema;
}

const CompatTableSchema& replay_body_position_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_replay_body_position",
      {{"position_id", CompatColumnType::kText, false},
       {"domain_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"position_ordinal", CompatColumnType::kInteger, false},
       {"aggregate_id", CompatColumnType::kText, false},
       {"identity", CompatColumnType::kText, false},
       {"kind", CompatColumnType::kText, false},
       {"member_occurrence_count", CompatColumnType::kInteger, false},
       {"replay_unit_count", CompatColumnType::kInteger, false},
       {"launch_member_count", CompatColumnType::kInteger, false},
       {"duration_p25_ns", CompatColumnType::kInteger, false},
       {"duration_median_ns", CompatColumnType::kInteger, false},
       {"duration_p75_ns", CompatColumnType::kInteger, false},
       {"scheduled_work_share_ppm", CompatColumnType::kInteger, false},
       {"scheduled_work_share_supported", CompatColumnType::kInteger,
        false}}};
  return schema;
}

const CompatTableSchema& replay_body_position_refinement_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_replay_body_position_refinement",
      {{"parent_position_id", CompatColumnType::kText, false},
       {"slot_ordinal", CompatColumnType::kInteger, false},
       {"child_position_id", CompatColumnType::kText, false},
       {"domain_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false}}};
  return schema;
}

const CompatTableSchema& replay_body_position_member_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_replay_body_position_member",
      {{"parent_position_id", CompatColumnType::kText, false},
       {"parent_occurrence_id", CompatColumnType::kText, false},
       {"slot_ordinal", CompatColumnType::kInteger, false},
       {"member_order", CompatColumnType::kInteger, false},
       {"member_kind", CompatColumnType::kText, false},
       {"child_position_id", CompatColumnType::kText, true},
       {"child_occurrence_id", CompatColumnType::kText, true},
       {"terminal_position_id", CompatColumnType::kText, true},
       {"terminal_position_ordinal", CompatColumnType::kInteger, true},
       {"terminal_aggregate_id", CompatColumnType::kText, true},
       {"domain_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false}}};
  return schema;
}

const CompatTableSchema& replay_body_pattern_issue_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_replay_body_pattern_issue",
      {{"issue_id", CompatColumnType::kText, false},
       {"run_id", CompatColumnType::kText, false},
       {"domain_id", CompatColumnType::kText, true},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, true},
       {"code", CompatColumnType::kText, false},
       {"detail", CompatColumnType::kText, false}}};
  return schema;
}

}  // namespace traceloom::compat
