#include "traceloom/compat/schema.h"

namespace traceloom::compat {

const char* compat_column_type_name(CompatColumnType type) {
  switch (type) {
    case CompatColumnType::kInteger:
      return "integer";
    case CompatColumnType::kReal:
      return "real";
    case CompatColumnType::kText:
      return "text";
  }
  return "text";
}

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

std::vector<std::string> column_names(const CompatTableSchema& schema) {
  std::vector<std::string> names;
  names.reserve(schema.columns.size());
  for (const CompatColumnSchema& column : schema.columns) {
    names.push_back(column.name);
  }
  return names;
}

}  // namespace traceloom::compat
