#include <chrono>
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

#include "traceloom/adapters/ascend_sqlite_adapter.h"
#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/analysis/native_pipeline.h"
#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/materialize/grammar_debug_json.h"
#include "traceloom/materialize/loop_tree_markdown.h"
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
  std::string source_db;
  std::string out_path = "-";
  std::string grammar_debug_out_path;
  std::string compat_sidecar_out_path;
  std::string loop_tree_out_path;
  std::string loop_tree_db_label;
  bool has_loop_tree_device_id = false;
  std::uint32_t loop_tree_device_id = 0;
  bool loop_tree_grammar = true;
  bool loop_tree_aux = false;
  std::size_t loop_tree_full_discovery_cap = 5000000;
  bool sidecar_only = false;
  bool timings = false;
  std::size_t threads = 0;
  std::size_t top_candidate_limit = 16;
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
            << " --source-db <ascend-msprof.db> [--threads N]"
               " [--out PATH|-] [--top-candidates N]"
               " [--grammar-debug-out PATH|-]"
               " [--compat-sidecar-out PATH]"
               " [--loop-tree-out PATH|-]"
               " [--loop-tree-db-label LABEL]"
               " [--loop-tree-device-id N]"
               " [--loop-tree-grammar|--loop-tree-no-grammar]"
               " [--loop-tree-full-discovery-cap N]"
               " [--loop-tree-aux]"
               " [--timings]"
               " [--sidecar-only]\n";
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
    } else if (arg == "--grammar-debug-out") {
      options.grammar_debug_out_path = require_value(arg);
    } else if (arg == "--compat-sidecar-out") {
      options.compat_sidecar_out_path = require_value(arg);
    } else if (arg == "--loop-tree-out") {
      options.loop_tree_out_path = require_value(arg);
    } else if (arg == "--loop-tree-db-label") {
      options.loop_tree_db_label = require_value(arg);
    } else if (arg == "--loop-tree-device-id") {
      options.loop_tree_device_id =
          static_cast<std::uint32_t>(parse_size(require_value(arg), arg));
      options.has_loop_tree_device_id = true;
    } else if (arg == "--loop-tree-grammar") {
      options.loop_tree_grammar = true;
    } else if (arg == "--loop-tree-no-grammar") {
      options.loop_tree_grammar = false;
    } else if (arg == "--loop-tree-full-discovery-cap") {
      options.loop_tree_full_discovery_cap = parse_size(require_value(arg), arg);
    } else if (arg == "--loop-tree-aux") {
      options.loop_tree_aux = true;
    } else if (arg == "--sidecar-only") {
      options.sidecar_only = true;
    } else if (arg == "--timings") {
      options.timings = true;
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
    options.threads = default_thread_count();
  }
  if (options.threads == 0) {
    throw std::invalid_argument("--threads must be greater than zero");
  }
  int stdout_outputs = 0;
  if (!options.sidecar_only && options.out_path == "-") {
    ++stdout_outputs;
  }
  if (options.grammar_debug_out_path == "-") {
    ++stdout_outputs;
  }
  if (options.loop_tree_out_path == "-") {
    ++stdout_outputs;
  }
  if (stdout_outputs > 1) {
    throw std::invalid_argument("multiple outputs cannot use '-' together");
  }
  if (options.sidecar_only && options.compat_sidecar_out_path.empty() &&
      options.loop_tree_out_path.empty()) {
    throw std::invalid_argument(
        "--sidecar-only requires --compat-sidecar-out or --loop-tree-out");
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

    const traceloom::AscendSQLiteAdapter adapter(cli.source_db);
    traceloom::NativeIr ir;
    const Stopwatch load_watch;
    ir = adapter.load();
    const double load_ms = load_watch.elapsed_ms();
    if (cli.timings) {
      std::cerr << "timing load_ms=" << load_ms << "\n";
    }

    traceloom::NativePipelineOptions pipeline_options;
    pipeline_options.thread_count = cli.threads;
    pipeline_options.partition_config = traceloom::PartitionPlanConfig{4096, 3};
    pipeline_options.candidate_scan_config =
        traceloom::CandidateScanConfig{2, 3};
    pipeline_options.anchor_config.skipped_task_type_symbols = {"CAPTURE_WAIT"};
    pipeline_options.anchor_config.skip_tasks_covered_by_communication_ops =
        true;
    pipeline_options.anchor_config.filter_auxiliary_task_anchors = true;

    traceloom::NativePipelineResult pipeline;
    if (cli.sidecar_only) {
      const Stopwatch anchor_watch;
      traceloom::build_flat_anchors(ir, pipeline_options.anchor_config);
      if (cli.timings) {
        std::cerr << "timing build_anchor_tokens_ms="
                  << anchor_watch.elapsed_ms() << "\n";
      }
    } else {
      const Stopwatch pipeline_watch;
      pipeline = traceloom::run_native_pipeline(ir, pipeline_options);
      if (cli.timings) {
        std::cerr << "timing native_pipeline_ms="
                  << pipeline_watch.elapsed_ms() << "\n";
      }
    }

    traceloom::NativeResultJsonOptions json_options;
    json_options.source_kind = "ascend_sqlite_hot_path";
    json_options.source_path = cli.source_db;
    json_options.thread_count = cli.threads;
    json_options.top_candidate_limit = cli.top_candidate_limit;
    json_options.load_source_adapter_ms = load_ms;

    std::ostringstream first_pass;
    const Stopwatch materialize_watch;
    traceloom::compat::NativeCompatibilitySidecarOptions sidecar_options;
    sidecar_options.source_kind = json_options.source_kind;
    sidecar_options.source_path = json_options.source_path;
    sidecar_options.grammar_worker_count = cli.threads;
    sidecar_options.grammar_target_nodes_per_chunk =
        pipeline_options.partition_config.target_tokens_per_partition;
    sidecar_options.timing_diagnostics = cli.timings;

    if (!cli.compat_sidecar_out_path.empty()) {
      const Stopwatch sidecar_watch;
      traceloom::compat::write_basic_native_compatibility_sidecar(
          cli.compat_sidecar_out_path, ir, sidecar_options);
      if (cli.timings) {
        std::cerr << "timing compat_sidecar_ms="
                  << sidecar_watch.elapsed_ms() << "\n";
      }
    }
    if (!cli.loop_tree_out_path.empty()) {
      traceloom::compat::NativeCompatibilitySidecarOptions loop_tree_options;
      loop_tree_options.source_kind = sidecar_options.source_kind;
      loop_tree_options.source_path = sidecar_options.source_path;
      loop_tree_options.grammar_worker_count =
          sidecar_options.grammar_worker_count;
      loop_tree_options.grammar_target_nodes_per_chunk =
          sidecar_options.grammar_target_nodes_per_chunk;
      loop_tree_options.grammar_full_discovery_cap =
          cli.loop_tree_full_discovery_cap;
      loop_tree_options.materialize_grammar_report_tree =
          cli.loop_tree_grammar;
      loop_tree_options.materialize_aux_attribution = cli.loop_tree_aux;
      loop_tree_options.timing_diagnostics = cli.timings;
      const Stopwatch loop_tree_rows_watch;
      const traceloom::compat::NodeCoverageSqlRows loop_tree_rows =
          traceloom::compat::build_native_loop_tree_node_coverage_rows(
              ir, loop_tree_options);
      if (cli.timings) {
        std::cerr << "timing loop_tree_rows_ms="
                  << loop_tree_rows_watch.elapsed_ms() << "\n";
      }
      traceloom::LoopTreeMarkdownOptions markdown_options;
      markdown_options.db_label = cli.loop_tree_db_label;
      markdown_options.source_kind = json_options.source_kind;
      markdown_options.source_path = json_options.source_path;
      markdown_options.db_idx = sidecar_options.db_idx;
      markdown_options.has_device_id = cli.has_loop_tree_device_id;
      markdown_options.device_id = cli.loop_tree_device_id;
      markdown_options.trace_event_count = ir.trace_events.size();
      markdown_options.anchor_count = ir.anchors.size();
      const Stopwatch loop_tree_render_watch;
      std::ostringstream markdown;
      traceloom::write_loop_tree_markdown(markdown, loop_tree_rows,
                                          markdown_options);
      write_text_output(cli.loop_tree_out_path, markdown.str());
      if (cli.timings) {
        std::cerr << "timing loop_tree_markdown_ms="
                  << loop_tree_render_watch.elapsed_ms() << "\n";
      }
    }
    if (cli.sidecar_only) {
      return 0;
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
    print_usage(argc > 0 ? argv[0] : "traceloom-native-analyze-db");
    return 1;
  }

  return 0;
}
