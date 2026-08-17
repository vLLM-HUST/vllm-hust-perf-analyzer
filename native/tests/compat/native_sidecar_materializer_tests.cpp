#include "native_sidecar_materializer_test_support.h"

#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/testing/test_util.h"

#include <cstdio>
#include <cstdint>
#include <string>

namespace {

using namespace traceloom;

NativeIr build_reconciled_evidence_role_ir() {
  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("ascend", "memory", "TASK", 0);
  const SymbolId mix_aiv = ir.symbols.intern("KERNEL_MIX_AIV");
  const SymbolId reduce_all = ir.symbols.intern("ReduceAll");
  const TraceEventId envelope =
      ir.trace_events.append(source, 1, 0, 46, 10, 30, mix_aiv);
  const TraceEventId detail =
      ir.trace_events.append(source, 2, 0, 46, 12, 28, mix_aiv);
  ir.tasks.append(source, envelope, 1, 1, 1, mix_aiv,
                  SymbolId::invalid(), SymbolId::invalid(),
                  SymbolId::invalid(), SymbolId::invalid(), -1,
                  SymbolId::invalid(), 4294967295LL);
  ir.tasks.append(source, detail, 1, 2, 1, mix_aiv, SymbolId::invalid(),
                  reduce_all, SymbolId::invalid(), SymbolId::invalid(), -1,
                  SymbolId::invalid(), 0);

  FlatAnchorBuildConfig config;
  config.filter_auxiliary_task_anchors = true;
  const FlatAnchorBuildStats stats = build_flat_anchors(ir, config);
  traceloom::testing::require(stats.reconciled_event_groups == 1);
  traceloom::testing::require(stats.reconciled_event_members == 2);
  return ir;
}

NativeIr build_multi_device_structural_ir() {
  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "multi-device-sidecar", "TASK", 0);
  const SymbolId matmul = ir.symbols.intern("MatMul");
  const SymbolId all_reduce = ir.symbols.intern("AllReduce");
  const SymbolId softmax = ir.symbols.intern("Softmax");
  const auto append_token = [&](std::uint32_t device_id,
                                std::uint64_t source_row_id,
                                SymbolId symbol,
                                AnchorKind kind,
                                std::int64_t start_ns,
                                std::int64_t end_ns) {
    const TraceEventId event = ir.trace_events.append(
        source, source_row_id, device_id, 3, start_ns, end_ns, symbol);
    const AnchorId anchor = ir.anchors.append(
        source, event, ReplayUnitId::invalid(), kind, symbol, device_id, 3,
        start_ns, end_ns);
    ir.tokens.append(
        anchor, symbol, device_id,
        static_cast<std::uint32_t>(ir.tokens.size()), start_ns, end_ns);
  };
  for (std::uint32_t step = 0; step < 3; ++step) {
    const std::int64_t base = 1000 + static_cast<std::int64_t>(step) * 1000;
    append_token(0, step + 1, matmul, AnchorKind::kDeviceEvent, base,
                 base + 600);
  }
  for (std::uint32_t step = 0; step < 3; ++step) {
    const std::int64_t base = 4000 + static_cast<std::int64_t>(step) * 1000;
    append_token(0, step + 10, all_reduce, AnchorKind::kCommunication, base,
                 base + 400);
  }
  for (std::uint32_t step = 0; step < 4; ++step) {
    const std::int64_t base = 3000 + static_cast<std::int64_t>(step) * 1000;
    append_token(1, 100 + step, softmax, AnchorKind::kDeviceEvent, base,
                 base + 300);
  }
  return ir;
}

}  // namespace

