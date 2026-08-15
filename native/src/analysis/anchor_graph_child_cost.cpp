#include "traceloom/analysis/anchor_graph_child_cost.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace traceloom {

namespace {

constexpr const char* kPartialOverlapFlag = "partial_overlap_diagnostic_only";

struct WindowAccum {
  std::int64_t duration_ns = 0;
  std::uint32_t raw_child_task_count = 0;
  std::uint32_t source_ref_count = 0;
  std::map<std::string, std::int64_t> op_duration_ns;
  std::string diagnostic_flags;
};

void add_diagnostic(AnchorGraphChildCostResult& out,
                    DiagnosticSeverity severity,
                    std::string code,
                    std::string message) {
  out.diagnostics.push_back(Diagnostic{
      severity,
      std::move(code),
      std::move(message),
  });
}

bool has_errors(const AnchorGraphChildCostResult& out) {
  return std::any_of(out.diagnostics.begin(), out.diagnostics.end(),
                     [](const Diagnostic& diagnostic) {
                       return diagnostic.severity == DiagnosticSeverity::kError;
                     });
}

bool contains(const AnchorGraphChildWindow& window,
              const AnchorGraphChildTask& task) {
  return task.stream_id == window.stream_id && task.start_ns >= window.start_ns &&
         task.end_ns <= window.end_ns;
}

bool overlaps(const AnchorGraphChildWindow& window,
              const AnchorGraphChildTask& task) {
  return task.stream_id == window.stream_id && task.start_ns < window.end_ns &&
         task.end_ns > window.start_ns;
}

void append_flag(std::string& target, const std::string& value) {
  if (value.empty()) {
    return;
  }
  std::size_t pos = 0;
  while (pos <= target.size()) {
    const std::size_t end = target.find(';', pos);
    const std::string current =
        target.substr(pos, end == std::string::npos ? end : end - pos);
    if (current == value) {
      return;
    }
    if (end == std::string::npos) {
      break;
    }
    pos = end + 1;
  }
  if (!target.empty()) {
    target += ";";
  }
  target += value;
}

std::string format_top_ops(const std::map<std::string, std::int64_t>& totals,
                           std::uint32_t max_top_ops) {
  if (max_top_ops == 0 || totals.empty()) {
    return {};
  }

  std::vector<std::pair<std::string, std::int64_t>> sorted(totals.begin(),
                                                           totals.end());
  std::sort(sorted.begin(), sorted.end(),
            [](const auto& lhs, const auto& rhs) {
              if (lhs.second != rhs.second) {
                return lhs.second > rhs.second;
              }
              return lhs.first < rhs.first;
            });

  std::string out;
  const std::size_t limit =
      std::min<std::size_t>(sorted.size(), max_top_ops);
  for (std::size_t i = 0; i < limit; ++i) {
    if (!out.empty()) {
      out += ";";
    }
    out += sorted[i].first;
    out += ":";
    out += std::to_string(sorted[i].second);
  }
  return out;
}

}  // namespace

