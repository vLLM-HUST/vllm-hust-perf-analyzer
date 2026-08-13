#include <chrono>
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "traceloom/adapters/ascend_sqlite_adapter.h"
#include "traceloom/adapters/cuda_nsys_sqlite_adapter.h"
#include "traceloom/adapters/hygon_sqlite_adapter.h"
#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/analysis/native_pipeline.h"
#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/compat/report_tree_rows.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/materialize/grammar_debug_json.h"
#include "traceloom/materialize/loop_tree_markdown.h"
#include "traceloom/materialize/native_result_json.h"
#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/pattern/grammar_state.h"

#ifndef TRACELOOM_NATIVE_VERSION
#define TRACELOOM_NATIVE_VERSION "dev"
#endif

namespace {

namespace fs = std::filesystem;

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
  std::string executable_path;
  std::string source_input;
  std::string source_kind = "auto";
  std::vector<std::string> source_dbs;
  std::string out_path;
  bool out_path_set = false;
  std::string grammar_debug_out_path;
  std::string compat_sidecar_out_path;
  std::string augmented_db_out_path;
  bool augmented_db_enabled = true;
  std::string loop_tree_out_path;
  bool loop_tree_out_path_set = false;
  std::string loop_tree_db_label;
  bool has_loop_tree_device_id = false;
  std::uint32_t loop_tree_device_id = 0;
  bool loop_tree_grammar = true;
  bool loop_tree_aux = true;
  std::size_t loop_tree_full_discovery_cap = 5000000;
  bool sidecar_only = false;
  bool timings = false;
  std::size_t threads = 0;
  std::size_t top_candidate_limit = 16;
  std::string classification_rules_path;
  std::string extend_classification_rules_path;
  std::vector<std::string> classification_rule_overrides;
};

std::vector<std::string> discover_profile_dbs(const std::string& input,
                                               const std::string& source_kind);

std::size_t default_thread_count() {
  const unsigned int hardware = std::thread::hardware_concurrency();
  if (hardware <= 1) {
    return 1;
  }
  return std::max<std::size_t>(1, static_cast<std::size_t>(hardware / 2));
}

void print_usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " <profile.db-or-profile-dir> [--threads N]"
               " [--output ANALYSIS.db]"
               " [--loop-tree-out PATH|-]"
               " [--timings]\n\n"
            << "Writes a self-contained queryable database timeline "
               "under a neighboring traceloom/ directory by default.\n"
            << "Query its traceloom_analysis_surface catalog to discover "
               "hierarchy, cost, replay, and evidence relations.\n"
            << "Use --loop-tree-out only when a Markdown projection is "
               "needed for a human reader.\n"
            << "Use --help-advanced for compatibility and debug options.\n";
}

