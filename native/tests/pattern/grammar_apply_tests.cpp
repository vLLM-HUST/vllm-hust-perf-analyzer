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

traceloom::GrammarCommitPlan selected_plan(
    const traceloom::GlobalGrammarState& state) {
  const traceloom::GrammarSnapshot snapshot =
      traceloom::freeze_grammar_snapshot(state);
  const traceloom::GrammarRoundResult round =
      traceloom::run_adjacent_run_readonly_round(state);
  return traceloom::build_adjacent_run_commit_plan(snapshot, round.action);
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  GlobalGrammarState state = make_state({"A", "A", "B", "B"});
  const SymbolId original_next_macro_symbol = state.next_macro_symbol_id;
  const GrammarCommitPlan plan = selected_plan(state);
  const GrammarApplyResult applied =
      apply_adjacent_run_commit_plan(state, plan);
  require(applied.applied());
  require(state.generation == 1);
  require(state.live_node_count == 3);
  require(state.nodes.size() == 3);
  require(state.chunks.size() == 2);
  require(state.boundary_summaries.size() == 2);
  require(state.macro_defs.size() == 1);
  require(state.macro_defs[0].id == MacroDefId(0));
  require(state.macro_defs[0].symbol_id == original_next_macro_symbol);
  require(state.macro_defs[0].level == MacroLevel::kLP);
  require(state.macro_defs[0].rhs_symbols.size() == 2);
  require(state.macro_defs[0].replace_count == 1);
  require(state.macro_defs[0].gain == 1);
  require(state.macro_defs[0].first_pos == 0);
  require(state.nodes[0].symbol_id == state.macro_defs[0].symbol_id);
  require(state.nodes[0].macro_def_id == MacroDefId(0));
  require(state.nodes[0].source_begin_token_index == 0);
  require(state.nodes[0].source_end_token_index_exclusive == 2);
  require(state.nodes[0].start_ns == 0);
  require(state.nodes[0].end_ns == 20);
  require(state.nodes[1].macro_def_id == MacroDefId::invalid());
  require(state.nodes[1].source_begin_token_index == 2);
  require(state.nodes[2].source_begin_token_index == 3);
  require(state.next_macro_symbol_id.value() ==
          original_next_macro_symbol.value() + 1);

  const GrammarRoundResult next_round =
      run_adjacent_run_readonly_round(state);
  require(next_round.status == GrammarRoundStatus::kActionSelected);
  require(next_round.action.key.run_len == 2);
  require(next_round.action.first_dense_index == 1);

  GlobalGrammarState stale_state = make_state({"A", "A", "B", "B"});
  GrammarCommitPlan stale_plan = selected_plan(stale_state);
  stale_plan.snapshot_generation = 99;
  const std::size_t stale_live_count = stale_state.live_node_count;
  const GrammarApplyResult stale_result =
      apply_adjacent_run_commit_plan(stale_state, stale_plan);
  require(!stale_result.applied());
  require(stale_result.diagnostics[0].code ==
          GrammarApplyDiagnosticCode::kStaleStateGeneration);
  require(stale_state.live_node_count == stale_live_count);
  require(stale_state.generation == 0);
  require(stale_state.macro_defs.empty());

  GlobalGrammarState invalid_state = make_state({"A", "A", "A", "B"}, {
      FixtureProtectedInterval{ProtectedIntervalKind::kGraphReplayUnit,
                               BoundaryPolicy::kNoCross, 1, 3},
  });
  const GrammarCommitPlan invalid_plan = selected_plan(invalid_state);
  require(!invalid_plan.valid());
  const GrammarApplyResult invalid_result =
      apply_adjacent_run_commit_plan(invalid_state, invalid_plan);
  require(!invalid_result.applied());
  require(invalid_result.diagnostics[0].code ==
          GrammarApplyDiagnosticCode::kInvalidCommitPlan);
  require(invalid_state.generation == 0);
  require(invalid_state.macro_defs.empty());

  return 0;
}
