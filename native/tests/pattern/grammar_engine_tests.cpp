#include "traceloom/adapters/fixture_adapter.h"
#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/pattern/grammar_state.h"
#include "traceloom/testing/test_util.h"

#include <string>
#include <vector>

namespace {

traceloom::NativeIr make_ir(const std::vector<std::string>& symbols) {
  using namespace traceloom;

  FixtureInput input;
  std::int64_t ts = 0;
  for (const std::string& symbol : symbols) {
    input.tokens.push_back(
        FixtureToken{symbol, AnchorKind::kDeviceEvent, 0, 0, ts, ts + 10});
    ts += 10;
  }
  return FixtureAdapter(input).load();
}

traceloom::GlobalGrammarState make_state(
    const std::vector<std::string>& symbols,
    std::size_t target_nodes_per_chunk = 2,
    std::size_t worker_count = 2) {
  traceloom::GrammarStateConfig config;
  config.target_nodes_per_chunk = target_nodes_per_chunk;
  config.worker_count = worker_count;
  return traceloom::build_initial_grammar_state(make_ir(symbols), config);
}

bool has_stage(const std::vector<traceloom::GrammarEngineStep>& steps,
               traceloom::GrammarStage stage) {
  for (const traceloom::GrammarEngineStep& step : steps) {
    if (step.stage == stage) {
      return true;
    }
  }
  return false;
}

bool has_action(const std::vector<traceloom::GrammarEngineStep>& steps,
                traceloom::GrammarActionKind kind) {
  for (const traceloom::GrammarEngineStep& step : steps) {
    if (step.round_status == traceloom::GrammarRoundStatus::kActionSelected &&
        step.action_kind == kind) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  GlobalGrammarState run_state = make_state({"A", "A", "A", "B", "B"});
  GrammarEngineResult run_result = run_grammar_state_machine(run_state);
  require(run_result.ok());
  require(run_result.stop_reason == GrammarEngineStopReason::kDone);
  require(run_state.stage == GrammarStage::kDone);
  require(run_state.macro_defs.size() == 2);
  require(run_state.live_node_count == 2);
  require(has_action(run_result.steps, GrammarActionKind::kReplaceExactRuns));
  require(has_stage(run_result.steps, GrammarStage::kPairGrammar));
  require(std::string(grammar_engine_stop_reason_name(run_result.stop_reason)) ==
          "done");

  GlobalGrammarState pair_macro_state =
      make_state({"A", "B", "A", "B", "C", "A",
                  "B", "A", "B", "A", "B"});
  GrammarEngineResult pair_macro_result =
      run_grammar_state_machine(pair_macro_state);
  require(pair_macro_result.ok());
  require(pair_macro_state.stage == GrammarStage::kDone);
  require(pair_macro_state.macro_defs.size() == 3);
  require(pair_macro_state.live_node_count == 3);
  require(has_action(pair_macro_result.steps, GrammarActionKind::kReplacePair));
  require(has_action(pair_macro_result.steps,
                     GrammarActionKind::kCompressMaximalRuns));
  require(pair_macro_state.macro_defs[0].level == MacroLevel::kRP);
  require(pair_macro_state.macro_defs[1].level == MacroLevel::kLP);
  require(pair_macro_state.macro_defs[2].level == MacroLevel::kLP);

  GlobalGrammarState capped_state =
      make_state({"A", "B", "C", "D"}, 2, 2);
  GrammarEngineConfig capped_config;
  capped_config.full_discovery_cap = 2;
  GrammarEngineResult capped_result =
      run_grammar_state_machine(capped_state, capped_config);
  require(capped_result.ok());
  require(capped_result.stop_reason ==
          GrammarEngineStopReason::kSequenceTooLargeForFullPairDiscovery);
  require(capped_state.stage == GrammarStage::kDone);
  require(capped_state.macro_defs.empty());

  GlobalGrammarState limited_state =
      make_state({"A", "A", "A", "B", "B"});
  GrammarEngineConfig limited_config;
  limited_config.max_rounds = 1;
  GrammarEngineResult limited_result =
      run_grammar_state_machine(limited_state, limited_config);
  require(!limited_result.ok());
  require(limited_result.stop_reason ==
          GrammarEngineStopReason::kRoundLimitExceeded);
  require(limited_state.stage == GrammarStage::kError);

  GlobalGrammarState one_worker_state =
      make_state({"A", "B", "A", "B", "C", "A",
                  "B", "A", "B", "A", "B"},
                 2, 1);
  GrammarEngineResult one_worker_result =
      run_grammar_state_machine(one_worker_state);
  require(one_worker_result.ok());
  require(one_worker_state.live_node_count ==
          pair_macro_state.live_node_count);
  require(one_worker_state.macro_defs.size() ==
          pair_macro_state.macro_defs.size());

  return 0;
}