void print_advanced_usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " --source-db <profiler.sqlite-or-db> [--threads N]"
               " [--source-kind auto|ascend_sqlite_hot_path|"
               "ascend_sqlite_split|hygon_sqlite|cuda_nsys_sqlite]"
               " [--out PATH|-] [--top-candidates N]"
               " [--grammar-debug-out PATH|-]"
               " [--compat-db-out PATH]"
               " [--output PATH|--aug-db-out PATH|--no-aug-db]"
               " [--loop-tree-out PATH|-]"
               " [--loop-tree-db-label LABEL]"
               " [--loop-tree-device-id N]"
               " [--loop-tree-grammar|--loop-tree-no-grammar]"
               " [--loop-tree-full-discovery-cap N]"
               " [--loop-tree-aux|--loop-tree-no-aux]"
               " [--classification-rules PATH]"
               " [--extend-classification-rules PATH]"
               " [--classification-rule-override RULE_ID.FIELD=VALUE]"
               " [--timings]\n";
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
  options.executable_path = argc > 0 ? argv[0] : "traceloom";
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    auto require_value = [&](const std::string& flag) -> std::string {
      if (index + 1 >= argc) {
        throw std::invalid_argument("missing value for " + flag);
      }
      ++index;
      return argv[index];
    };

    if (arg == "analyze" && options.source_input.empty()) {
      std::cerr << "warning: 'traceloom analyze <path>' is deprecated; use "
                   "'traceloom <path>'\n";
    } else if (arg == "--source-db") {
      options.source_input = require_value(arg);
    } else if (arg == "--source-kind") {
      options.source_kind = require_value(arg);
    } else if (arg == "--threads") {
      options.threads = parse_size(require_value(arg), arg);
    } else if (arg == "--out") {
      options.out_path = require_value(arg);
      options.out_path_set = true;
    } else if (arg == "--grammar-debug-out") {
      options.grammar_debug_out_path = require_value(arg);
    } else if (arg == "--compat-db-out" || arg == "--compat-sidecar-out") {
      options.compat_sidecar_out_path = require_value(arg);
    } else if (arg == "--output" || arg == "--aug-db-out") {
      options.augmented_db_out_path = require_value(arg);
      options.augmented_db_enabled = true;
    } else if (arg == "--no-aug-db") {
      options.augmented_db_enabled = false;
    } else if (arg == "--loop-tree-out") {
      options.loop_tree_out_path = require_value(arg);
      options.loop_tree_out_path_set = true;
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
    } else if (arg == "--loop-tree-no-aux") {
      options.loop_tree_aux = false;
    } else if (arg == "--classification-rules") {
      options.classification_rules_path = require_value(arg);
    } else if (arg == "--extend-classification-rules") {
      options.extend_classification_rules_path = require_value(arg);
    } else if (arg == "--classification-rule-override") {
      options.classification_rule_overrides.push_back(require_value(arg));
    } else if (arg == "--sidecar-only") {
      options.sidecar_only = true;
    } else if (arg == "--timings") {
      options.timings = true;
    } else if (arg == "--top-candidates") {
      options.top_candidate_limit = parse_size(require_value(arg), arg);
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argc > 0 ? argv[0] : "traceloom");
      std::exit(0);
    } else if (arg == "--help-advanced") {
      print_advanced_usage(argc > 0 ? argv[0] : "traceloom");
      std::exit(0);
    } else if (arg == "--version" || arg == "-V") {
      std::cout << "traceloom " << TRACELOOM_NATIVE_VERSION << '\n';
      std::exit(0);
    } else if (!arg.empty() && arg[0] != '-' && options.source_input.empty()) {
      options.source_input = arg;
    } else {
      throw std::invalid_argument("unknown argument: " + arg);
    }
  }

  if (options.source_input.empty()) {
    throw std::invalid_argument(
        "input path is required: pass an msprof_*.db, Hygon hipprof DB, "
        "CUDA/Nsight SQLite export, or profile directory");
  }
  if (options.source_kind != "auto" &&
      options.source_kind != "ascend_sqlite_hot_path" &&
      options.source_kind != "ascend_sqlite_split" &&
      options.source_kind != "hygon_sqlite" &&
      options.source_kind != "cuda_nsys_sqlite") {
    throw std::invalid_argument("unsupported --source-kind: " +
                                options.source_kind);
  }
  options.source_dbs =
      discover_profile_dbs(options.source_input, options.source_kind);
  if (options.source_dbs.empty()) {
    throw std::invalid_argument("no supported msprof, Hygon, or CUDA/Nsight "
                                "profile DB found under input path: " +
                                options.source_input);
  }
  if (options.source_dbs.size() > 1 &&
      (options.out_path_set || !options.grammar_debug_out_path.empty() ||
       !options.compat_sidecar_out_path.empty() ||
       !options.augmented_db_out_path.empty() ||
       options.loop_tree_out_path_set)) {
    throw std::invalid_argument(
        "explicit output paths are only supported for a single input DB; pass "
        "one msprof_*.db or omit output flags for directory input");
  }
  if (options.threads == 0) {
    options.threads = default_thread_count();
  }
  if (options.threads == 0) {
    throw std::invalid_argument("--threads must be greater than zero");
  }
  if (!options.augmented_db_enabled &&
      !options.augmented_db_out_path.empty()) {
    throw std::invalid_argument(
        "--no-aug-db cannot be combined with --output/--aug-db-out");
  }
  if (options.augmented_db_out_path == "-") {
    throw std::invalid_argument("SQLite analysis output cannot use '-'");
  }
  if (!options.augmented_db_enabled && !options.out_path_set &&
      options.grammar_debug_out_path.empty() &&
      options.compat_sidecar_out_path.empty() &&
      !options.loop_tree_out_path_set) {
    throw std::invalid_argument(
        "--no-aug-db requires another explicit output");
  }
  int stdout_outputs = 0;
  if (!options.sidecar_only && options.out_path_set && options.out_path == "-") {
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
      !options.augmented_db_enabled &&
      !options.loop_tree_out_path_set) {
    throw std::invalid_argument(
        "--sidecar-only requires database output, --compat-db-out, or "
        "--loop-tree-out");
  }
  return options;
}

