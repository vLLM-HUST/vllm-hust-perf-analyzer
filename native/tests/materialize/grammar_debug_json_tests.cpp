#include "traceloom/adapters/fixture_adapter.h"
#include "traceloom/materialize/grammar_debug_json.h"
#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/pattern/grammar_state.h"
#include "traceloom/testing/test_util.h"

#include <sstream>
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
  write_grammar_debug_json(out, ir.symbols, state, result);
  const std::string json = out.str();

  require(json.find("\"schema_version\": \"native_grammar_debug_v1\"") !=
          std::string::npos);
  require(json.find("\"mode\": \"analysis_quality_v1\"") !=
          std::string::npos);
  require(json.find("\"full_discovery_cap\": 1234") != std::string::npos);
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
  require(json.find("\"final_sequence\"") != std::string::npos);
  require(json.find("\"name\": \"<macro:") != std::string::npos);

  GrammarDebugJsonOptions compact_options;
  compact_options.include_final_sequence = false;
  std::ostringstream compact_out;
  write_grammar_debug_json(compact_out, ir.symbols, state, result,
                           compact_options);
  require(compact_out.str().find("\"final_sequence\"") == std::string::npos);

  return 0;
}
