#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "traceloom/adapters/ascend_sqlite_adapter.h"
#include "traceloom/analysis/idle_evidence_semantic_rules.h"
#include "traceloom/analysis/semantic_task_classifier.h"
#include "traceloom/ir/native_ir.h"

namespace {

struct CliOptions {
  std::string source_db;
  std::string idle_evidence_rules_path;
  std::string out_path = "-";
};

CliOptions parse_args(int argc, char** argv) {
  CliOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--source-db") {
      if (index + 1 >= argc) {
        throw std::invalid_argument("--source-db requires a path");
      }
      options.source_db = argv[++index];
    } else if (arg == "--idle-evidence-rules") {
      if (index + 1 >= argc) {
        throw std::invalid_argument("--idle-evidence-rules requires a path");
      }
      options.idle_evidence_rules_path = argv[++index];
    } else if (arg == "--out") {
      if (index + 1 >= argc) {
        throw std::invalid_argument("--out requires a path");
      }
      options.out_path = argv[++index];
    } else if (arg == "--help" || arg == "-h") {
      std::cerr << "usage: " << argv[0]
                << " --source-db <profiler.sqlite>"
                   " [--idle-evidence-rules PATH] [--out PATH|-]\n"
                << "Classifies every TaskRow with the idle evidence semantic "
                   "ruleset and reports per-role counts and durations plus "
                   "unknown-task detail. A ruleset override that fails to "
                   "load exits non-zero; it never silently falls back.\n";
      std::exit(0);
    } else {
      throw std::invalid_argument("unknown argument: " + arg);
    }
  }
  if (options.source_db.empty()) {
    throw std::invalid_argument("--source-db is required");
  }
  return options;
}

std::string symbol_text(const traceloom::NativeIr& ir,
                        traceloom::SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

}  // namespace

int main(int argc, char** argv) {
  using namespace traceloom;

  try {
    const CliOptions options = parse_args(argc, argv);

    const AscendSQLiteAdapter adapter(options.source_db, "ascend_sqlite");
    const NativeIr ir = adapter.load();

    // An explicit ruleset override that cannot load must fail the command;
    // there is no silent fallback (the loader throws).
    const SemanticTaskRuleset ruleset =
        options.idle_evidence_rules_path.empty()
            ? load_default_idle_evidence_semantic_ruleset()
            : load_idle_evidence_semantic_ruleset(
                  options.idle_evidence_rules_path);

    const SemanticTaskClassificationResult result =
        classify_semantic_tasks(ir, ruleset);
    if (result.rows.size() != ir.tasks.size()) {
      throw std::invalid_argument(
          "classification row count does not match task count");
    }

    // Aggregate per-role counts and durations; collect unknown detail.
    struct RoleStat {
      std::uint64_t count = 0;
      std::int64_t duration_ns = 0;
    };
    std::map<SemanticTaskRole, RoleStat> role_stats;
    struct UnknownStat {
      std::uint64_t count = 0;
      std::int64_t duration_ns = 0;
    };
    std::map<std::string, UnknownStat> unknown_task_types;
    std::map<std::string, UnknownStat> unknown_op_types;

    for (std::size_t index = 0; index < result.rows.size(); ++index) {
      const SemanticTaskClassificationRow& row = result.rows[index];
      const TaskRow& task = ir.tasks.row(TaskId(index));
      const TraceEventRow& event = ir.trace_events.row(row.trace_event_id);
      const std::int64_t duration = event.end_ns - event.start_ns;
      role_stats[row.role].count += 1;
      role_stats[row.role].duration_ns += duration;
      if (row.role == SemanticTaskRole::kUnknown) {
        unknown_task_types[symbol_text(ir, task.task_type_symbol_id)]
            .count += 1;
        unknown_task_types[symbol_text(ir, task.task_type_symbol_id)]
            .duration_ns += duration;
        unknown_op_types[symbol_text(ir, task.op_type_symbol_id)].count += 1;
        unknown_op_types[symbol_text(ir, task.op_type_symbol_id)].duration_ns +=
            duration;
      }
    }

    std::string output;
    output += "# Idle Evidence Semantic Taxonomy Audit\n\n";
    output += "- ruleset_version: " + result.semantic_rules_version + "\n";
    output += "- ruleset_sha256: " + result.semantic_rules_sha256 + "\n";
    output += "- tasks: " + std::to_string(result.rows.size()) + "\n\n";

    output += "## Roles\n\n";
    output += "| role | count | count % | duration_ns | duration % |\n";
    output += "| --- | --- | --- | --- | --- |\n";
    std::uint64_t total_count = 0;
    std::int64_t total_duration = 0;
    for (const auto& [role, stat] : role_stats) {
      total_count += stat.count;
      total_duration += stat.duration_ns;
    }
    const auto pct = [](std::uint64_t part, std::uint64_t whole) {
      return whole == 0 ? 0.0
                        : 100.0 * static_cast<double>(part) /
                              static_cast<double>(whole);
    };
    for (const auto& [role, stat] : role_stats) {
      output += "| " + std::string(semantic_task_role_name(role)) + " | " +
                std::to_string(stat.count) + " | " +
                std::to_string(pct(stat.count, total_count)) + " | " +
                std::to_string(stat.duration_ns) + " | " +
                std::to_string(
                    pct(static_cast<std::uint64_t>(stat.duration_ns),
                        static_cast<std::uint64_t>(total_duration))) +
                " |\n";
    }

    const auto top_unknowns =
        [](const std::map<std::string, UnknownStat>& stats, std::size_t top) {
          std::vector<std::pair<std::string, UnknownStat>> sorted(
              stats.begin(), stats.end());
          std::sort(sorted.begin(), sorted.end(),
                    [](const auto& lhs, const auto& rhs) {
                      return lhs.second.duration_ns > rhs.second.duration_ns;
                    });
          if (sorted.size() > top) {
            sorted.resize(top);
          }
          return sorted;
        };

    output += "\n## Unknown detail\n\n";
    const auto task_types = top_unknowns(unknown_task_types, 10);
    output += "### Top unknown task_type (by duration)\n\n";
    output += "| task_type | count | duration_ns |\n| --- | --- | --- |\n";
    for (const auto& [name, stat] : task_types) {
      output += "| " + (name.empty() ? "(empty)" : name) + " | " +
                std::to_string(stat.count) + " | " +
                std::to_string(stat.duration_ns) + " |\n";
    }
    const auto op_types = top_unknowns(unknown_op_types, 10);
    output += "\n### Top unknown op_type (by duration)\n\n";
    output += "| op_type | count | duration_ns |\n| --- | --- | --- |\n";
    for (const auto& [name, stat] : op_types) {
      output += "| " + (name.empty() ? "(empty)" : name) + " | " +
                std::to_string(stat.count) + " | " +
                std::to_string(stat.duration_ns) + " |\n";
    }

    if (options.out_path == "-") {
      std::cout << output;
    } else {
      std::ofstream out(options.out_path);
      if (!out) {
        throw std::invalid_argument("cannot open output path: " +
                                    options.out_path);
      }
      out << output;
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