bool looks_like_msprof_db(const fs::path& path) {
  if (!fs::is_regular_file(path)) {
    return false;
  }
  const std::string name = path.filename().string();
  return name.rfind("msprof_", 0) == 0 && path.extension() == ".db";
}

bool has_sqlite_profile_extension(const fs::path& path) {
  const std::string extension = path.extension().string();
  return extension == ".db" || extension == ".sqlite" ||
         extension == ".sqlite3";
}

bool looks_like_supported_profile_db(const fs::path& path,
                                     const std::string& source_kind) {
  if (!fs::is_regular_file(path) || !has_sqlite_profile_extension(path)) {
    return false;
  }
  if (source_kind == "cuda_nsys_sqlite") {
    return traceloom::looks_like_cuda_nsys_sqlite_profile(path.string());
  }
  if (source_kind == "hygon_sqlite") {
    return traceloom::looks_like_hygon_sqlite_profile(path.string());
  }
  if (source_kind == "ascend_sqlite_hot_path") {
    return looks_like_msprof_db(path);
  }
  if (source_kind == "ascend_sqlite_split") {
    return false;
  }
  if (traceloom::looks_like_cuda_nsys_sqlite_profile(path.string())) {
    return true;
  }
  if (looks_like_msprof_db(path)) {
    return traceloom::ascend_sqlite_has_usable_task_table(path.string());
  }
  return traceloom::looks_like_hygon_sqlite_profile(path.string());
}

std::vector<std::string> discover_profile_dbs(const std::string& input,
                                               const std::string& source_kind) {
  const fs::path root(input);
  std::vector<fs::path> dbs;
  std::vector<fs::path> split_profiles;
  std::error_code ec;
  const bool allow_split_profiles =
      source_kind == "auto" || source_kind == "ascend_sqlite_split";
  if (looks_like_supported_profile_db(root, source_kind)) {
    dbs.push_back(root);
  } else if (allow_split_profiles && looks_like_msprof_db(root)) {
    const fs::path profile_dir = root.parent_path();
    if (traceloom::looks_like_ascend_split_sqlite_profile(
            profile_dir.string())) {
      split_profiles.push_back(profile_dir);
    }
  } else if (fs::is_directory(root, ec)) {
    if (allow_split_profiles &&
        traceloom::looks_like_ascend_split_sqlite_profile(root.string())) {
      split_profiles.push_back(root);
    }
    for (fs::recursive_directory_iterator iterator(root), end;
         iterator != end; ++iterator) {
      const auto& entry = *iterator;
      if (entry.is_directory() && entry.path().filename() == "traceloom") {
        iterator.disable_recursion_pending();
        continue;
      }
      if (looks_like_supported_profile_db(entry.path(), source_kind)) {
        dbs.push_back(entry.path());
      }
      if (allow_split_profiles && entry.is_directory() &&
          fs::is_directory(entry.path() / "host" / "sqlite", ec) &&
          traceloom::looks_like_ascend_split_sqlite_profile(
              entry.path().string())) {
        split_profiles.push_back(entry.path());
      }
    }
  } else if (!fs::exists(root, ec)) {
    throw std::invalid_argument("input path does not exist: " + input);
  } else {
    throw std::invalid_argument(
        "input is not a supported msprof/Hygon/CUDA profile DB or directory: " +
        input);
  }
  std::sort(dbs.begin(), dbs.end());
  std::sort(split_profiles.begin(), split_profiles.end());
  split_profiles.erase(
      std::unique(split_profiles.begin(), split_profiles.end()),
      split_profiles.end());
  for (const fs::path& profile : split_profiles) {
    const bool has_usable_monolithic =
        std::any_of(dbs.begin(), dbs.end(), [&](const fs::path& db) {
          return db.parent_path() == profile;
        });
    if (!has_usable_monolithic) {
      dbs.push_back(profile);
    }
  }
  std::sort(dbs.begin(), dbs.end());
  std::vector<std::string> result;
  result.reserve(dbs.size());
  for (const auto& db : dbs) {
    result.push_back(db.string());
  }
  return result;
}

