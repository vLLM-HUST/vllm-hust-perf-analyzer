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
#include "traceloom/ir/native_ir.h"
#include "traceloom/materialize/native_result_json.h"

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
  std::size_t threads = 4;
  std::size_t top_candidate_limit = 16;
};

void print_usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " --fixture <protected-sequence-fixture.json> [--threads N]"
               " [--out PATH|-] [--top-candidates N]\n";
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
  return options;
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
    traceloom::write_native_result_json(first_pass, ir.symbols, pipeline,
                                        json_options);
    json_options.materialization_ms = materialize_watch.elapsed_ms();

    std::ostringstream final_json;
    traceloom::write_native_result_json(final_json, ir.symbols, pipeline,
                                        json_options);

    if (cli.out_path == "-") {
      std::cout << final_json.str();
    } else {
      std::ofstream out(cli.out_path);
      if (!out) {
        throw std::runtime_error("failed to open output path: " + cli.out_path);
      }
      out << final_json.str();
    }
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << "\n";
    print_usage(argc > 0 ? argv[0] : "traceloom-native-analyze-fixture");
    return 1;
  }

  return 0;
}
