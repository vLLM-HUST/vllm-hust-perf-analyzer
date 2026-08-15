#pragma once

#include "traceloom/compat/schema.h"

#include <string>
#include <vector>

namespace traceloom::testing::sidecar_writer_test {

struct ColumnInfo {
  std::string name;
  std::string type;
  bool not_null = false;
};

struct StoredAnchorCostRow {
  int anchor_idx = 0;
  std::string symbol;
  std::string anchor_kind;
  double total_us = 0.0;
  double self_us = 0.0;
  double aux_us = 0.0;
  double graph_child_us = 0.0;
  double residual_us = 0.0;
  int raw_child_task_count = 0;
  std::string top_ops;
  std::string diagnostic_flags;
};

struct StoredMetadataRow {
  std::string key;
  std::string value;
};

std::string temp_db_path();
void execute_sql(const std::string& path, const std::string& sql);
std::vector<ColumnInfo> load_columns(const std::string& path,
                                     const std::string& table_name);
std::vector<std::string> load_sqlite_master_names(const std::string& path,
                                                  const std::string& type);
bool has_column(const std::vector<ColumnInfo>& columns,
                const std::string& name);
int run_scalar_int(const std::string& path, const std::string& sql);
std::string run_scalar_text(const std::string& path, const std::string& sql);
std::vector<StoredAnchorCostRow> load_anchor_cost_rows(
    const std::string& path);
std::vector<StoredMetadataRow> load_metadata_rows(const std::string& path);
void require_columns_match_schema(
    const std::string& db_path,
    const traceloom::compat::CompatTableSchema& schema);

void run_graph_projection_tests(
    const std::string& db_path,
    const std::vector<std::string>& expected_tables,
    const std::vector<traceloom::compat::CompatTableSchema>& table_schemas);

}  // namespace traceloom::testing::sidecar_writer_test
