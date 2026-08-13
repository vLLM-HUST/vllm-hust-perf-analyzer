#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/compat/symbol_normalization_rows.h"
#include "traceloom/testing/test_util.h"

#include <algorithm>
#include <string>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir;
  const SourceRefId task_source =
      ir.source_refs.append("ascend", "profile.db", "TASK", 0);
  const SourceRefId comm_source = ir.source_refs.append(
      "ascend", "profile.db", "COMMUNICATION_OP", 0);
  const SymbolId ai_core = ir.symbols.intern("AI_CORE");
  const SymbolId matmul_v2 = ir.symbols.intern("MatMulV2");
  const SymbolId novel = ir.symbols.intern("NovelFusedKernel");
  const SymbolId raw_allreduce =
      ir.symbols.intern("hcom_allReduce__fixture_0");

  const TraceEventId matmul_event = ir.trace_events.append(
      task_source, 11, 0, 3, 100, 300, matmul_v2);
  const TraceEventId novel_event =
      ir.trace_events.append(task_source, 12, 0, 3, 400, 700, novel);
  const TraceEventId comm_event = ir.trace_events.append(
      comm_source, 21, 0, 4, 800, 1200, raw_allreduce);
  ir.tasks.append(task_source, matmul_event, 1, 101, -1, ai_core,
                  SymbolId::invalid(), matmul_v2, SymbolId::invalid(),
                  SymbolId::invalid());
  ir.tasks.append(task_source, novel_event, 2, 102, -1, ai_core,
                  SymbolId::invalid(), novel, SymbolId::invalid(),
                  SymbolId::invalid());
  ir.communication_ops.append(comm_source, comm_event, 5, 6, 1, 1,
                              raw_allreduce);

  const FlatAnchorBuildStats stats = build_flat_anchors(ir);
  require(stats.tokens == 3);
  const compat::SymbolNormalizationSqlRows rows =
      compat::build_symbol_normalization_sql_rows(ir, 7);
  require(rows.policies.size() == 1);
  require(rows.policies.front().policy_id ==
          "traceloom.default-structural-symbols");
  require(rows.policies.front().policy_version == "1");
  require(rows.rules.size() >= 10);
  require(std::any_of(rows.rules.begin(), rows.rules.end(),
                      [](const compat::SymbolNormalizationRuleSqlRow& row) {
                        return row.rule_id ==
                                   "ascend.task.matmul-backend-variant" &&
                               row.structural_symbol == "MatMul" &&
                               row.rule_origin_sha256.size() == 64 &&
                               row.source_line > 0;
                      }));
  require(rows.decisions.size() == 3);

  const auto decision_for = [&rows](const std::string& observed) {
    return std::find_if(
        rows.decisions.begin(), rows.decisions.end(),
        [&observed](const compat::AnchorSymbolNormalizationSqlRow& row) {
          return row.observed_symbol == observed;
        });
  };
  const auto matmul = decision_for("MatMulV2");
  require(matmul != rows.decisions.end());
  require(matmul->db_idx == 7);
  require(matmul->source_path == "profile.db");
  require(matmul->source_table == "TASK");
  require(matmul->source_key == "11");
  require(matmul->structural_symbol == "MatMul");
  require(matmul->observed_symbol_source == "task.op_type");
  require(matmul->rule_id == "ascend.task.matmul-backend-variant");
  require(matmul->outcome == "canonicalized");
  require(matmul->reason_code == "explicit_rule_match");

  const auto unfamiliar = decision_for("NovelFusedKernel");
  require(unfamiliar != rows.decisions.end());
  require(unfamiliar->structural_symbol == "NovelFusedKernel");
  require(unfamiliar->rule_id == "fallback.identity-preserve");
  require(unfamiliar->outcome == "identity");

  const auto collective = decision_for("hcom_allReduce__fixture_0");
  require(collective != rows.decisions.end());
  require(collective->structural_symbol == "AllReduce");
  require(collective->rule_id ==
          "collective.communication.allreduce-alias");
  require(collective->source_table == "COMMUNICATION_OP");
  require(collective->source_key == "21");

  return 0;
}