AnchorGraphChildCostResult build_anchor_graph_child_cost_components(
    const std::vector<AnchorGraphChildWindow>& windows,
    const std::vector<AnchorGraphChildTask>& tasks,
    const AnchorGraphChildCostConfig& config) {
  AnchorGraphChildCostResult out;

  if (!config.first_leaf_id.valid()) {
    add_diagnostic(out, DiagnosticSeverity::kError,
                   "anchor_graph_child_invalid_first_leaf_id",
                   "first graph-child component leaf id must be valid");
  }

  for (const AnchorGraphChildWindow& window : windows) {
    if (window.end_ns < window.start_ns) {
      add_diagnostic(out, DiagnosticSeverity::kError,
                     "anchor_graph_child_invalid_window_range",
                     "graph-child window end_ns cannot be smaller than "
                     "start_ns");
    }
  }
  for (const AnchorGraphChildTask& task : tasks) {
    if (task.end_ns < task.start_ns) {
      add_diagnostic(out, DiagnosticSeverity::kError,
                     "anchor_graph_child_invalid_task_range",
                     "graph-child task end_ns cannot be smaller than start_ns");
    }
    if (task.duration_ns < 0) {
      add_diagnostic(out, DiagnosticSeverity::kError,
                     "anchor_graph_child_negative_task_duration",
                     "graph-child task duration cannot be negative");
    }
  }
  if (has_errors(out)) {
    return out;
  }

  std::vector<WindowAccum> accum(windows.size());
  for (std::size_t i = 0; i < windows.size(); ++i) {
    append_flag(accum[i].diagnostic_flags, windows[i].diagnostic_flags);
  }

  const std::size_t unowned = std::numeric_limits<std::size_t>::max();
  std::vector<std::size_t> task_owner(tasks.size(), unowned);

  for (std::size_t task_index = 0; task_index < tasks.size(); ++task_index) {
    const AnchorGraphChildTask& task = tasks[task_index];
    for (std::size_t window_index = 0; window_index < windows.size();
         ++window_index) {
      const AnchorGraphChildWindow& window = windows[window_index];
      if (contains(window, task)) {
        if (task_owner[task_index] != unowned) {
          add_diagnostic(out, DiagnosticSeverity::kError,
                         "anchor_graph_child_duplicate_task_owner",
                         "graph-child task is fully contained by more than "
                         "one anchor window");
          continue;
        }
        task_owner[task_index] = window_index;
      } else if (overlaps(window, task) &&
                 config.diagnostic_only_partial_overlap) {
        add_diagnostic(out, DiagnosticSeverity::kWarning,
                       "anchor_graph_child_partial_overlap",
                       "graph-child task partially overlaps an anchor window "
                       "and is not counted");
        append_flag(accum[window_index].diagnostic_flags, kPartialOverlapFlag);
      }
    }
  }

  if (has_errors(out)) {
    out.component_leaves.clear();
    return out;
  }

  for (std::size_t task_index = 0; task_index < tasks.size(); ++task_index) {
    const std::size_t window_index = task_owner[task_index];
    if (window_index == unowned) {
      continue;
    }
    const AnchorGraphChildTask& task = tasks[task_index];
    WindowAccum& window = accum[window_index];
    window.duration_ns += task.duration_ns;
    window.raw_child_task_count += 1;
    if (task.source_ref_id.valid()) {
      window.source_ref_count += 1;
    }
    const std::string op = task.op.empty() ? "<unknown>" : task.op;
    window.op_duration_ns[op] += task.duration_ns;
  }

  out.component_leaves.reserve(windows.size());
  for (std::size_t i = 0; i < windows.size(); ++i) {
    const WindowAccum& window = accum[i];
    if (!config.emit_zero_duration_windows && window.duration_ns == 0 &&
        window.diagnostic_flags.empty()) {
      continue;
    }

    AnchorCostComponentLeaf leaf;
    leaf.id = StructuralCostLeafId(
        config.first_leaf_id.value() +
        static_cast<StructuralCostLeafId::value_type>(out.component_leaves.size()));
    leaf.token_ordinal = windows[i].token_ordinal;
    leaf.kind = AnchorCostComponentKind::kGraphChild;
    leaf.duration_ns = window.duration_ns;
    leaf.raw_child_task_count = window.raw_child_task_count;
    leaf.source_ref_count = window.source_ref_count;
    leaf.top_ops = format_top_ops(window.op_duration_ns, config.max_top_ops);
    leaf.diagnostic_flags = window.diagnostic_flags;
    out.component_leaves.push_back(std::move(leaf));
  }

  return out;
}

AnchorGraphChildCostResult build_anchor_graph_child_summary_components(
    const std::vector<AnchorGraphChildSummary>& summaries,
    const AnchorGraphChildCostConfig& config) {
  AnchorGraphChildCostResult out;

  if (!config.first_leaf_id.valid()) {
    add_diagnostic(out, DiagnosticSeverity::kError,
                   "anchor_graph_child_invalid_first_leaf_id",
                   "first graph-child component leaf id must be valid");
  }

  for (const AnchorGraphChildSummary& summary : summaries) {
    if (summary.end_ns < summary.start_ns) {
      add_diagnostic(out, DiagnosticSeverity::kError,
                     "anchor_graph_child_invalid_summary_range",
                     "graph-child summary end_ns cannot be smaller than "
                     "start_ns");
    }
    if (summary.duration_ns < 0) {
      add_diagnostic(out, DiagnosticSeverity::kError,
                     "anchor_graph_child_negative_summary_duration",
                     "graph-child summary duration cannot be negative");
    }
  }
  if (has_errors(out)) {
    return out;
  }

  out.component_leaves.reserve(summaries.size());
  for (const AnchorGraphChildSummary& summary : summaries) {
    if (!config.emit_zero_duration_windows && summary.duration_ns == 0 &&
        summary.diagnostic_flags.empty()) {
      continue;
    }

    AnchorCostComponentLeaf leaf;
    leaf.id = StructuralCostLeafId(
        config.first_leaf_id.value() +
        static_cast<StructuralCostLeafId::value_type>(out.component_leaves.size()));
    leaf.token_ordinal = summary.token_ordinal;
    leaf.kind = AnchorCostComponentKind::kGraphChild;
    leaf.duration_ns = summary.duration_ns;
    leaf.raw_child_task_count = summary.raw_child_task_count;
    leaf.source_ref_count = summary.source_ref_count;
    leaf.top_ops = summary.top_ops;
    leaf.diagnostic_flags = summary.diagnostic_flags;
    out.component_leaves.push_back(std::move(leaf));
  }

  return out;
}

}  // namespace traceloom
