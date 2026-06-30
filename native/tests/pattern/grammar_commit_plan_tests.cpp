#include "traceloom/adapters/fixture_adapter.h"
#include "traceloom/pattern/grammar_apply.h"
#include "traceloom/pattern/grammar_commit_plan.h"
#include "traceloom/pattern/grammar_round.h"
#include "traceloom/pattern/grammar_snapshot.h"
#include "traceloom/pattern/grammar_state.h"
#include "traceloom/testing/test_util.h"

#include <string>
#include <utility>
#include <vector>

namespace {

traceloom::NativeIr make_ir(
    const std::vector<std::string>& symbols,
    std::vector<traceloom::FixtureProtectedInterval> intervals = {}) {
  using namespace traceloom;

  FixtureInput input;
  std::int64_t ts = 0;
  for (const std::string& symbol : symbols) {
    input.tokens.push_back(
        FixtureToken{symbol, AnchorKind::kDeviceEvent, 0, 0, ts, ts + 10});
    ts += 10;
  }
  input.protected_intervals = std::move(intervals);
  return FixtureAdapter(input).load();
}

traceloom::GlobalGrammarState make_state(
    const std::vector<std::string>& symbols,
    std::vector<traceloom::FixtureProtectedInterval> intervals = {}) {
  traceloom::GrammarStateConfig config;
  config.target_nodes_per_chunk = 2;
  config.worker_count = 2;
  return traceloom::build_initial_grammar_state(
      make_ir(symbols, std::move(intervals)), config);
}

}  // namespace

