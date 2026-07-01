#include "traceloom/adapters/aclgraph_fixture_adapter.h"
#include "traceloom/analysis/native_pipeline.h"
#include "traceloom/testing/test_util.h"

#include <map>
#include <string>

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
  }

  return 0;
}
