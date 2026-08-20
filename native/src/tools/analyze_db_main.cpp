#include <algorithm>
#include <chrono>
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

#include "profile_input_discovery.h"

#include "traceloom/adapters/ascend_sqlite_adapter.h"
#include "traceloom/adapters/cuda_nsys_sqlite_adapter.h"
#include "traceloom/adapters/hygon_sqlite_adapter.h"
#include "traceloom/analysis/event_reconciliation.h"
#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/compat/structural_projection_rows.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/materialize/grammar_debug_json.h"
#include "traceloom/materialize/loop_tree_markdown.h"
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
  std::string grammar_debug_out_path;
  std::string compat_sidecar_out_path;
  std::string augmented_db_out_path;
  bool augmented_db_enabled = true;
  std::string loop_tree_out_path;
  bool loop_tree_out_path_set = false;
  std::string loop_tree_db_label;
  bool has_loop_tree_device_id = false;
  std::uint32_t loop_tree_device_id = 0;
  std::string loop_tree_view = "compact";
  bool loop_tree_grammar = true;
  bool loop_tree_aux = true;
  std::size_t loop_tree_full_discovery_cap = 5000000;
  bool sidecar_only = false;
  bool timings = false;
  std::size_t threads = 0;
  std::string classification_rules_path;
  std::string extend_classification_rules_path;
  std::vector<std::string> classification_rule_overrides;
  std::string symbol_rules_path;
  std::string extend_symbol_rules_path;
  std::string event_reconciliation_rules_path;
  std::string extend_event_reconciliation_rules_path;
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
            << " <profile.db-or-profile-dir> [--threads N]"
               " [--output ANALYSIS.db]"
               " [--loop-tree-out PATH|-]"
               " [--timings]\n\n"
            << "Writes a self-contained queryable database timeline "
               "under a neighboring traceloom/ directory by default.\n"
            << "Query its traceloom_projection_recipe catalog to select a "
               "scope and compose analytical projections.\n"
            << "Use traceloom_analysis_surface to discover the underlying "
               "hierarchy, cost, replay, and evidence relations.\n"
            << "Use --loop-tree-out only when a Markdown projection is "
               "needed for a human reader. It defaults to a compact grammar "
               "summary; select the exact expanded tree with "
               "--loop-tree-view expanded.\n"
            << "Use --help-advanced for compatibility and debug options.\n";
}

