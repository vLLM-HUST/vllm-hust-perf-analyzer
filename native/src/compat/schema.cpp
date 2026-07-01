#include "traceloom/compat/schema.h"

#include <cctype>
#include <stdexcept>

namespace traceloom::compat {

namespace {

bool is_safe_identifier(const std::string& value) {
  if (value.empty()) {
    return false;
  }
  const unsigned char first = static_cast<unsigned char>(value[0]);
  if (!(std::isalpha(first) || value[0] == '_')) {
    return false;
  }
  for (const char ch : value) {
    const unsigned char current = static_cast<unsigned char>(ch);
    if (!(std::isalnum(current) || ch == '_')) {
      return false;
    }
  }
  return true;
}

void require_safe_identifier(const std::string& value, const char* what) {
  if (!is_safe_identifier(value)) {
    throw std::invalid_argument(std::string("invalid compatibility ") + what +
                                " identifier: " + value);
  }
}

}  // namespace

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

const char* sqlite_column_type_name(CompatColumnType type) {
  switch (type) {
    case CompatColumnType::kInteger:
      return "INTEGER";
    case CompatColumnType::kReal:
      return "REAL";
    case CompatColumnType::kText:
      return "TEXT";
  }
  return "TEXT";
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

std::vector<CompatTableSchema> compatibility_table_schemas() {
  return {anchor_cost_breakdown_table_schema()};
}

std::vector<std::string> column_names(const CompatTableSchema& schema) {
  std::vector<std::string> names;
  names.reserve(schema.columns.size());
  for (const CompatColumnSchema& column : schema.columns) {
    names.push_back(column.name);
  }
  return names;
}

std::string sqlite_create_table_sql(const CompatTableSchema& schema) {
  require_safe_identifier(schema.name, "table");
  if (schema.columns.empty()) {
    throw std::invalid_argument("compatibility table schema has no columns");
  }

  std::string sql = "CREATE TABLE IF NOT EXISTS ";
  sql += schema.name;
  sql += " (";
  for (std::size_t index = 0; index < schema.columns.size(); ++index) {
    const CompatColumnSchema& column = schema.columns[index];
    require_safe_identifier(column.name, "column");
    if (index != 0) {
      sql += ", ";
    }
    sql += column.name;
    sql += " ";
    sql += sqlite_column_type_name(column.type);
    if (!column.nullable) {
      sql += " NOT NULL";
    }
  }
  sql += ")";
  return sql;
}

}  // namespace traceloom::compat
