#include "traceloom/adapters/fixture_adapter.h"
#include "traceloom/materialize/grammar_debug_json.h"
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

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir = make_ir({"A", "B", "A", "B", "C", "A",
                         "B", "A", "B", "A", "B"});
  GrammarStateConfig state_config;
  state_config.target_nodes_per_chunk = 2;
  state_config.worker_count = 3;
  state_config.full_discovery_cap = 1234;
  GlobalGrammarState state = build_initial_grammar_state(ir, state_config);

  GrammarEngineConfig engine_config;
  engine_config.full_discovery_cap = state_config.full_discovery_cap;
  const GrammarEngineResult result =
      run_grammar_state_machine(state, engine_config);
  require(result.ok());

  std::ostringstream out;
  GrammarDebugJsonOptions options;
  options.engine_max_rounds = engine_config.max_rounds;
  write_grammar_debug_json(out, ir.symbols, state, result, options);
  const std::string json = out.str();
  std::ostringstream repeat_out;
  write_grammar_debug_json(repeat_out, ir.symbols, state, result, options);
  require(repeat_out.str() == json);

  require(json.find("\"schema_version\": \"native_grammar_debug_v1\"") !=
          std::string::npos);
  require(json.find("\"mode\": \"analysis_quality_v1\"") !=
          std::string::npos);
  require(json.find("\"full_discovery_cap\": 1234") != std::string::npos);
  require(json.find("\"target_nodes_per_chunk\": 2") != std::string::npos);
  require(json.find("\"worker_count\": 3") != std::string::npos);
  require(json.find("\"max_rounds\": 10000") != std::string::npos);
  require(json.find("\"AdjacentRunProducer\"") != std::string::npos);
  require(json.find("\"PairGrammarProducer\"") != std::string::npos);
  require(json.find("\"NativeMacroRunProducer\"") != std::string::npos);
  require(json.find("\"generic_repeated_block_skipped_native_v1\"") !=
          std::string::npos);
  require(json.find("\"stop_reason\": \"done\"") != std::string::npos);
  require(json.find("\"producer\": \"PairGrammarProducer\"") !=
          std::string::npos);
  require(json.find("\"action\": \"replace_pair\"") != std::string::npos);
  require(json.find("\"action\": \"compress_maximal_runs\"") !=
          std::string::npos);
  require(json.find("\"level\": \"RP\"") != std::string::npos);
  require(json.find("\"level\": \"LP\"") != std::string::npos);
  require(json.find("\"commit_diagnostics\"") != std::string::npos);
  require(json.find("\"apply_diagnostics\"") != std::string::npos);
  require(json.find("\"final_sequence\"") != std::string::npos);
  require(json.find("\"name\": \"<macro:") != std::string::npos);

  GrammarDebugJsonOptions compact_options;
  compact_options.include_final_sequence = false;
  std::ostringstream compact_out;
  write_grammar_debug_json(compact_out, ir.symbols, state, result,
                           compact_options);
  const std::string compact_json = compact_out.str();
  require(compact_json.find("\"algorithm\"") != std::string::npos);
  require(compact_json.find("\"state\"") != std::string::npos);
  require(compact_json.find("\"engine\"") != std::string::npos);
  require(compact_json.find("\"steps\"") != std::string::npos);
  require(compact_json.find("\"macro_defs\"") != std::string::npos);
  require(compact_json.find("\"final_sequence\"") == std::string::npos);

  std::vector<FixtureProtectedInterval> no_cross = {
      FixtureProtectedInterval{ProtectedIntervalKind::kGraphReplayUnit,
                               BoundaryPolicy::kNoCross, 1, 3},
  };
  NativeIr protected_ir = make_ir({"A", "A", "A", "B"}, no_cross);
  GrammarStateConfig protected_config;
  protected_config.target_nodes_per_chunk = 2;
  protected_config.worker_count = 2;
  GlobalGrammarState protected_state =
      build_initial_grammar_state(protected_ir, protected_config);
  const GrammarEngineResult protected_result =
      run_grammar_state_machine(protected_state);
  require(!protected_result.ok());
  std::ostringstream protected_out;
  write_grammar_debug_json(protected_out, protected_ir.symbols,
                           protected_state, protected_result);
  const std::string protected_json = protected_out.str();
  require(protected_json.find("\"stop_reason\": \"commit_plan_rejected\"") !=
          std::string::npos);
  require(protected_json.find("\"code\": \"protected_interval_violation\"") !=
          std::string::npos);
  require(protected_json.find(
              "\"boundary_violation\": \"crosses_no_cross_boundary\"") !=
          std::string::npos);

  return 0;
}
