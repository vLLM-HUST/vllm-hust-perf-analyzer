#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "traceloom/adapters/fixture_adapter.h"
#include "traceloom/adapters/protected_sequence_fixture_reader.h"
#include "traceloom/analysis/native_pipeline.h"
#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/materialize/grammar_debug_json.h"
#include "traceloom/materialize/native_result_json.h"
#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/pattern/grammar_state.h"

namespace {

class Stopwatch {
 public:
  Stopwatch() : start_(Clock::now()) {}

  double elapsed_ms() const {
    const auto elapsed = Clock::now() - start_;
    return std::chrono::duration<double, std::milli>(elapsed).count();
  }

 private:
  using Clock = std::chrono::steady_clock;
  Clock::time_point start_;
};

struct CliOptions {
  std::string fixture_path;
  std::string out_path = "-";
  std::string grammar_debug_out_path;
  std::string compat_sidecar_out_path;
  std::size_t threads = 4;
  std::size_t top_candidate_limit = 16;
};

void print_usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " --fixture <protected-sequence-fixture.json> [--threads N]"
               " [--out PATH|-] [--top-candidates N]"
               " [--grammar-debug-out PATH|-]"
               " [--compat-sidecar-out PATH]\n";
}

std::size_t parse_size(const std::string& text, const std::string& flag) {
  std::size_t consumed = 0;
  const unsigned long long value = std::stoull(text, &consumed, 10);
  if (consumed != text.size()) {
    throw std::invalid_argument("invalid integer for " + flag + ": " + text);
  }
  return static_cast<std::size_t>(value);
}

CliOptions parse_args(int argc, char** argv) {
  CliOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    auto require_value = [&](const std::string& flag) -> std::string {
      if (index + 1 >= argc) {
        throw std::invalid_argument("missing value for " + flag);
      }
      ++index;
      return argv[index];
    };

    if (arg == "--fixture") {
      options.fixture_path = require_value(arg);
    } else if (arg == "--threads") {
      options.threads = parse_size(require_value(arg), arg);
    } else if (arg == "--out") {
      options.out_path = require_value(arg);
    } else if (arg == "--grammar-debug-out") {
      options.grammar_debug_out_path = require_value(arg);
    } else if (arg == "--compat-sidecar-out") {
      options.compat_sidecar_out_path = require_value(arg);
    } else if (arg == "--top-candidates") {
      options.top_candidate_limit = parse_size(require_value(arg), arg);
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argc > 0 ? argv[0] : "traceloom-native-analyze-fixture");
      std::exit(0);
    } else {
      throw std::invalid_argument("unknown argument: " + arg);
    }
  }

  if (options.fixture_path.empty()) {
    throw std::invalid_argument("--fixture is required");
  }
  if (options.threads == 0) {
    throw std::invalid_argument("--threads must be greater than zero");
  }
  if (options.out_path == "-" && options.grammar_debug_out_path == "-") {
    throw std::invalid_argument(
        "--out - and --grammar-debug-out - cannot be used together");
  }
  return options;
}

void write_text_output(const std::string& path, const std::string& contents) {
  if (path == "-") {
    std::cout << contents;
    return;
  }
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("failed to open output path: " + path);
  }
  out << contents;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const CliOptions cli = parse_args(argc, argv);

    traceloom::ProtectedSequenceFixture fixture;
    traceloom::NativeIr ir;
    const Stopwatch load_watch;
    fixture = traceloom::load_protected_sequence_fixture(cli.fixture_path);
    ir = traceloom::FixtureAdapter(fixture.input).load();
    const double load_ms = load_watch.elapsed_ms();

    traceloom::NativePipelineOptions pipeline_options;
    pipeline_options.thread_count = cli.threads;
    pipeline_options.partition_config = fixture.partition_config;
    pipeline_options.candidate_scan_config = fixture.candidate_scan_config;
    pipeline_options.anchor_mode =
        traceloom::NativePipelineAnchorMode::kUseExistingAnchorsAndTokens;

    const traceloom::NativePipelineResult pipeline =
        traceloom::run_native_pipeline(ir, pipeline_options);

    traceloom::NativeResultJsonOptions json_options;
    json_options.source_kind = "protected_sequence_fixture";
    json_options.source_path = cli.fixture_path;
    json_options.fixture_id = fixture.fixture_id;
    json_options.thread_count = cli.threads;
    json_options.top_candidate_limit = cli.top_candidate_limit;
    json_options.load_source_adapter_ms = load_ms;

    std::ostringstream first_pass;
    const Stopwatch materialize_watch;
    if (!cli.compat_sidecar_out_path.empty()) {
      traceloom::compat::NativeCompatibilitySidecarOptions sidecar_options;
      sidecar_options.source_kind = json_options.source_kind;
      sidecar_options.source_path = json_options.source_path;
      traceloom::compat::write_basic_native_compatibility_sidecar(
          cli.compat_sidecar_out_path, ir, sidecar_options);
    }
    traceloom::write_native_result_json(first_pass, ir.symbols, pipeline,
                                        json_options);
    json_options.materialization_ms = materialize_watch.elapsed_ms();

    std::ostringstream final_json;
    traceloom::write_native_result_json(final_json, ir.symbols, pipeline,
                                        json_options);

    write_text_output(cli.out_path, final_json.str());

    if (!cli.grammar_debug_out_path.empty()) {
      traceloom::GrammarStateConfig grammar_state_config;
      grammar_state_config.target_nodes_per_chunk =
          pipeline_options.partition_config.target_tokens_per_partition;
      grammar_state_config.worker_count = cli.threads;
      traceloom::GlobalGrammarState grammar_state =
          traceloom::build_initial_grammar_state(ir, grammar_state_config);

      traceloom::GrammarEngineConfig grammar_engine_config;
      grammar_engine_config.full_discovery_cap =
          grammar_state.metadata.full_discovery_cap;
      const traceloom::GrammarEngineResult grammar_result =
          traceloom::run_grammar_state_machine(grammar_state,
                                               grammar_engine_config);

      std::ostringstream grammar_debug_json;
      traceloom::GrammarDebugJsonOptions grammar_debug_options;
      grammar_debug_options.engine_max_rounds =
          grammar_engine_config.max_rounds;
      traceloom::write_grammar_debug_json(grammar_debug_json, ir.symbols,
                                          grammar_state, grammar_result,
                                          grammar_debug_options);
      write_text_output(cli.grammar_debug_out_path,
                        grammar_debug_json.str());
    }
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << "\n";
    print_usage(argc > 0 ? argv[0] : "traceloom-native-analyze-fixture");
    return 1;
  }

  return 0;
}
