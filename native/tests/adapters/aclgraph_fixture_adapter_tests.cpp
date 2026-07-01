#include "traceloom/adapters/aclgraph_fixture_adapter.h"
#include "traceloom/analysis/native_pipeline.h"
#include "traceloom/report/anchor_graph_child_cost.h"
#include "traceloom/report/anchor_internal_cost_breakdown.h"
#include "traceloom/report/report_tree_builder.h"
#include "traceloom/testing/test_util.h"

#include <map>
#include <string>
#include <vector>

namespace {

std::string fixture_path(const std::string& name) {
  return std::string(TRACELOOM_WORKSPACE_ROOT) +
         "/drafts/refactor/80_tests_fixtures/fixtures/aclgraph/" + name +
         ".json";
}

std::string token_sequence(const traceloom::NativeIr& ir) {
  std::string out;
  for (const traceloom::TokenRow& token : ir.tokens.rows()) {
    if (!out.empty()) {
      out += " ";
    }
    out += ir.symbols.value(token.symbol_id);
  }
  return out;
}

std::map<std::string, std::size_t> candidate_counts(
    const traceloom::NativeIr& ir,
    const std::vector<traceloom::CandidateSummaryRow>& rows) {
  std::map<std::string, std::size_t> counts;
  for (const traceloom::CandidateSummaryRow& row : rows) {
    std::string key;
    for (traceloom::SymbolId symbol : row.key.symbols) {
      if (!key.empty()) {
        key += " ";
      }
      key += ir.symbols.value(symbol);
    }
    counts.emplace(key, row.occurrence_count);
  }
  return counts;
}

traceloom::ReportAnchorKind report_anchor_kind(
    traceloom::AnchorKind anchor_kind) {
  switch (anchor_kind) {
    case traceloom::AnchorKind::kGraphH:
      return traceloom::ReportAnchorKind::kGraphH;
    case traceloom::AnchorKind::kGraphL:
      return traceloom::ReportAnchorKind::kGraphL;
    case traceloom::AnchorKind::kGraphT:
      return traceloom::ReportAnchorKind::kGraphT;
    default:
      return traceloom::ReportAnchorKind::kUnknown;
  }
}

std::vector<traceloom::ReportToken> report_tokens_from_ir(
    const traceloom::NativeIr& ir) {
  std::vector<traceloom::ReportToken> tokens;
  tokens.reserve(ir.tokens.size());
  for (const traceloom::TokenRow& token : ir.tokens.rows()) {
    const traceloom::AnchorRow& anchor = ir.anchors.row(token.anchor_id);
    traceloom::ReportToken out;
    out.ordinal = token.sequence_index;
    out.symbol_id = token.symbol_id;
    out.display_op = ir.symbols.value(token.symbol_id);
    out.display_category = "graph";
    out.anchor_kind = report_anchor_kind(anchor.kind);
    out.anchor_id = token.anchor_id;
    out.launch_activity_id = "";
    out.start_ns = token.start_ns;
    out.end_ns = token.end_ns;
    tokens.push_back(out);
  }
  return tokens;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  {
    const AclGraphSemanticFixture fixture =
        load_aclgraph_semantic_fixture(
            fixture_path("aclgraph_hlt_single_unit"));
    const NativeIr ir = AclGraphFixtureAdapter(fixture).load();

    require(ir.graph_templates.size() == 1);
    require(ir.capture_slots.size() == 4);
    require(ir.replay_units.size() == 1);
    require(ir.anchors.size() == 4);
    require(ir.tokens.size() == 4);
    require(ir.protected_intervals.size() == 1);
    require(token_sequence(ir) == "ACLH ACLL ACLL ACLT");

    require(ir.anchors.row(AnchorId(0)).kind == AnchorKind::kGraphH);
    require(ir.anchors.row(AnchorId(1)).kind == AnchorKind::kGraphL);
    require(ir.anchors.row(AnchorId(2)).kind == AnchorKind::kGraphL);
    require(ir.anchors.row(AnchorId(3)).kind == AnchorKind::kGraphT);

    const ReplayUnitRow& replay_unit = ir.replay_units.row(ReplayUnitId(0));
    require(replay_unit.first_anchor_id == AnchorId(0));
    require(replay_unit.last_anchor_id == AnchorId(3));

    const ProtectedIntervalRow& interval =
        ir.protected_intervals.row(ProtectedIntervalId(0));
    require(interval.kind == ProtectedIntervalKind::kGraphReplayUnit);
    require(interval.boundary_policy == BoundaryPolicy::kNoCross);
    require(interval.first_token_id == TokenId(0));
    require(interval.last_token_id == TokenId(3));
  }

  {
    const AclGraphSemanticFixture fixture =
        load_aclgraph_semantic_fixture(
            fixture_path("aclgraph_hlt_multi_unit"));
    const NativeIr ir = AclGraphFixtureAdapter(fixture).load();
    require(ir.replay_units.size() == 2);
    require(ir.anchors.size() == 6);
    require(ir.tokens.size() == 6);
    require(ir.protected_intervals.size() == 2);
    require(token_sequence(ir) == "ACLH ACLL ACLT ACLH ACLL ACLT");
    require(ir.replay_units.row(ReplayUnitId(0)).first_anchor_id ==
            AnchorId(0));
    require(ir.replay_units.row(ReplayUnitId(0)).last_anchor_id == AnchorId(2));
    require(ir.replay_units.row(ReplayUnitId(1)).first_anchor_id ==
            AnchorId(3));
    require(ir.replay_units.row(ReplayUnitId(1)).last_anchor_id == AnchorId(5));

    NativeIr pipeline_ir = AclGraphFixtureAdapter(fixture).load();
    NativePipelineOptions options;
    options.anchor_mode =
        NativePipelineAnchorMode::kUseExistingAnchorsAndTokens;
    options.thread_count = 4;
    options.partition_config = PartitionPlanConfig{2, 3};
    options.candidate_scan_config = CandidateScanConfig{2, 3};
    const NativePipelineResult result =
        run_native_pipeline(pipeline_ir, options);
    const std::map<std::string, std::size_t> counts =
        candidate_counts(pipeline_ir, result.reduced_candidates);
    require(counts.at("ACLH ACLL") == 2);
    require(counts.at("ACLL ACLT") == 2);
    require(counts.find("ACLT ACLH") == counts.end());
  }

  {
    const AclGraphSemanticFixture fixture =
        load_aclgraph_semantic_fixture(
            fixture_path("aclgraph_hlt_launch_metadata"));
    const NativeIr ir = AclGraphFixtureAdapter(fixture).load();
    require(ir.anchors.size() == fixture.hlt_anchor_seeds.size());
    require(ir.anchors.size() == 4);
    require(ir.replay_units.size() == 1);
    require(ir.protected_intervals.size() == 1);
    require(token_sequence(ir) == "ACLH ACLL ACLL ACLT");
    for (const AnchorRow& anchor : ir.anchors.rows()) {
      require(anchor.kind == AnchorKind::kGraphH ||
              anchor.kind == AnchorKind::kGraphL ||
              anchor.kind == AnchorKind::kGraphT);
      require(anchor.replay_unit_id == ReplayUnitId(0));
    }
  }

  {
    const AclGraphSemanticFixture fixture =
        load_aclgraph_semantic_fixture(
            fixture_path("aclgraph_hlt_partial_coverage"));
    const NativeIr ir = AclGraphFixtureAdapter(fixture).load();
    require(ir.anchors.size() == 2);
    require(ir.tokens.size() == 2);
    require(ir.protected_intervals.size() == 1);
    require(token_sequence(ir) == "ACLH ACLT");
  }

  {
    const AclGraphSemanticFixture fixture =
        load_aclgraph_semantic_fixture(
            fixture_path("aclgraph_python_minimal_assets"));
    const NativeIr ir = AclGraphFixtureAdapter(fixture).load();
    require(ir.graph_templates.size() == 1);
    require(ir.capture_slots.size() == 3);
    require(ir.replay_units.size() == 2);
    require(ir.anchors.size() == 5);
    require(ir.tokens.size() == 5);
    require(ir.protected_intervals.size() == 2);
    require(token_sequence(ir) == "ACLH ACLL ACLT ACLH ACLT");
    require(ir.replay_units.row(ReplayUnitId(0)).first_anchor_id ==
            AnchorId(0));
    require(ir.replay_units.row(ReplayUnitId(0)).last_anchor_id == AnchorId(2));
    require(ir.replay_units.row(ReplayUnitId(1)).first_anchor_id ==
            AnchorId(3));
    require(ir.replay_units.row(ReplayUnitId(1)).last_anchor_id == AnchorId(4));

    std::vector<AnchorGraphChildSummary> summaries;
    summaries.reserve(fixture.hlt_anchor_seeds.size());
    for (std::size_t index = 0; index < fixture.hlt_anchor_seeds.size();
         ++index) {
      const AclGraphHltAnchorSeedFixtureRow& seed =
          fixture.hlt_anchor_seeds[index];
      summaries.push_back(AnchorGraphChildSummary{
          static_cast<std::uint32_t>(index),
          seed.start_ns,
          seed.end_ns,
          seed.end_ns - seed.start_ns,
          seed.raw_child_task_count,
          0,
          seed.raw_top_ops,
          "",
      });
    }
    const AnchorGraphChildCostResult graph_cost =
        build_anchor_graph_child_summary_components(summaries);
    require(graph_cost.diagnostics.empty());
    require(graph_cost.component_leaves.size() == 5);
    require(graph_cost.component_leaves[1].token_ordinal == 1);
    require(graph_cost.component_leaves[1].duration_ns == 100);
    require(graph_cost.component_leaves[1].raw_child_task_count == 20);
    require(graph_cost.component_leaves[1].top_ops == "MatMul:16");

    const std::vector<ReportToken> report_tokens = report_tokens_from_ir(ir);
    const ReportTree tree = build_report_tree_from_tokens(report_tokens);
    const AnchorInternalCostBreakdown breakdown =
        build_anchor_internal_cost_breakdown(
            tree, report_tokens, graph_cost.component_leaves);
    require(breakdown.diagnostics.empty());
    require(breakdown.rows.size() == 5);
    require(breakdown.rows[1].symbol == "ACLL");
    require(breakdown.rows[1].anchor_kind == ReportAnchorKind::kGraphL);
    require(breakdown.rows[1].graph_child_ns == 100);
    require(breakdown.rows[1].raw_child_task_count == 20);
    require(breakdown.rows[1].top_ops == "MatMul:16");
  }

  return 0;
}
