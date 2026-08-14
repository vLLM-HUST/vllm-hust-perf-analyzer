#include "traceloom/compat/schema.h"

namespace traceloom::compat {

const CompatTableSchema& anchor_cost_breakdown_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_anchor_cost_breakdown",
      {
          {"anchor_idx", CompatColumnType::kInteger, false},
          {"symbol", CompatColumnType::kText, false},
          {"anchor_kind", CompatColumnType::kText, false},
          {"total_us", CompatColumnType::kReal, false},
          {"self_us", CompatColumnType::kReal, false},
          {"aux_us", CompatColumnType::kReal, false},
          {"graph_child_us", CompatColumnType::kReal, false},
          {"residual_us", CompatColumnType::kReal, false},
          {"raw_child_task_count", CompatColumnType::kInteger, false},
          {"top_ops", CompatColumnType::kText, false},
          {"diagnostic_flags", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& graph_launch_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_graph_launch",
      {
          {"launch_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"graph_provider", CompatColumnType::kText, false},
          {"graph_event_id", CompatColumnType::kText, false},
          {"anchor_id", CompatColumnType::kText, true},
          {"replay_unit_id", CompatColumnType::kInteger, false},
          {"graph_template_id", CompatColumnType::kInteger, false},
          {"graph_launch_occurrence_id", CompatColumnType::kInteger, false},
          {"replay_body_template_id", CompatColumnType::kInteger, false},
          {"body_id", CompatColumnType::kInteger, false},
          {"member_order", CompatColumnType::kInteger, false},
          {"slot_order", CompatColumnType::kInteger, true},
          {"correlation_id", CompatColumnType::kText, true},
          {"match_policy", CompatColumnType::kText, true},
          {"association_policy", CompatColumnType::kText, true},
          {"start_ns", CompatColumnType::kInteger, true},
          {"end_ns", CompatColumnType::kInteger, true},
          {"dur_us", CompatColumnType::kReal, true},
          {"evidence_level", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& graph_body_member_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_graph_body_member",
      {
          {"member_id", CompatColumnType::kText, false},
          {"launch_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"graph_provider", CompatColumnType::kText, false},
          {"graph_event_id", CompatColumnType::kText, false},
          {"replay_unit_id", CompatColumnType::kInteger, false},
          {"graph_template_id", CompatColumnType::kInteger, false},
          {"graph_launch_occurrence_id", CompatColumnType::kInteger, false},
          {"body_id", CompatColumnType::kInteger, false},
          {"replay_body_template_id", CompatColumnType::kInteger, false},
          {"member_order", CompatColumnType::kInteger, false},
          {"slot_order", CompatColumnType::kInteger, true},
          {"lane_ordinal", CompatColumnType::kInteger, false},
          {"task_ordinal", CompatColumnType::kInteger, false},
          {"kind", CompatColumnType::kText, false},
          {"event_id", CompatColumnType::kText, false},
          {"task_id", CompatColumnType::kInteger, false},
          {"source_table", CompatColumnType::kText, true},
          {"source_row_id", CompatColumnType::kInteger, true},
          {"raw_task_id", CompatColumnType::kInteger, true},
          {"start_ns", CompatColumnType::kInteger, true},
          {"end_ns", CompatColumnType::kInteger, true},
          {"dur_us", CompatColumnType::kReal, true},
          {"correlation_id", CompatColumnType::kText, true},
          {"graph_node_id", CompatColumnType::kInteger, true},
          {"original_graph_node_id", CompatColumnType::kInteger, true},
          {"match_policy", CompatColumnType::kText, true},
          {"association_policy", CompatColumnType::kText, true},
          {"evidence_level", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& replay_cost_unit_table_schema() {
  static const CompatTableSchema s{
      "traceloom_replay_cost_unit",
      {{"cost_unit_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"replay_unit_id", CompatColumnType::kInteger, false},
       {"graph_template_id", CompatColumnType::kInteger, false},
       {"launch_member_count", CompatColumnType::kInteger, false},
       {"resolved_launch_count", CompatColumnType::kInteger, false},
       {"support_status", CompatColumnType::kText, false},
       {"reason_codes", CompatColumnType::kText, false}}};
  return s;
}
const CompatTableSchema& replay_cost_launch_table_schema() {
  static const CompatTableSchema s{
      "traceloom_replay_cost_launch",
      {{"launch_id", CompatColumnType::kText, false},
       {"cost_unit_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"member_order", CompatColumnType::kInteger, false},
       {"graph_launch_occurrence_id", CompatColumnType::kInteger, false},
       {"composition_slot_id", CompatColumnType::kInteger, false},
       {"slot_role", CompatColumnType::kText, false},
       {"slot_order", CompatColumnType::kInteger, false},
       {"replay_body_template_id", CompatColumnType::kInteger, false},
       {"body_id", CompatColumnType::kInteger, false},
       {"support_status", CompatColumnType::kText, false},
       {"reason_code", CompatColumnType::kText, false},
       {"member_count", CompatColumnType::kInteger, false},
       {"task_sum_ns", CompatColumnType::kInteger, false},
       {"busy_union_ns", CompatColumnType::kInteger, false},
       {"envelope_ns", CompatColumnType::kInteger, false},
       {"compute_ns", CompatColumnType::kInteger, false},
       {"communication_ns", CompatColumnType::kInteger, false},
       {"data_move_ns", CompatColumnType::kInteger, false},
       {"replay_unit_id", CompatColumnType::kInteger, false},
       {"graph_template_id", CompatColumnType::kInteger, false}}};
  return s;
}
const CompatTableSchema& replay_cost_stream_table_schema() {
  static const CompatTableSchema s{
      "traceloom_replay_cost_stream",
      {{"launch_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"stream_id", CompatColumnType::kInteger, false},
       {"lane_ordinal", CompatColumnType::kInteger, false},
       {"lane_consistent", CompatColumnType::kInteger, false},
       {"member_count", CompatColumnType::kInteger, false},
       {"task_sum_ns", CompatColumnType::kInteger, false},
       {"busy_union_ns", CompatColumnType::kInteger, false},
       {"compute_ns", CompatColumnType::kInteger, false},
       {"communication_ns", CompatColumnType::kInteger, false},
       {"data_move_ns", CompatColumnType::kInteger, false}}};
  return s;
}
const CompatTableSchema& replay_cost_member_table_schema() {
  static const CompatTableSchema s{
      "traceloom_replay_cost_member",
      {{"member_id", CompatColumnType::kText, false},
       {"launch_id", CompatColumnType::kText, false},
       {"cost_unit_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"composition_slot_id", CompatColumnType::kInteger, false},
       {"slot_role", CompatColumnType::kText, false},
       {"slot_order", CompatColumnType::kInteger, false},
       {"replay_body_template_id", CompatColumnType::kInteger, false},
       {"body_id", CompatColumnType::kInteger, false},
       {"stream_id", CompatColumnType::kInteger, false},
       {"lane_ordinal", CompatColumnType::kInteger, false},
       {"task_ordinal", CompatColumnType::kInteger, false},
       {"kind", CompatColumnType::kText, false},
       {"event_id", CompatColumnType::kText, false},
       {"identity", CompatColumnType::kText, false},
       {"raw_task_id", CompatColumnType::kInteger, false},
       {"start_ns", CompatColumnType::kInteger, false},
       {"end_ns", CompatColumnType::kInteger, false},
       {"duration_ns", CompatColumnType::kInteger, false},
       {"relative_start_ns", CompatColumnType::kInteger, false},
       {"relative_end_ns", CompatColumnType::kInteger, false},
       {"scheduled_work_share_ppm", CompatColumnType::kInteger, false},
       {"scheduled_work_share_supported", CompatColumnType::kInteger, false},
       {"scheduled_work_denominator_body_task_sum_ns",
        CompatColumnType::kInteger, false}}};
  return s;
}
const CompatTableSchema& replay_cost_aggregate_table_schema() {
  static const CompatTableSchema s{
      "traceloom_replay_cost_aggregate",
      {{"aggregate_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"graph_template_id", CompatColumnType::kInteger, false},
       {"slot_role", CompatColumnType::kText, false},
       {"aggregation_scope", CompatColumnType::kText, false},
       {"replay_body_template_id", CompatColumnType::kInteger, false},
       {"stream_id", CompatColumnType::kInteger, false},
       {"task_ordinal", CompatColumnType::kInteger, false},
       {"identity", CompatColumnType::kText, false},
       {"kind", CompatColumnType::kText, false},
       {"member_occurrence_count", CompatColumnType::kInteger, false},
       {"replay_unit_count", CompatColumnType::kInteger, false},
       {"launch_member_count", CompatColumnType::kInteger, false},
       {"kind_consistent", CompatColumnType::kInteger, false},
       {"lane_consistent", CompatColumnType::kInteger, false},
       {"distribution_supported", CompatColumnType::kInteger, false},
       {"duration_p25_ns", CompatColumnType::kInteger, false},
       {"duration_median_ns", CompatColumnType::kInteger, false},
       {"duration_p75_ns", CompatColumnType::kInteger, false},
       {"scheduled_work_share_ppm", CompatColumnType::kInteger, false},
       {"scheduled_work_share_supported", CompatColumnType::kInteger, false},
       {"scheduled_work_denominator_body_task_sum_ns",
        CompatColumnType::kInteger, false}}};
  return s;
}
const CompatTableSchema& replay_cost_aggregate_member_table_schema() {
  static const CompatTableSchema s{
      "traceloom_replay_cost_aggregate_member",
      {{"aggregate_id", CompatColumnType::kText, false},
       {"member_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"contributor_order", CompatColumnType::kInteger, false}}};
  return s;
}
const CompatTableSchema& replay_cost_issue_table_schema() {
  static const CompatTableSchema s{
      "traceloom_replay_cost_issue",
      {{"issue_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"code", CompatColumnType::kText, false},
       {"replay_unit_id", CompatColumnType::kInteger, false},
       {"launch_id", CompatColumnType::kText, true},
       {"detail", CompatColumnType::kText, false}}};
  return s;
}

}  // namespace traceloom::compat
