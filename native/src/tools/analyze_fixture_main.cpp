#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "traceloom/adapters/fixture_adapter.h"
#include "traceloom/adapters/protected_sequence_fixture_reader.h"
#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/materialize/grammar_debug_json.h"
#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/pattern/grammar_state.h"

namespace {

struct CliOptions {
  std::string fixture_path;
  std::string grammar_debug_out_path;
  std::string compat_sidecar_out_path;
  std::size_t threads = 0;
};

std::size_t default_thread_count() {
  const unsigned int hardware = std::thread::hardware_concurrency();
  if (hardware <= 1) {
    return 1;
  }
  return std::max<std::size_t>(1, static_cast<std::size_t>(hardware / 2));
}

void print_usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " --fixture <protected-sequence-fixture.json> [--threads N]"
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
    } else if (arg == "--grammar-debug-out") {
      options.grammar_debug_out_path = require_value(arg);
    } else if (arg == "--compat-sidecar-out") {
      options.compat_sidecar_out_path = require_value(arg);
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
  if (options.grammar_debug_out_path.empty() &&
      options.compat_sidecar_out_path.empty()) {
    throw std::invalid_argument(
        "an explicit --grammar-debug-out or --compat-sidecar-out is required");
  }
  if (options.threads == 0) {
    options.threads = default_thread_count();
  }
  if (options.threads == 0) {
    throw std::invalid_argument("--threads must be greater than zero");
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
    const traceloom::ProtectedSequenceFixture fixture =
        traceloom::load_protected_sequence_fixture(cli.fixture_path);
    traceloom::NativeIr ir = traceloom::FixtureAdapter(fixture.input).load();

    if (!cli.compat_sidecar_out_path.empty()) {
      traceloom::compat::NativeCompatibilitySidecarOptions sidecar_options;
      sidecar_options.source_kind = "protected_sequence_fixture";
      sidecar_options.source_path = cli.fixture_path;
      sidecar_options.grammar_worker_count = cli.threads;
      sidecar_options.grammar_target_nodes_per_chunk =
          fixture.partition_config.target_tokens_per_partition;
      traceloom::compat::write_basic_native_compatibility_sidecar(
          cli.compat_sidecar_out_path, ir, sidecar_options);
    }

    if (!cli.grammar_debug_out_path.empty()) {
      traceloom::GrammarStateConfig grammar_state_config;
      grammar_state_config.target_nodes_per_chunk =
          fixture.partition_config.target_tokens_per_partition;
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