void print_advanced_usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " --source-db <profiler.sqlite-or-db> [--threads N]"
               " [--source-kind auto|ascend_sqlite_hot_path|"
               "ascend_sqlite_split|hygon_sqlite|cuda_nsys_sqlite]"
               " [--grammar-debug-out PATH|-]"
               " [--compat-db-out PATH]"
               " [--output PATH|--aug-db-out PATH|--no-aug-db]"
               " [--loop-tree-out PATH|-]"
               " [--loop-tree-db-label LABEL]"
               " [--loop-tree-device-id N]"
               " [--loop-tree-view compact|expanded|both]"
               " [--loop-tree-grammar|--loop-tree-no-grammar]"
               " [--loop-tree-full-discovery-cap N]"
               " [--loop-tree-aux|--loop-tree-no-aux]"
               " [--classification-rules PATH]"
               " [--extend-classification-rules PATH]"
               " [--classification-rule-override RULE_ID.FIELD=VALUE]"
               " [--symbol-rules PATH]"
               " [--extend-symbol-rules PATH]"
               " [--event-reconciliation-rules PATH]"
               " [--extend-event-reconciliation-rules PATH]"
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
    } else if (arg == "--loop-tree-view") {
      options.loop_tree_view = require_value(arg);
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
    } else if (arg == "--symbol-rules") {
      options.symbol_rules_path = require_value(arg);
    } else if (arg == "--extend-symbol-rules") {
      options.extend_symbol_rules_path = require_value(arg);
    } else if (arg == "--event-reconciliation-rules") {
      options.event_reconciliation_rules_path = require_value(arg);
    } else if (arg == "--extend-event-reconciliation-rules") {
      options.extend_event_reconciliation_rules_path = require_value(arg);
    } else if (arg == "--sidecar-only") {
      options.sidecar_only = true;
    } else if (arg == "--timings") {
      options.timings = true;
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
        "input path is required: pass an Ascend profiler SQLite DB, Hygon "
        "hipprof DB, CUDA/Nsight SQLite export, or profile directory");
  }
  if (options.source_kind != "auto" &&
      options.source_kind != "ascend_sqlite_hot_path" &&
      options.source_kind != "ascend_sqlite_split" &&
      options.source_kind != "hygon_sqlite" &&
      options.source_kind != "cuda_nsys_sqlite") {
    throw std::invalid_argument("unsupported --source-kind: " +
                                options.source_kind);
  }
  if (options.loop_tree_view != "compact" &&
      options.loop_tree_view != "expanded" &&
      options.loop_tree_view != "both") {
    throw std::invalid_argument(
        "unsupported --loop-tree-view: " + options.loop_tree_view);
  }
  options.source_dbs =
      traceloom::tools::discover_profile_dbs(options.source_input,
                                             options.source_kind);
  if (options.source_dbs.empty()) {
    throw std::invalid_argument("no supported Ascend, Hygon, or CUDA/Nsight "
                                "profile DB found under input path: " +
                                options.source_input);
  }
  if (options.source_dbs.size() > 1 &&
      (!options.grammar_debug_out_path.empty() ||
       !options.compat_sidecar_out_path.empty() ||
       !options.augmented_db_out_path.empty() ||
       options.loop_tree_out_path_set)) {
    throw std::invalid_argument(
        "explicit output paths are only supported for a single input DB; pass "
        "one profiler SQLite DB or omit output flags for directory input");
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
  if (!options.augmented_db_enabled && options.grammar_debug_out_path.empty() &&
      options.compat_sidecar_out_path.empty() &&
      !options.loop_tree_out_path_set) {
    throw std::invalid_argument(
        "--no-aug-db requires another explicit output");
  }
  int stdout_outputs = 0;
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

fs::path default_output_root(const std::string& input) {
  const fs::path root(input);
  if (fs::is_regular_file(root)) {
    return root.parent_path() / "traceloom";
  }
  return root / "traceloom";
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
    const std::string input_format =
        traceloom::tools::input_format_for(source_db, source_kind);
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

    constexpr std::size_t kGrammarTargetNodesPerChunk = 4096;
    traceloom::FlatAnchorBuildConfig anchor_config;
    anchor_config.skip_tasks_covered_by_communication_ops = true;
    anchor_config.skip_events_covered_by_replay_units = true;
    anchor_config.filter_auxiliary_task_anchors = true;
    anchor_config.classification_rules =
        cli.classification_rules_path.empty()
            ? traceloom::load_default_signal_classification_ruleset(
                  cli.executable_path)
            : traceloom::load_signal_classification_ruleset(
                  cli.classification_rules_path);
    if (!cli.extend_classification_rules_path.empty()) {
      anchor_config.classification_rules =
          traceloom::extend_signal_classification_ruleset(
              anchor_config.classification_rules,
              traceloom::load_signal_classification_ruleset(
                  cli.extend_classification_rules_path));
    }
    for (const std::string& specification :
         cli.classification_rule_overrides) {
      anchor_config.classification_overrides.push_back(
          traceloom::parse_signal_classification_override(specification));
    }
    if (!anchor_config.classification_overrides.empty()) {
      anchor_config.classification_rules =
          traceloom::override_signal_classification_ruleset(
              anchor_config.classification_rules,
              anchor_config.classification_overrides);
      anchor_config.classification_overrides.clear();
    }
    anchor_config.structural_symbol_rules =
        cli.symbol_rules_path.empty()
            ? traceloom::load_default_structural_symbol_ruleset(
                  cli.executable_path)
            : traceloom::load_structural_symbol_ruleset(
                  cli.symbol_rules_path);
    if (!cli.extend_symbol_rules_path.empty()) {
      anchor_config.structural_symbol_rules =
          traceloom::extend_structural_symbol_ruleset(
              anchor_config.structural_symbol_rules,
              traceloom::load_structural_symbol_ruleset(
                  cli.extend_symbol_rules_path));
    }
    anchor_config.event_reconciliation_rules =
        cli.event_reconciliation_rules_path.empty()
            ? traceloom::load_default_event_reconciliation_ruleset(
                  cli.executable_path)
            : traceloom::load_event_reconciliation_ruleset(
                  cli.event_reconciliation_rules_path);
    if (!cli.extend_event_reconciliation_rules_path.empty()) {
      anchor_config.event_reconciliation_rules =
          traceloom::overlay_event_reconciliation_ruleset(
              anchor_config.event_reconciliation_rules,
              traceloom::load_event_reconciliation_ruleset(
                  cli.extend_event_reconciliation_rules_path));
    }

    const Stopwatch anchor_watch;
    traceloom::build_flat_anchors(ir, anchor_config);
    if (cli.timings) {
      std::cerr << "timing build_anchor_tokens_ms="
                << anchor_watch.elapsed_ms() << "\n";
    }

    traceloom::compat::NativeCompatibilitySidecarOptions sidecar_options;
    sidecar_options.source_kind = source_kind;
    sidecar_options.input_format = input_format;
    sidecar_options.source_path = source_db;
    if (!is_cuda && !is_hygon) {
      const traceloom::AscendProfileEvidenceState evidence =
          traceloom::classify_ascend_profile_evidence(source_db);
      sidecar_options.input_evidence_contract = evidence.contract_version;
      sidecar_options.input_scope = evidence.input_scope;
      sidecar_options.input_evidence_state = evidence.evidence_state;
      sidecar_options.input_missing_components = evidence.missing_components;
    }
    sidecar_options.grammar_worker_count = cli.threads;
    sidecar_options.grammar_target_nodes_per_chunk =
        kGrammarTargetNodesPerChunk;
    sidecar_options.grammar_full_discovery_cap =
        cli.loop_tree_full_discovery_cap;
    sidecar_options.materialize_grammar_structural_projection = cli.loop_tree_grammar;
    sidecar_options.materialize_aux_attribution = cli.loop_tree_aux;
    sidecar_options.timing_diagnostics = cli.timings;
    sidecar_options.evidence_role_policy_id =
        anchor_config.classification_rules.metadata().policy_id;
    sidecar_options.evidence_role_policy_version =
        anchor_config.classification_rules.metadata().policy_version;
    sidecar_options.evidence_role_manifest_sha256 =
        anchor_config.classification_rules.metadata().manifest_sha256;
    sidecar_options.evidence_role_config = anchor_config;

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
      std::cerr << "  input_format: " << input_format << "\n";
      std::cerr << "  start: SELECT * FROM traceloom_projection_recipe "
                   "ORDER BY display_order;\n";
      std::cerr << "  relations: SELECT * FROM traceloom_analysis_surface;\n";
      if (cli.timings) {
        std::cerr << "timing augmented_db_ms="
                  << augmented_db_watch.elapsed_ms() << "\n";
      }
    }
    if (cli.loop_tree_out_path_set) {
      const traceloom::compat::NativeCompatibilitySidecarOptions
          loop_tree_options = sidecar_options;
      const Stopwatch loop_tree_rows_watch;
      const traceloom::compat::NativeLoopTreeReportData loop_tree_report =
          traceloom::compat::build_native_loop_tree_report_data(
              ir, loop_tree_options);
      const traceloom::compat::NodeCoverageSqlRows& loop_tree_rows =
          loop_tree_report.coverage;
      if (cli.timings) {
        std::cerr << "timing loop_tree_rows_ms="
                  << loop_tree_rows_watch.elapsed_ms() << "\n";
      }
      std::set<std::uint32_t> report_device_ids;
      for (const traceloom::compat::VizNodeSqlRow& node :
           loop_tree_rows.nodes) {
        if (node.view_name == "native_report_tree" &&
            node.db_idx == sidecar_options.db_idx) {
          report_device_ids.insert(node.device_id);
        }
      }
      if (report_device_ids.empty()) {
        throw std::runtime_error("no native_report_tree rows to render");
      }

      std::uint32_t render_device_id = *report_device_ids.begin();
      if (cli.has_loop_tree_device_id) {
        if (report_device_ids.find(cli.loop_tree_device_id) ==
            report_device_ids.end()) {
          std::ostringstream available;
          for (auto it = report_device_ids.begin();
               it != report_device_ids.end(); ++it) {
            if (it != report_device_ids.begin()) {
              available << ", ";
            }
            available << *it;
          }
          throw std::runtime_error(
              "no native_report_tree rows for device " +
              std::to_string(cli.loop_tree_device_id) +
              "; available devices: " + available.str());
        }
        render_device_id = cli.loop_tree_device_id;
      } else if (report_device_ids.size() > 1) {
        throw std::runtime_error(
            "profile DB contains multiple devices; select one with "
            "--loop-tree-device-id when --loop-tree-out is given");
      }

      traceloom::LoopTreeMarkdownOptions markdown_options;
      markdown_options.db_label =
          cli.loop_tree_db_label.empty()
              ? default_db_label(source_db, db_index, cli.source_dbs.size())
              : cli.loop_tree_db_label;
      markdown_options.source_kind = source_kind;
      markdown_options.input_format = input_format;
      markdown_options.source_path = source_db;
      markdown_options.input_evidence_contract =
          sidecar_options.input_evidence_contract;
      markdown_options.input_scope = sidecar_options.input_scope;
      markdown_options.input_evidence_state =
          sidecar_options.input_evidence_state;
      markdown_options.input_missing_components =
          sidecar_options.input_missing_components;
      markdown_options.db_idx = sidecar_options.db_idx;
      markdown_options.has_device_id = true;
      markdown_options.device_id = render_device_id;
      markdown_options.trace_event_count = ir.trace_events.size();
      markdown_options.anchor_count = ir.anchors.size();
      markdown_options.replay_composition_region_count =
          ir.replay_composition_regions.size();
      markdown_options.replay_unit_count = ir.replay_units.size();
      if (cli.loop_tree_view == "compact") {
        markdown_options.view = traceloom::LoopTreeMarkdownView::kCompact;
      } else if (cli.loop_tree_view == "expanded") {
        markdown_options.view = traceloom::LoopTreeMarkdownView::kExpanded;
      } else {
        markdown_options.view = traceloom::LoopTreeMarkdownView::kBoth;
      }
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
      const traceloom::compat::NativeCompactGrammarProjection*
          compact_grammar = nullptr;
      for (const auto& candidate : loop_tree_report.compact_grammars) {
        if (candidate.device_id == render_device_id) {
          compact_grammar = &candidate;
          break;
        }
      }
      traceloom::write_loop_tree_markdown(markdown, loop_tree_rows,
                                          markdown_options, compact_grammar);
      write_text_output(cli.loop_tree_out_path, markdown.str());
      if (cli.loop_tree_out_path != "-") {
        std::cerr << "wrote loop tree: " << cli.loop_tree_out_path << "\n";
        std::cerr << "  source_db: " << source_db << "\n";
        std::cerr << "  input_format: " << input_format << "\n";
        std::cerr << "  human_view: " << cli.loop_tree_view << "\n";
        std::cerr << "  device_id: " << render_device_id << "\n";
      }
      if (cli.timings) {
        std::cerr << "timing loop_tree_markdown_ms="
                  << loop_tree_render_watch.elapsed_ms() << "\n";
      }
    }
    if (!cli.grammar_debug_out_path.empty()) {
      traceloom::GrammarStateConfig grammar_state_config;
      grammar_state_config.target_nodes_per_chunk =
          kGrammarTargetNodesPerChunk;
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
