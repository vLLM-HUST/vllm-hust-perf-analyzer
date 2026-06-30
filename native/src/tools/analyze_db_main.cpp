#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "traceloom/adapters/ascend_sqlite_adapter.h"
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
  std::string source_db;
  std::string out_path = "-";
  std::size_t threads = 4;
  std::size_t top_candidate_limit = 16;
};

void print_usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " --source-db <ascend-msprof.db> [--threads N]"
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

    if (arg == "--source-db") {
      options.source_db = require_value(arg);
    } else if (arg == "--threads") {
      options.threads = parse_size(require_value(arg), arg);
    } else if (arg == "--out") {
      options.out_path = require_value(arg);
    } else if (arg == "--top-candidates") {
      options.top_candidate_limit = parse_size(require_value(arg), arg);
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argc > 0 ? argv[0] : "traceloom-native-analyze-db");
      std::exit(0);
    } else {
      throw std::invalid_argument("unknown argument: " + arg);
    }
  }

  if (options.source_db.empty()) {
    throw std::invalid_argument("--source-db is required");
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

    const traceloom::AscendSQLiteAdapter adapter(cli.source_db);
    traceloom::NativeIr ir;
    const Stopwatch load_watch;
    ir = adapter.load();
    const double load_ms = load_watch.elapsed_ms();

    traceloom::NativePipelineOptions pipeline_options;
    pipeline_options.thread_count = cli.threads;
    pipeline_options.partition_config = traceloom::PartitionPlanConfig{4096, 3};
    pipeline_options.candidate_scan_config =
        traceloom::CandidateScanConfig{2, 3};
    pipeline_options.anchor_config.skipped_task_type_symbols = {"CAPTURE_WAIT"};
    pipeline_options.anchor_config.skip_tasks_covered_by_communication_ops =
        true;

    const traceloom::NativePipelineResult pipeline =
        traceloom::run_native_pipeline(ir, pipeline_options);

    traceloom::NativeResultJsonOptions json_options;
    json_options.source_kind = "ascend_sqlite_hot_path";
    json_options.source_path = cli.source_db;
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
    print_usage(argc > 0 ? argv[0] : "traceloom-native-analyze-db");
    return 1;
  }

  return 0;
}