traceloom::GlobalGrammarState pair_compressed_state(
    const std::vector<std::string>& symbols) {
  traceloom::GlobalGrammarState state = make_state(symbols);
  const traceloom::GrammarSnapshot snapshot =
      traceloom::freeze_grammar_snapshot(state);
  const traceloom::GrammarRoundResult round =
      traceloom::run_pair_grammar_readonly_round(state);
  const traceloom::GrammarCommitPlan plan =
      traceloom::build_pair_grammar_commit_plan(snapshot, round.action);
  const traceloom::GrammarApplyResult applied =
      traceloom::apply_pair_grammar_commit_plan(state, plan);
  traceloom::testing::require(applied.applied());
  return state;
}

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  GlobalGrammarState state = make_state({"A", "A", "B", "B"});
  const GrammarSnapshot snapshot = freeze_grammar_snapshot(state);
  const GrammarRoundResult round = run_adjacent_run_readonly_round(state);
  const GrammarCommitPlan plan =
      build_adjacent_run_commit_plan(snapshot, round.action);

  require(plan.valid());
  require(plan.replacement_spans.size() == 1);
  require(plan.replacement_spans[0].begin_node_id == GrammarNodeId(0));
  require(plan.replacement_spans[0].last_node_id == GrammarNodeId(1));
  require(plan.replacement_spans[0].source_begin_token_index == 0);
  require(plan.replacement_spans[0].source_end_token_index_exclusive == 2);
  require(plan.replacement_spans[0].owner_worker_id == 0);

  GlobalGrammarState pair_state =
      make_state({"A", "B", "A", "B", "A", "B", "A", "B"});
  const GrammarSnapshot pair_snapshot = freeze_grammar_snapshot(pair_state);
  const GrammarRoundResult pair_round =
      run_pair_grammar_readonly_round(pair_state);
  const GrammarCommitPlan pair_plan =
      build_pair_grammar_commit_plan(pair_snapshot, pair_round.action);
  require(pair_plan.valid());
  require(pair_plan.replacement_spans.size() == 4);
  require(pair_plan.replacement_spans[0].begin_dense_index == 0);
  require(pair_plan.replacement_spans[0].end_dense_index_exclusive == 2);
  require(pair_plan.replacement_spans[3].begin_dense_index == 6);
  require(pair_plan.replacement_spans[3].end_dense_index_exclusive == 8);

  GlobalGrammarState macro_state =
      pair_compressed_state({"A", "B", "A", "B", "C", "A",
                             "B", "A", "B", "A", "B"});
  const GrammarSnapshot macro_snapshot = freeze_grammar_snapshot(macro_state);
  const GrammarRoundResult macro_round =
      run_native_macro_run_readonly_round(macro_state);
  const GrammarCommitPlan macro_plan =
      build_native_macro_run_commit_plan(macro_snapshot, macro_round.action);
  require(macro_plan.valid());
  require(macro_plan.replacement_spans.size() == 2);
  require(macro_plan.replacement_spans[0].begin_dense_index == 0);
  require(macro_plan.replacement_spans[0].end_dense_index_exclusive == 2);
  require(macro_plan.replacement_spans[1].begin_dense_index == 3);
  require(macro_plan.replacement_spans[1].end_dense_index_exclusive == 6);

  GrammarGlobalAction non_maximal_macro = macro_round.action;
  non_maximal_macro.occurrences[0] = macro_round.action.occurrences[1];
  non_maximal_macro.occurrences[0].begin_dense_index = 4;
  non_maximal_macro.occurrences[0].begin_node_id = macro_snapshot.nodes[4].node_id;
  const GrammarCommitPlan non_maximal_plan =
      build_native_macro_run_commit_plan(macro_snapshot, non_maximal_macro);
  require(!non_maximal_plan.valid());
  require(non_maximal_plan.diagnostics[0].code ==
          GrammarCommitDiagnosticCode::kReplacementSpanMismatch);

  GrammarGlobalAction stale = round.action;
  stale.snapshot_generation = 99;
  const GrammarCommitPlan stale_plan =
      build_adjacent_run_commit_plan(snapshot, stale);
  require(!stale_plan.valid());
  require(stale_plan.diagnostics.size() == 1);
  require(stale_plan.diagnostics[0].code ==
          GrammarCommitDiagnosticCode::kStaleSnapshotGeneration);

  GrammarGlobalAction mismatched = round.action;
  mismatched.occurrences[0].end_dense_index_exclusive = 3;
  const GrammarCommitPlan mismatch_plan =
      build_adjacent_run_commit_plan(snapshot, mismatched);
  require(!mismatch_plan.valid());
  require(mismatch_plan.diagnostics[0].code ==
          GrammarCommitDiagnosticCode::kReplacementSpanMismatch);

  GrammarGlobalAction overlapping = round.action;
  GrammarCandidateOccurrence duplicate = overlapping.occurrences[0];
  overlapping.occurrences.push_back(duplicate);
  const GrammarCommitPlan overlap_plan =
      build_adjacent_run_commit_plan(snapshot, overlapping);
  require(!overlap_plan.valid());
  require(overlap_plan.diagnostics[0].code ==
          GrammarCommitDiagnosticCode::kOverlappingReplacementSpan);

  std::vector<FixtureProtectedInterval> no_cross = {
      FixtureProtectedInterval{ProtectedIntervalKind::kGraphReplayUnit,
                               BoundaryPolicy::kNoCross, 1, 3},
  };
  GlobalGrammarState crossing_state =
      make_state({"A", "A", "A", "B"}, no_cross);
  const GrammarSnapshot crossing_snapshot =
      freeze_grammar_snapshot(crossing_state);
  const GrammarRoundResult crossing_round =
      run_adjacent_run_readonly_round(crossing_state);
  const GrammarCommitPlan crossing_plan =
      build_adjacent_run_commit_plan(crossing_snapshot,
                                     crossing_round.action);
  require(!crossing_plan.valid());
  require(crossing_plan.diagnostics[0].code ==
          GrammarCommitDiagnosticCode::kProtectedIntervalViolation);
  require(crossing_plan.diagnostics[0].boundary_violation_kind ==
          BoundaryViolationKind::kCrossesNoCrossBoundary);

  std::vector<FixtureProtectedInterval> block_any = {
      FixtureProtectedInterval{ProtectedIntervalKind::kUserWindow,
                               BoundaryPolicy::kBlockAnyOverlap, 0, 1},
  };
  GlobalGrammarState blocked_state =
      make_state({"A", "A", "B"}, block_any);
  const GrammarSnapshot blocked_snapshot =
      freeze_grammar_snapshot(blocked_state);
  const GrammarRoundResult blocked_round =
      run_adjacent_run_readonly_round(blocked_state);
  const GrammarCommitPlan blocked_plan =
      build_adjacent_run_commit_plan(blocked_snapshot, blocked_round.action);
  require(!blocked_plan.valid());
  require(blocked_plan.diagnostics[0].boundary_violation_kind ==
          BoundaryViolationKind::kAmbiguousIntervalBlocksCandidate);

  return 0;
}
