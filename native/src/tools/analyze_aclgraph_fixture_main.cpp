#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "traceloom/adapters/aclgraph_fixture_adapter.h"
#include "traceloom/adapters/aclgraph_fixture_reader.h"
#include "traceloom/analysis/native_pipeline.h"
#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/materialize/native_result_json.h"
#include "traceloom/report/anchor_internal_cost_breakdown.h"

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
  std::string compat_sidecar_out_path;
  std::size_t threads = 4;
  std::size_t top_candidate_limit = 16;
};

void print_usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " --fixture <aclgraph-fixture-v1.json> [--threads N]"
               " [--out PATH|-] [--top-candidates N]"
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
    } else if (arg == "--compat-sidecar-out") {
      options.compat_sidecar_out_path = require_value(arg);
    } else if (arg == "--top-candidates") {
      options.top_candidate_limit = parse_size(require_value(arg), arg);
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argc > 0 ? argv[0]
                           : "traceloom-native-analyze-aclgraph-fixture");
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

    traceloom::AclGraphSemanticFixture fixture;
    traceloom::NativeIr ir;
    const Stopwatch load_watch;
    fixture = traceloom::load_aclgraph_semantic_fixture(cli.fixture_path);
    ir = traceloom::AclGraphFixtureAdapter(fixture).load();
    const double load_ms = load_watch.elapsed_ms();

    traceloom::NativePipelineOptions pipeline_options;
    pipeline_options.thread_count = cli.threads;
    pipeline_options.partition_config = traceloom::PartitionPlanConfig{4096, 3};
    pipeline_options.candidate_scan_config =
        traceloom::CandidateScanConfig{2, 3};
    pipeline_options.anchor_mode =
        traceloom::NativePipelineAnchorMode::kUseExistingAnchorsAndTokens;

    const traceloom::NativePipelineResult pipeline =
        traceloom::run_native_pipeline(ir, pipeline_options);

    const traceloom::AnchorInternalCostBreakdown breakdown =
        traceloom::build_aclgraph_fixture_anchor_cost_breakdown(fixture, ir);

    traceloom::NativeResultJsonOptions json_options;
    json_options.source_kind = "aclgraph_semantic_fixture";
    json_options.source_path = cli.fixture_path;
    json_options.fixture_id = fixture.fixture_id;
    json_options.thread_count = cli.threads;
    json_options.top_candidate_limit = cli.top_candidate_limit;
    json_options.load_source_adapter_ms = load_ms;
    json_options.anchor_internal_cost_breakdown = &breakdown;

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
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << "\n";
    print_usage(argc > 0 ? argv[0]
                         : "traceloom-native-analyze-aclgraph-fixture");
    return 1;
  }

  return 0;
}
