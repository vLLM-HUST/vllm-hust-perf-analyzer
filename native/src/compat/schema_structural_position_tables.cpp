#include "traceloom/compat/schema.h"

namespace traceloom::compat {

const CompatTableSchema& position_refinement_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_position_refinement",
      {{"parent_position_id", CompatColumnType::kText, false},
       {"slot_ordinal", CompatColumnType::kInteger, false},
       {"child_position_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"tree_id", CompatColumnType::kText, false},
       {"view_name", CompatColumnType::kText, false}}};
  return schema;
}

const CompatTableSchema& position_occurrence_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_position_occurrence",
      {{"occurrence_id", CompatColumnType::kText, false},
       {"position_id", CompatColumnType::kText, false},
       {"parent_occurrence_id", CompatColumnType::kText, true},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"tree_id", CompatColumnType::kText, false},
       {"view_name", CompatColumnType::kText, false},
       {"occurrence_idx", CompatColumnType::kInteger, false},
       {"repeat_iteration", CompatColumnType::kInteger, false},
       {"token_start_ordinal", CompatColumnType::kInteger, false},
       {"token_end_exclusive", CompatColumnType::kInteger, false},
       {"rooted_position_path", CompatColumnType::kText, false},
       {"occurrence_path", CompatColumnType::kText, false}}};
  return schema;
}

const CompatTableSchema& position_member_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_position_member",
      {{"parent_position_id", CompatColumnType::kText, false},
       {"parent_occurrence_id", CompatColumnType::kText, false},
       {"slot_ordinal", CompatColumnType::kInteger, false},
       {"member_order", CompatColumnType::kInteger, false},
       {"member_kind", CompatColumnType::kText, false},
       {"child_position_id", CompatColumnType::kText, true},
       {"child_occurrence_id", CompatColumnType::kText, true},
       {"terminal_token_ordinal", CompatColumnType::kInteger, true},
       {"terminal_anchor_id", CompatColumnType::kText, true},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"tree_id", CompatColumnType::kText, false},
       {"view_name", CompatColumnType::kText, false},
       {"member_path", CompatColumnType::kText, false}}};
  return schema;
}

}  // namespace traceloom::compat
