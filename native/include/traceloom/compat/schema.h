#pragma once

#include <string>
#include <vector>

namespace traceloom::compat {

enum class CompatColumnType {
  kInteger,
  kReal,
  kText,
};

struct CompatColumnSchema {
  std::string name;
  CompatColumnType type = CompatColumnType::kText;
  bool nullable = false;
};

struct CompatTableSchema {
  std::string name;
  std::vector<CompatColumnSchema> columns;
};

const char* compat_column_type_name(CompatColumnType type);
const char* sqlite_column_type_name(CompatColumnType type);

const CompatTableSchema& event_table_schema();
const CompatTableSchema& anchor_table_schema();
const CompatTableSchema& anchor_aux_slot_table_schema();
const CompatTableSchema& aux_link_table_schema();
const CompatTableSchema& cuda_graph_replay_table_schema();
const CompatTableSchema& cuda_graph_envelope_table_schema();
const CompatTableSchema& viz_node_table_schema();
const CompatTableSchema& viz_edge_table_schema();
const CompatTableSchema& viz_node_anchor_table_schema();
const CompatTableSchema& anchor_cost_breakdown_table_schema();

std::vector<CompatTableSchema> compatibility_table_schemas();

std::vector<std::string> column_names(const CompatTableSchema& schema);
std::string sqlite_create_table_sql(const CompatTableSchema& schema);

}  // namespace traceloom::compat
