#include "traceloom/adapters/fixture_adapter.h"
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
