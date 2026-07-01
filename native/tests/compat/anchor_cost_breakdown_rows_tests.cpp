#include "traceloom/compat/anchor_cost_breakdown_rows.h"
#include "traceloom/testing/test_util.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

bool near(double lhs, double rhs) {
  return std::fabs(lhs - rhs) < 0.000001;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  AnchorInternalCostBreakdown breakdown;

  AnchorInternalCostBreakdownRow graph_row;
  graph_row.anchor_idx = 2;
  graph_row.symbol = "ACLL";
  graph_row.anchor_kind = ReportAnchorKind::kGraphL;
  graph_row.total_ns = 123456;
  graph_row.self_ns = 1000;
  graph_row.aux_ns = 2000;
  graph_row.graph_child_ns = 120000;
  graph_row.residual_ns = 456;
  graph_row.raw_child_task_count = 20;
  graph_row.top_ops = "MatMul:16";
  graph_row.diagnostic_flags = "partial_overlap";
  breakdown.rows.push_back(graph_row);

  AnchorInternalCostBreakdownRow exec_row;
  exec_row.anchor_idx = 3;
  exec_row.symbol = "Kernel";
  exec_row.anchor_kind = ReportAnchorKind::kExec;
  exec_row.total_ns = 7000;
  exec_row.self_ns = 7000;
  breakdown.rows.push_back(exec_row);

  const std::vector<compat::AnchorCostBreakdownSqlRow> rows =
      compat::build_anchor_cost_breakdown_sql_rows(breakdown);

  require(rows.size() == 2);
  require(rows[0].anchor_idx == 2);
  require(rows[0].symbol == "ACLL");
  require(rows[0].anchor_kind == "graph_l");
  require(near(rows[0].total_us, 123.456));
  require(near(rows[0].self_us, 1.0));
  require(near(rows[0].aux_us, 2.0));
  require(near(rows[0].graph_child_us, 120.0));
  require(near(rows[0].residual_us, 0.456));
  require(rows[0].raw_child_task_count == 20);
  require(rows[0].top_ops == "MatMul:16");
  require(rows[0].diagnostic_flags == "partial_overlap");

  require(rows[1].anchor_idx == 3);
  require(rows[1].anchor_kind == "exec");
  require(near(rows[1].total_us, 7.0));
  require(near(rows[1].self_us, 7.0));

  const compat::CompatTableSchema& schema =
      compat::anchor_cost_breakdown_sql_row_schema();
  require(schema.name == "traceloom_anchor_cost_breakdown");
  const std::vector<std::string> expected_columns{
      "anchor_idx",
      "symbol",
      "anchor_kind",
      "total_us",
      "self_us",
      "aux_us",
      "graph_child_us",
      "residual_us",
      "raw_child_task_count",
      "top_ops",
      "diagnostic_flags",
  };
  require(compat::column_names(schema) == expected_columns);
  require(schema.columns[0].type == compat::CompatColumnType::kInteger);
  require(schema.columns[3].type == compat::CompatColumnType::kReal);
  require(schema.columns[10].type == compat::CompatColumnType::kText);
  require(compat::compat_column_type_name(compat::CompatColumnType::kInteger) ==
          std::string("integer"));
  require(compat::sqlite_column_type_name(compat::CompatColumnType::kReal) ==
          std::string("REAL"));
  const std::vector<compat::CompatTableSchema> catalog =
      compat::compatibility_table_schemas();
  bool found_anchor_cost_schema = false;
  for (const compat::CompatTableSchema& catalog_schema : catalog) {
    if (catalog_schema.name == schema.name) {
      found_anchor_cost_schema = true;
      require(compat::column_names(catalog_schema) == expected_columns);
    }
  }
  require(found_anchor_cost_schema);

  const std::string create_sql = compat::sqlite_create_table_sql(schema);
  require(create_sql.find(
              "CREATE TABLE IF NOT EXISTS traceloom_anchor_cost_breakdown") ==
          0);
  require(create_sql.find("anchor_idx INTEGER NOT NULL") !=
          std::string::npos);
  require(create_sql.find("total_us REAL NOT NULL") != std::string::npos);
  require(create_sql.find("diagnostic_flags TEXT NOT NULL") !=
          std::string::npos);

  bool rejected_bad_table = false;
  try {
    (void)compat::sqlite_create_table_sql(
        compat::CompatTableSchema{"bad-table", schema.columns});
  } catch (const std::invalid_argument&) {
    rejected_bad_table = true;
  }
  require(rejected_bad_table);

  return 0;
}