fs::path default_output_root(const std::string& input) {
  const fs::path root(input);
  if (fs::is_regular_file(root)) {
    return root.parent_path() / "traceloom";
  }
  return root / "traceloom";
}

std::string default_loop_tree_output_path(const CliOptions& cli,
                                          std::size_t db_index,
                                          bool has_device_id,
                                          std::uint32_t device_id) {
  const fs::path output_root = default_output_root(cli.source_input);
  if (cli.source_dbs.size() == 1) {
    return (output_root / "loop_tree_v2.md").string();
  }
  std::ostringstream filename;
  if (has_device_id) {
    filename << "device" << device_id << "_loop_tree_v2.md";
  } else {
    filename << "db" << std::setw(2) << std::setfill('0') << (db_index + 1)
             << "_loop_tree_v2.md";
  }
  return (output_root / filename.str()).string();
}

std::string default_augmented_db_output_path(const CliOptions& cli,
                                             std::size_t db_index) {
  const fs::path output_root = default_output_root(cli.source_input);
  if (cli.source_dbs.size() == 1) {
    return (output_root / "analysis.db").string();
  }
  std::ostringstream filename;
  filename << "analysis_db" << std::setw(2) << std::setfill('0')
           << (db_index + 1) << ".db";
  return (output_root / filename.str()).string();
}

std::vector<std::string> raw_sqlite_sources(const std::string& source_db,
                                            bool is_split) {
  if (!is_split) {
    return {source_db};
  }
  std::set<std::string> paths;
  for (const auto& table :
       traceloom::inventory_ascend_split_sqlite_profile(source_db)) {
    paths.insert(table.db_path);
  }
  if (paths.empty()) {
    throw std::invalid_argument(
        "split profile contains no raw SQLite databases: " + source_db);
  }
  return {paths.begin(), paths.end()};
}

std::string default_db_label(const std::string& source_db,
                             std::size_t db_index,
                             std::size_t db_count) {
  if (db_count == 1) {
    const fs::path source(source_db);
    return fs::is_directory(source) ? source.filename().string()
                                    : source.parent_path().filename().string();
  }
  std::ostringstream label;
  const fs::path source(source_db);
  label << "db" << std::setw(2) << std::setfill('0') << (db_index + 1) << "_"
        << (fs::is_directory(source) ? source.filename().string()
                                     : source.parent_path().filename().string());
  return label.str();
}

bool infer_single_device_id(const traceloom::NativeIr& ir,
                            std::uint32_t& device_id) {
  bool found = false;
  for (const auto& event : ir.trace_events.rows()) {
    if (!found) {
      device_id = event.device_id;
      found = true;
      continue;
    }
    if (device_id != event.device_id) {
      return false;
    }
  }
  return found;
}

