#include "traceloom/adapters/fixture_adapter.h"
#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/pattern/grammar_state.h"
#include "traceloom/testing/test_util.h"

#include <sstream>
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
    std::size_t target_nodes_per_chunk = 2,
    std::size_t worker_count = 2,
    std::vector<traceloom::FixtureProtectedInterval> intervals = {}) {
  traceloom::GrammarStateConfig config;
  config.target_nodes_per_chunk = target_nodes_per_chunk;
  config.worker_count = worker_count;
  return traceloom::build_initial_grammar_state(
      make_ir(symbols, std::move(intervals)), config);
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

bool has_pair_then_macro_fixpoint(
    const std::vector<traceloom::GrammarEngineStep>& steps) {
  for (std::size_t index = 0; index + 2 < steps.size(); ++index) {
    const traceloom::GrammarEngineStep& pair_step = steps[index];
    if (pair_step.stage != traceloom::GrammarStage::kPairGrammar ||
        pair_step.round_status !=
            traceloom::GrammarRoundStatus::kActionSelected ||
        pair_step.action_kind != traceloom::GrammarActionKind::kReplacePair) {
      continue;
    }
    bool saw_macro_action = false;
    for (std::size_t macro_index = index + 1; macro_index < steps.size();
         ++macro_index) {
      const traceloom::GrammarEngineStep& macro_step = steps[macro_index];
      if (macro_step.stage != traceloom::GrammarStage::kMacroRunFold) {
        return false;
      }
      if (macro_step.round_status ==
              traceloom::GrammarRoundStatus::kActionSelected &&
          macro_step.action_kind ==
              traceloom::GrammarActionKind::kCompressMaximalRuns) {
        saw_macro_action = true;
        continue;
      }
      if (macro_step.round_status == traceloom::GrammarRoundStatus::kStop) {
        return saw_macro_action &&
               macro_index + 1 < steps.size() &&
               steps[macro_index + 1].stage ==
                   traceloom::GrammarStage::kPairGrammar;
      }
      return false;
    }
  }
  return false;
}

std::string grammar_output_signature(
    const traceloom::GlobalGrammarState& state,
    const traceloom::GrammarEngineResult& result) {
  std::ostringstream out;
  out << "stop=" << traceloom::grammar_engine_stop_reason_name(
      result.stop_reason) << ";";
  out << "stage=" << static_cast<int>(state.stage) << ";";
  out << "live=" << state.live_node_count << ";";
  out << "macros=" << state.macro_defs.size() << "[";
  for (const traceloom::MacroDefRow& macro : state.macro_defs) {
    out << macro.id.value() << ":"
        << macro.symbol_id.value() << ":"
        << static_cast<int>(macro.level) << ":"
        << macro.definition_len << ":"
        << macro.replace_count << ":"
        << macro.gain << ":"
        << macro.first_pos << "=(";
    for (traceloom::SymbolId symbol : macro.rhs_symbols) {
      out << symbol.value() << ",";
    }
    out << ");";
  }
  out << "];nodes=[";
  for (const traceloom::GrammarNode& node : state.nodes) {
    out << node.symbol_id.value() << ":";
    if (node.macro_def_id.valid()) {
      out << node.macro_def_id.value();
    } else {
      out << "null";
    }
    out << ":" << node.source_begin_token_index
        << "-" << node.source_end_token_index_exclusive << ";";
  }
  out << "];steps=[";
  for (const traceloom::GrammarEngineStep& step : result.steps) {
    out << static_cast<int>(step.stage) << ":"
        << static_cast<int>(step.producer_id) << ":"
        << static_cast<int>(step.round_status) << ":"
        << static_cast<int>(step.action_kind) << ":"
        << step.before_live_node_count << ">"
        << step.after_live_node_count << ":"
        << step.gain << ":" << step.replace_count << ";";
  }
  out << "]";
  return out.str();
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
  require(has_pair_then_macro_fixpoint(pair_macro_result.steps));
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

  const std::vector<std::string> deterministic_symbols = {
      "A", "B", "A", "B", "C", "A", "B",
      "A", "B", "A", "B", "D", "D", "D"};
  GlobalGrammarState reference_state =
      make_state(deterministic_symbols, 2, 1);
  const GrammarEngineResult reference_result =
      run_grammar_state_machine(reference_state);
  const std::string reference_signature =
      grammar_output_signature(reference_state, reference_result);
  for (const std::pair<std::size_t, std::size_t> variant :
       std::vector<std::pair<std::size_t, std::size_t>>{
           {1, 2}, {2, 3}, {3, 4}, {5, 2}}) {
    GlobalGrammarState variant_state =
        make_state(deterministic_symbols, variant.first, variant.second);
    const GrammarEngineResult variant_result =
        run_grammar_state_machine(variant_state);
    require(variant_result.ok());
    require(grammar_output_signature(variant_state, variant_result) ==
            reference_signature);
  }

  GlobalGrammarState boundary_pair_state =
      make_state({"A", "B", "A", "B", "A", "B", "A", "B"}, 1, 3);
  const GrammarEngineResult boundary_pair_result =
      run_grammar_state_machine(boundary_pair_state);
  require(boundary_pair_result.ok());
  require(has_action(boundary_pair_result.steps,
                     GrammarActionKind::kReplacePair));
  require(has_action(boundary_pair_result.steps,
                     GrammarActionKind::kCompressMaximalRuns));
  require(boundary_pair_state.live_node_count == 1);
  require(boundary_pair_state.macro_defs.size() == 2);
  require(boundary_pair_state.macro_defs[0].level == MacroLevel::kRP);
  require(boundary_pair_state.macro_defs[1].level == MacroLevel::kLP);

  GlobalGrammarState boundary_macro_state =
      make_state({"A", "B", "A", "B", "C", "A",
                  "B", "A", "B", "A", "B"},
                 1, 4);
  const GrammarEngineResult boundary_macro_result =
      run_grammar_state_machine(boundary_macro_state);
  require(boundary_macro_result.ok());
  require(has_action(boundary_macro_result.steps,
                     GrammarActionKind::kCompressMaximalRuns));
  require(boundary_macro_state.live_node_count == 3);
  require(boundary_macro_state.macro_defs.size() == 3);

  std::vector<FixtureProtectedInterval> no_cross = {
      FixtureProtectedInterval{ProtectedIntervalKind::kGraphReplayUnit,
                               BoundaryPolicy::kNoCross, 1, 3},
  };
  GlobalGrammarState protected_state =
      make_state({"A", "A", "A", "B"}, 2, 2, no_cross);
  const GrammarEngineResult protected_result =
      run_grammar_state_machine(protected_state);
  require(!protected_result.ok());
  require(protected_result.stop_reason ==
          GrammarEngineStopReason::kCommitPlanRejected);
  require(protected_state.stage == GrammarStage::kError);
  require(!protected_result.commit_diagnostics.empty());
  require(protected_result.commit_diagnostics[0].code ==
          GrammarCommitDiagnosticCode::kProtectedIntervalViolation);

  return 0;
}