int main() {
  using namespace traceloom;
  using namespace traceloom::testing::sidecar_materializer;
  using traceloom::testing::require;

  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "memory", "TASK", 0);
  const SymbolId task_type = ir.symbols.intern("AI_CORE");
  const SymbolId op_name = ir.symbols.intern("MatMul");
  const SymbolId op_type = ir.symbols.intern("Cube");

  const TraceEventId event =
      ir.trace_events.append(source, 12, 0, 5, 1000, 3000, task_type);
  ir.tasks.append(source, event, 77, 9001, -1, task_type, op_name, op_type,
                  task_type, SymbolId::invalid());
  const AnchorId anchor =
      ir.anchors.append(source, event, ReplayUnitId::invalid(),
                        AnchorKind::kDeviceEvent, op_type, 0, 5, 1000, 3000);
  ir.tokens.append(anchor, op_type, 0, 0, 1000, 3000);

  const std::string db_path = temp_db_path();
  compat::NativeCompatibilitySidecarOptions options;
  options.db_idx = 2;
  options.source_kind = "fixture";
  options.source_path = "memory";
  options.input_evidence_contract = "ascend_full_profile_v1";
  options.input_scope = "monolithic_db_only";
  options.input_evidence_state = "evidence_incomplete";
  options.input_missing_components = "host/sqlite/runtime.db";
  compat::write_basic_native_compatibility_sidecar(db_path, ir, options);

  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_metadata") == 41);
  require(run_scalar_text(db_path,
                          "SELECT value FROM traceloom_metadata WHERE key = "
                          "'input_evidence_contract'") ==
          "ascend_full_profile_v1");
  require(run_scalar_text(db_path,
                          "SELECT value FROM traceloom_metadata WHERE key = "
                          "'input_scope'") == "monolithic_db_only");
  require(run_scalar_text(db_path,
                          "SELECT value FROM traceloom_metadata WHERE key = "
                          "'input_evidence_state'") == "evidence_incomplete");
  require(run_scalar_text(db_path,
                          "SELECT value FROM traceloom_metadata WHERE key = "
                          "'input_missing_components'") ==
          "host/sqlite/runtime.db");
  require(run_scalar_text(db_path,
                          "SELECT value FROM traceloom_metadata WHERE key = "
                          "'evidence_role_policy_id'") ==
          "traceloom.default.accelerator-task-projection");
  require(run_scalar_text(db_path,
                          "SELECT value FROM traceloom_metadata WHERE key = "
                          "'evidence_role_policy_version'") == "1");
  require(run_scalar_int(
              db_path,
              "SELECT length(value) FROM traceloom_metadata WHERE key = "
              "'evidence_role_manifest_sha256'") == 64);
  require(run_scalar_text(db_path,
                          "SELECT value FROM traceloom_metadata "
                          "WHERE key = 'native_compatibility_materializer'") ==
          "basic_native_ir_v1");
  require(run_scalar_text(db_path,
                          "SELECT value FROM traceloom_metadata "
                          "WHERE key = 'runtime_call_count'") == "0");
  require(run_scalar_text(db_path,
                          "SELECT value FROM traceloom_metadata "
                          "WHERE key = 'device_work_count'") == "1");
  require(run_scalar_text(db_path,
                          "SELECT value FROM traceloom_metadata "
                          "WHERE key = 'runtime_device_relation_count'") ==
          "1");
  require(run_scalar_text(db_path,
                          "SELECT value FROM traceloom_metadata "
                          "WHERE key = 'anchor_host_interval_count'") == "0");
  require(run_scalar_text(db_path,
                          "SELECT value FROM traceloom_metadata "
                          "WHERE key = 'anchor_host_activity_count'") == "0");
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_event") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_event_source") == 1);
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_anchor") ==
          1);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_anchor_cost_breakdown") == 1);
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_viz_node") ==
          2);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_viz_node_anchor") ==
          2);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_anchor_primary_node") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_semantic_tree") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_semantic_node") == 2);
  require(run_scalar_text(db_path,
                          "SELECT symbol FROM traceloom_event "
                          "WHERE event_id = 'event-0'") == "Cube");
  require(run_scalar_text(db_path,
                          "SELECT event_id FROM traceloom_anchor "
                          "WHERE anchor_id = 'anchor-0'") == "event-0");
  require(run_scalar_int(db_path,
                         "SELECT CAST(total_us * 1000 AS INTEGER) FROM "
                         "traceloom_anchor_cost_breakdown "
                         "WHERE anchor_idx = 1") == 2000);
  require(run_scalar_text(db_path,
                          "SELECT anchor_kind FROM "
                          "traceloom_anchor_cost_breakdown "
                          "WHERE anchor_idx = 1") == "exec");
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_event_source s "
              "LEFT JOIN traceloom_event e ON e.event_id = s.event_id "
              "WHERE e.event_id IS NULL") == 0);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_anchor a "
              "LEFT JOIN traceloom_event e ON e.event_id = a.event_id "
              "WHERE e.event_id IS NULL") == 0);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM sqlite_master "
              "WHERE type = 'view' AND name = 'traceloom_v_tree_node'") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_collective_global_link") == 0);

  std::remove(db_path.c_str());

  const std::string reconciled_db_path = temp_db_path();
  compat::NativeCompatibilitySidecarOptions reconciled_options;
  reconciled_options.source_kind = "ascend_sqlite_hot_path";
  reconciled_options.source_path = "memory";
  reconciled_options.evidence_role_config.classification_rules =
      load_default_signal_classification_ruleset();
  reconciled_options.evidence_role_config.filter_auxiliary_task_anchors = true;
  compat::write_basic_native_compatibility_sidecar(
      reconciled_db_path, build_reconciled_evidence_role_ir(),
      reconciled_options);
  require(run_scalar_int(
              reconciled_db_path,
              "SELECT COUNT(*) FROM traceloom_evidence_role_decision WHERE "
              "rule_id = 'system.event_reconciliation' AND final_role = "
              "'anchor'") == 1);
  require(run_scalar_int(
              reconciled_db_path,
              "SELECT COUNT(*) FROM traceloom_evidence_role_decision d LEFT "
              "JOIN traceloom_evidence_role_rule r ON r.policy_id = "
              "d.policy_id AND r.rule_id = d.rule_id WHERE r.rule_id IS "
              "NULL") == 0);
  std::remove(reconciled_db_path.c_str());

  run_graph_materializer_tests();

  const std::string collective_db_path = temp_db_path();
  compat::NativeCompatibilitySidecarOptions collective_options;
  collective_options.db_idx = 3;
  collective_options.source_kind = "fixture";
  collective_options.source_path = "collective-smoke";
  collective_options.collective_run_name = "collective smoke";
  collective_options.collective_db_name = "db00.traceloom_augmented.db";
  collective_options.collective_expected_world_size = 1;
  compat::write_basic_native_compatibility_sidecar(
      collective_db_path, build_collective_repeat_ir(), collective_options);

  require(run_scalar_int(collective_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_collective_global_link") == 4);
  require(run_scalar_text(collective_db_path,
                          "SELECT op_type FROM "
                          "traceloom_collective_global_link "
                          "ORDER BY idx_in_occurrence LIMIT 1") ==
          "allReduce");
  require(run_scalar_text(collective_db_path,
                          "SELECT validation_status FROM "
                          "traceloom_collective_global_link "
                          "ORDER BY idx_in_occurrence LIMIT 1") ==
          "complete");
  require(run_scalar_text(collective_db_path,
                          "SELECT candidate_collective_key FROM "
                          "traceloom_collective_global_link "
                          "ORDER BY idx_in_occurrence LIMIT 1")
              .find("collective_smoke:LP_M002_01_") == 0);
  require(run_scalar_int(collective_db_path,
                         "SELECT COUNT(*) FROM traceloom_viz_node "
                         "WHERE kind = 'atom' AND symbol = 'HcclAllReduce'") ==
          1);
  require(run_scalar_int(collective_db_path,
                         "SELECT occurrence_count FROM traceloom_viz_node "
                         "WHERE kind = 'atom' AND symbol = 'HcclAllReduce'") ==
          4);

  std::remove(collective_db_path.c_str());

  run_packaging_materializer_tests();

  // One profiler DB may contain several devices. Structural recovery and
  // semantic publication stay device-local, with queryable grammar-completion
  // status per tree.
  const std::string multi_db_path = temp_db_path();
  compat::NativeCompatibilitySidecarOptions multi_options;
  multi_options.source_kind = "native_multi_device_fixture";
  multi_options.source_path = "memory";
  multi_options.grammar_full_discovery_cap = 1;
  compat::write_basic_native_compatibility_sidecar(
      multi_db_path, build_multi_device_structural_ir(), multi_options);
  require(run_scalar_int(
              multi_db_path,
              "SELECT COUNT(DISTINCT device_id) FROM traceloom_viz_node "
              "WHERE view_name = 'native_report_tree'") == 2);
  require(run_scalar_int(
              multi_db_path,
              "SELECT COUNT(*) FROM traceloom_semantic_tree") == 2);
  require(run_scalar_text(
              multi_db_path,
              "SELECT tree_id FROM traceloom_semantic_tree "
              "WHERE device_id = 0") == "native-report-tree-d0");
  require(run_scalar_text(
              multi_db_path,
              "SELECT root_node_id FROM traceloom_semantic_tree "
              "WHERE device_id = 1") == "node-d1-N001");
  require(run_scalar_text(
              multi_db_path,
              "SELECT macro_discovery FROM traceloom_semantic_tree "
              "WHERE device_id = 0") ==
          "native_report_tree_partial_size_limit");
  require(run_scalar_text(
              multi_db_path,
              "SELECT macro_discovery FROM traceloom_semantic_tree "
              "WHERE device_id = 1") == "native_report_tree_complete");
  require(run_scalar_int(
              multi_db_path,
              "SELECT COUNT(*) FROM traceloom_viz_node_anchor na "
              "JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id "
              "WHERE na.device_id != a.device_id") == 0);
  std::remove(multi_db_path.c_str());
  return 0;
}