void write_text_output(const std::string& path, const std::string& contents) {
  if (path == "-") {
    std::cout << contents;
    return;
  }
  const fs::path output_path(path);
  if (output_path.has_parent_path()) {
    std::error_code ec;
    fs::create_directories(output_path.parent_path(), ec);
    if (ec) {
      throw std::runtime_error("failed to create output directory: " +
                               output_path.parent_path().string() + ": " +
                               ec.message());
    }
  }
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("failed to open output path: " + path);
  }
  out << contents;
}

int analyze_one_db(const CliOptions& cli, const std::string& source_db,
                   std::size_t db_index) {
  const bool report_only =
      cli.sidecar_only ||
      (!cli.out_path_set && cli.grammar_debug_out_path.empty());

  const bool is_cuda =
      cli.source_kind == "cuda_nsys_sqlite" ||
      (cli.source_kind == "auto" &&
       traceloom::looks_like_cuda_nsys_sqlite_profile(source_db));
  const bool is_hygon =
      cli.source_kind == "hygon_sqlite" ||
      (cli.source_kind == "auto" && !is_cuda &&
       traceloom::looks_like_hygon_sqlite_profile(source_db));
  const bool is_split = !is_cuda && !is_hygon && fs::is_directory(source_db);
  const std::string source_kind =
      is_cuda ? "cuda_nsys_sqlite"
              : (is_hygon ? "hygon_sqlite"
                          : (is_split ? "ascend_sqlite_split"
                                      : "ascend_sqlite_hot_path"));
    traceloom::NativeIr ir;
    const Stopwatch load_watch;
    if (is_cuda) {
      traceloom::CudaNsightSQLiteAdapterOptions adapter_options;
      adapter_options.db_path = source_db;
      adapter_options.thread_count = cli.threads;
      adapter_options.timing_diagnostics = cli.timings;
      const traceloom::CudaNsightSQLiteAdapter adapter(
          std::move(adapter_options));
      ir = adapter.load();
    } else if (is_hygon) {
      traceloom::HygonSQLiteAdapterOptions adapter_options;
      adapter_options.db_path = source_db;
      adapter_options.thread_count = cli.threads;
      adapter_options.timing_diagnostics = cli.timings;
      const traceloom::HygonSQLiteAdapter adapter(std::move(adapter_options));
      ir = adapter.load();
    } else {
      traceloom::AscendSQLiteAdapterOptions adapter_options;
      adapter_options.db_path = source_db;
      adapter_options.source_kind = source_kind;
      adapter_options.thread_count = cli.threads;
      adapter_options.timing_diagnostics = cli.timings;
      const traceloom::AscendSQLiteAdapter adapter(std::move(adapter_options));
      ir = adapter.load();
    }
    const double load_ms = load_watch.elapsed_ms();
    if (cli.timings) {
      std::cerr << "timing load_ms=" << load_ms << "\n";
      std::cerr << "timing source_kind=" << source_kind << "\n";
    }

    traceloom::NativePipelineOptions pipeline_options;
    pipeline_options.thread_count = cli.threads;
    pipeline_options.partition_config = traceloom::PartitionPlanConfig{4096, 3};
    pipeline_options.candidate_scan_config =
        traceloom::CandidateScanConfig{2, 3};
    pipeline_options.anchor_config.skip_tasks_covered_by_communication_ops =
        true;
    pipeline_options.anchor_config.skip_events_covered_by_replay_units = true;
    pipeline_options.anchor_config.filter_auxiliary_task_anchors = true;
    pipeline_options.anchor_config.classification_rules =
        cli.classification_rules_path.empty()
            ? traceloom::load_default_signal_classification_ruleset(
                  cli.executable_path)
            : traceloom::load_signal_classification_ruleset(
                  cli.classification_rules_path);
    if (!cli.extend_classification_rules_path.empty()) {
      pipeline_options.anchor_config.classification_rules =
          traceloom::extend_signal_classification_ruleset(
              pipeline_options.anchor_config.classification_rules,
              traceloom::load_signal_classification_ruleset(
                  cli.extend_classification_rules_path));
    }
    for (const std::string& specification :
         cli.classification_rule_overrides) {
      pipeline_options.anchor_config.classification_overrides.push_back(
          traceloom::parse_signal_classification_override(specification));
    }
    if (!pipeline_options.anchor_config.classification_overrides.empty()) {
      pipeline_options.anchor_config.classification_rules =
          traceloom::override_signal_classification_ruleset(
              pipeline_options.anchor_config.classification_rules,
              pipeline_options.anchor_config.classification_overrides);
      pipeline_options.anchor_config.classification_overrides.clear();
    }

    traceloom::NativePipelineResult pipeline;
    if (report_only) {
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
    json_options.source_kind = source_kind;
    json_options.source_path = source_db;
    json_options.thread_count = cli.threads;
    json_options.top_candidate_limit = cli.top_candidate_limit;
    json_options.load_source_adapter_ms = load_ms;
    json_options.native_ir = &ir;

    std::ostringstream first_pass;
    const Stopwatch materialize_watch;
    traceloom::compat::NativeCompatibilitySidecarOptions sidecar_options;
    sidecar_options.source_kind = json_options.source_kind;
    sidecar_options.source_path = json_options.source_path;
    sidecar_options.grammar_worker_count = cli.threads;
    sidecar_options.grammar_target_nodes_per_chunk =
        pipeline_options.partition_config.target_tokens_per_partition;
    sidecar_options.grammar_full_discovery_cap =
        cli.loop_tree_full_discovery_cap;
    sidecar_options.materialize_grammar_report_tree = cli.loop_tree_grammar;
    sidecar_options.materialize_aux_attribution = cli.loop_tree_aux;
    sidecar_options.timing_diagnostics = cli.timings;
    sidecar_options.evidence_role_policy_id =
        pipeline_options.anchor_config.classification_rules.metadata().policy_id;
    sidecar_options.evidence_role_policy_version =
        pipeline_options.anchor_config.classification_rules.metadata()
            .policy_version;
    sidecar_options.evidence_role_manifest_sha256 =
        pipeline_options.anchor_config.classification_rules.metadata()
            .manifest_sha256;
    sidecar_options.evidence_role_config = pipeline_options.anchor_config;

    if (!cli.compat_sidecar_out_path.empty()) {
      const Stopwatch sidecar_watch;
      traceloom::compat::write_basic_native_compatibility_sidecar(
          cli.compat_sidecar_out_path, ir, sidecar_options);
      if (cli.timings) {
        std::cerr << "timing compat_db_ms="
                  << sidecar_watch.elapsed_ms() << "\n";
      }
    }
    if (cli.augmented_db_enabled) {
      const Stopwatch augmented_db_watch;
      const std::string augmented_db_out =
          cli.augmented_db_out_path.empty()
              ? default_augmented_db_output_path(cli, db_index)
              : cli.augmented_db_out_path;
      traceloom::compat::write_queryable_database_timeline(
          augmented_db_out, raw_sqlite_sources(source_db, is_split), ir,
          sidecar_options);
      std::cerr << "wrote queryable database timeline: " << augmented_db_out << "\n";
      std::cerr << "  source: " << source_db << "\n";
      std::cerr << "  start: SELECT * FROM traceloom_analysis_surface;\n";
      if (cli.timings) {
        std::cerr << "timing augmented_db_ms="
                  << augmented_db_watch.elapsed_ms() << "\n";
      }
    }
    if (cli.loop_tree_out_path_set) {
      const traceloom::compat::NativeCompatibilitySidecarOptions
          loop_tree_options = sidecar_options;
      const Stopwatch loop_tree_rows_watch;
      const traceloom::compat::NodeCoverageSqlRows loop_tree_rows =
          traceloom::compat::build_native_loop_tree_node_coverage_rows(
              ir, loop_tree_options);
      if (cli.timings) {
        std::cerr << "timing loop_tree_rows_ms="
                  << loop_tree_rows_watch.elapsed_ms() << "\n";
      }
      std::uint32_t inferred_device_id = 0;
      const bool has_inferred_device_id =
          infer_single_device_id(ir, inferred_device_id);
      traceloom::LoopTreeMarkdownOptions markdown_options;
      markdown_options.db_label =
          cli.loop_tree_db_label.empty()
              ? default_db_label(source_db, db_index, cli.source_dbs.size())
              : cli.loop_tree_db_label;
      markdown_options.source_kind = json_options.source_kind;
      markdown_options.source_path = json_options.source_path;
      markdown_options.db_idx = sidecar_options.db_idx;
      markdown_options.has_device_id = cli.has_loop_tree_device_id;
      markdown_options.device_id = cli.loop_tree_device_id;
      markdown_options.trace_event_count = ir.trace_events.size();
      markdown_options.anchor_count = ir.anchors.size();
      markdown_options.replay_composition_region_count =
          ir.replay_composition_regions.size();
      markdown_options.replay_unit_count = ir.replay_units.size();
      std::map<std::string, std::uint64_t> reconstruction_status_counts;
      for (const traceloom::ReplayCompositionRegionRow& region :
           ir.replay_composition_regions.rows()) {
        const std::string status =
            traceloom::replay_composition_region_status_name(region.status);
        ++reconstruction_status_counts[status];
        if (region.status == traceloom::ReplayCompositionRegionStatus::
                                 kRecognizedCompletePattern) {
          ++markdown_options.recognized_replay_composition_region_count;
        } else {
          ++markdown_options.unrecognized_replay_composition_region_count;
        }
      }
      for (const traceloom::ReplayUnitRow& unit : ir.replay_units.rows()) {
        if (unit.replay_composition_region_id.valid()) {
          ++markdown_options.exact_replay_unit_count;
        }
      }
      for (const auto& item : reconstruction_status_counts) {
        markdown_options.reconstruction_status_counts.push_back(
            traceloom::ReconstructionStatusCount{item.first, item.second});
      }
      const Stopwatch loop_tree_render_watch;
      std::ostringstream markdown;
      traceloom::write_loop_tree_markdown(markdown, loop_tree_rows,
                                          markdown_options);
      const std::string loop_tree_out =
          cli.loop_tree_out_path.empty()
              ? default_loop_tree_output_path(cli, db_index,
                                              has_inferred_device_id,
                                              inferred_device_id)
              : cli.loop_tree_out_path;
      write_text_output(loop_tree_out, markdown.str());
      if (cli.loop_tree_out_path != "-") {
        std::cerr << "wrote loop tree: " << loop_tree_out << "\n";
        std::cerr << "  source_db: " << source_db << "\n";
        if (has_inferred_device_id) {
          std::cerr << "  device_id: " << inferred_device_id << "\n";
        }
      }
      if (cli.timings) {
        std::cerr << "timing loop_tree_markdown_ms="
                  << loop_tree_render_watch.elapsed_ms() << "\n";
      }
    }
    if (report_only) {
      return 0;
    }
    traceloom::write_native_result_json(first_pass, ir.symbols, pipeline,
                                        json_options);
    json_options.materialization_ms = materialize_watch.elapsed_ms();

    std::ostringstream final_json;
    traceloom::write_native_result_json(final_json, ir.symbols, pipeline,
                                        json_options);

    if (cli.out_path_set) {
      write_text_output(cli.out_path, final_json.str());
    }

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
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const CliOptions cli = parse_args(argc, argv);
    for (std::size_t index = 0; index < cli.source_dbs.size(); ++index) {
      analyze_one_db(cli, cli.source_dbs[index], index);
    }
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << "\n";
    print_usage(argc > 0 ? argv[0] : "traceloom");
    return 1;
  }

  return 0;
}
