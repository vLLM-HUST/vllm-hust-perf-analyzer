#include "traceloom/materialize/loop_tree_markdown.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace traceloom {
namespace {

std::string basename_or_default(const std::string& path,
                                const std::string& fallback) {
  if (path.empty()) {
    return fallback;
  }
  const std::string::size_type pos = path.find_last_of("/\\");
  if (pos == std::string::npos) {
    return path.empty() ? fallback : path;
  }
  const std::string value = path.substr(pos + 1);
  return value.empty() ? fallback : value;
}

std::string fmt(double value) {
  std::ostringstream out;
  if (value >= 1000.0 || value <= -1000.0) {
    out << std::fixed << std::setprecision(0) << value;
    return out.str();
  }
  if (value == 0.0) {
    return "0.00";
  }
  out << std::fixed << std::setprecision(2) << value;
  std::string text = out.str();
  while (!text.empty() && text.back() == '0') {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.') {
    text.pop_back();
  }
  return text;
}

std::string fmt(std::uint32_t value) {
  return std::to_string(value);
}

std::string shorten(const std::string& text, std::size_t width) {
  if (text.size() <= width) {
    return text;
  }
  if (width <= 3) {
    return text.substr(0, width);
  }
  return text.substr(0, width - 3) + "...";
}

std::string pad_right(std::string text, std::size_t width) {
  if (text.size() < width) {
    text.append(width - text.size(), ' ');
  }
  return text;
}

std::string pad_left(std::string text, std::size_t width) {
  if (text.size() < width) {
    text.insert(text.begin(), width - text.size(), ' ');
  }
  return text;
}

double cost_average_divisor(const compat::VizNodeSqlRow& row) {
  double divisor =
      static_cast<double>(row.occurrence_count == 0 ? 1 : row.occurrence_count);
  if (row.kind == "repeat" && row.repeat_count > 0) {
    divisor *= static_cast<double>(row.repeat_count);
  }
  return divisor;
}

std::uint32_t local_node_order(const std::string& local_node_id) {
  std::size_t index = 0;
  while (index < local_node_id.size() &&
         (local_node_id[index] < '0' || local_node_id[index] > '9')) {
    ++index;
  }
  if (index == local_node_id.size()) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  std::uint64_t value = 0;
  for (; index < local_node_id.size(); ++index) {
    const char ch = local_node_id[index];
    if (ch < '0' || ch > '9') {
      break;
    }
    value = value * 10 + static_cast<std::uint64_t>(ch - '0');
    if (value > std::numeric_limits<std::uint32_t>::max()) {
      return std::numeric_limits<std::uint32_t>::max();
    }
  }
  return static_cast<std::uint32_t>(value);
}

struct RenderRow {
  const compat::VizNodeSqlRow* row = nullptr;
  std::uint32_t depth = 0;
};

std::vector<RenderRow> edge_ordered_rows(
    const compat::NodeCoverageSqlRows& rows,
    const std::vector<const compat::VizNodeSqlRow*>& selected,
    std::uint32_t db_idx,
    bool filter_device,
    std::uint32_t device_id) {
  std::unordered_map<std::string, const compat::VizNodeSqlRow*> node_by_id;
  node_by_id.reserve(selected.size());
  for (const compat::VizNodeSqlRow* row : selected) {
    node_by_id.emplace(row->node_id, row);
  }

  std::unordered_map<std::string, std::vector<const compat::VizEdgeSqlRow*>>
      children_by_parent;
  std::unordered_set<std::string> child_node_ids;
  for (const compat::VizEdgeSqlRow& edge : rows.edges) {
    if (edge.view_name != "native_report_tree" || edge.db_idx != db_idx) {
      continue;
    }
    if (filter_device && edge.device_id != device_id) {
      continue;
    }
    if (node_by_id.find(edge.parent_node_id) == node_by_id.end() ||
        node_by_id.find(edge.child_node_id) == node_by_id.end()) {
      continue;
    }
    children_by_parent[edge.parent_node_id].push_back(&edge);
    child_node_ids.insert(edge.child_node_id);
  }

  if (children_by_parent.empty()) {
    std::vector<RenderRow> fallback;
    fallback.reserve(selected.size());
    for (const compat::VizNodeSqlRow* row : selected) {
      fallback.push_back(RenderRow{row, row->depth});
    }
    return fallback;
  }

  for (auto& entry : children_by_parent) {
    std::stable_sort(entry.second.begin(), entry.second.end(),
                     [&](const compat::VizEdgeSqlRow* left,
                         const compat::VizEdgeSqlRow* right) {
                       if (left->edge_order != right->edge_order) {
                         return left->edge_order < right->edge_order;
                       }
                       const compat::VizNodeSqlRow* left_row =
                           node_by_id[left->child_node_id];
                       const compat::VizNodeSqlRow* right_row =
                           node_by_id[right->child_node_id];
                       const std::uint32_t left_order =
                           local_node_order(left_row->local_node_id);
                       const std::uint32_t right_order =
                           local_node_order(right_row->local_node_id);
                       if (left_order != right_order) {
                         return left_order < right_order;
                       }
                       return left_row->local_node_id < right_row->local_node_id;
                     });
  }

  std::vector<const compat::VizNodeSqlRow*> roots;
  for (const compat::VizNodeSqlRow* row : selected) {
    if (child_node_ids.find(row->node_id) == child_node_ids.end()) {
      roots.push_back(row);
    }
  }
  if (roots.empty()) {
    roots.push_back(selected.front());
  }
  std::stable_sort(roots.begin(), roots.end(),
                   [](const compat::VizNodeSqlRow* left,
                      const compat::VizNodeSqlRow* right) {
                     const std::uint32_t left_order =
                         local_node_order(left->local_node_id);
                     const std::uint32_t right_order =
                         local_node_order(right->local_node_id);
                     if (left_order != right_order) {
                       return left_order < right_order;
                     }
                     return left->local_node_id < right->local_node_id;
                   });

  std::vector<RenderRow> ordered;
  ordered.reserve(selected.size());
  std::unordered_set<std::string> emitted;
  std::unordered_set<std::string> path;
  auto append = [&](auto&& self,
                    const compat::VizNodeSqlRow* row,
                    std::uint32_t depth) -> void {
    if (!path.insert(row->node_id).second) {
      return;
    }
    ordered.push_back(RenderRow{row, depth});
    emitted.insert(row->node_id);
    const auto children = children_by_parent.find(row->node_id);
    if (children != children_by_parent.end()) {
      for (const compat::VizEdgeSqlRow* edge : children->second) {
        self(self, node_by_id[edge->child_node_id], depth + 1);
      }
    }
    path.erase(row->node_id);
  };

  for (const compat::VizNodeSqlRow* root : roots) {
    append(append, root, 0);
  }
  for (const compat::VizNodeSqlRow* row : selected) {
    if (emitted.find(row->node_id) == emitted.end()) {
      append(append, row, row->depth);
    }
  }
  return ordered;
}

}  // namespace

void write_loop_tree_markdown(std::ostream& out,
                              const compat::NodeCoverageSqlRows& rows,
                              const LoopTreeMarkdownOptions& options) {
  std::set<std::uint32_t> device_ids;
  for (const compat::VizNodeSqlRow& row : rows.nodes) {
    if (row.view_name == "native_report_tree" && row.db_idx == options.db_idx) {
      device_ids.insert(row.device_id);
    }
  }

  const bool filter_device =
      options.has_device_id || device_ids.size() == 1;
  const std::uint32_t device_id =
      options.has_device_id
          ? options.device_id
          : (device_ids.empty() ? 0 : *device_ids.begin());

  std::vector<const compat::VizNodeSqlRow*> selected;
  selected.reserve(rows.nodes.size());
  for (const compat::VizNodeSqlRow& row : rows.nodes) {
    if (row.view_name != "native_report_tree" || row.db_idx != options.db_idx) {
      continue;
    }
    if (filter_device && row.device_id != device_id) {
      continue;
    }
    selected.push_back(&row);
  }

  if (selected.empty()) {
    throw std::runtime_error("no native_report_tree rows to render");
  }

  std::stable_sort(selected.begin(), selected.end(),
                   [](const compat::VizNodeSqlRow* left,
                      const compat::VizNodeSqlRow* right) {
                     const std::uint32_t left_order =
                         local_node_order(left->local_node_id);
                     const std::uint32_t right_order =
                         local_node_order(right->local_node_id);
                     if (left_order != right_order) {
                       return left_order < right_order;
                     }
                     return left->local_node_id < right->local_node_id;
                   });

  const std::string db_label =
      options.db_label.empty()
          ? basename_or_default(options.source_path, "native")
          : options.db_label;

  out << "# Loop Tree (v2)\n\n";
  out << "- db: `" << db_label << "`\n";
  out << "- source_db: `" << options.source_path << "`\n";
  out << "- device_id: `"
      << (filter_device ? std::to_string(device_id) : std::string()) << "`\n";
  out << "- stream_scope: `device_compute_sequence`\n";
  out << "- db_idx: `" << options.db_idx << "`\n";
  out << "- global_rank: `native`\n";
  out << "- report_view: `native_report_tree`\n";
  out << "- renderer: `native_loop_tree_markdown_v0`\n";
  out << "- source_kind: `" << options.source_kind << "`\n";
  out << "- trace_event_count: `" << options.trace_event_count << "`\n";
  out << "- anchor_count: `" << options.anchor_count << "`\n";

  if (options.replay_composition_region_count != 0) {
    out << "\n## Graph Replay Reconstruction\n\n";
    out << "- regions: `" << options.replay_composition_region_count
        << "` (`" << options.recognized_replay_composition_region_count
        << "` recognized, `"
        << options.unrecognized_replay_composition_region_count
        << "` unrecognized)\n";
    out << "- replay_units: `" << options.replay_unit_count << "` (`"
        << options.exact_replay_unit_count << "` exact, `"
        << (options.replay_unit_count >= options.exact_replay_unit_count
                ? options.replay_unit_count - options.exact_replay_unit_count
                : 0)
        << "` legacy)\n\n";
    out << "| Status | Regions |\n";
    out << "| --- | ---: |\n";
    for (const ReconstructionStatusCount& status_count :
         options.reconstruction_status_counts) {
      out << "| `" << status_count.status << "` | "
          << status_count.region_count << " |\n";
    }
  }

  if (options.has_idle_explanation_summary) {
    const double direct_pct =
        options.visible_productive_idle_ns == 0
            ? 0.0
            : 100.0 *
                  static_cast<double>(options.direct_explained_idle_ns) /
                  static_cast<double>(options.visible_productive_idle_ns);
    out << "\n## Visible Productive Idle Evidence\n\n";
    out << "- analysis_status: `" << options.idle_analysis_status << "`\n";
    out << "- collection_status: `" << options.idle_collection_status
        << "`\n";
    out << "- attribution_rule_version: `"
        << options.idle_attribution_rule_version << "`\n";
    out << "- visible_productive_idle_us: `"
        << fmt(static_cast<double>(options.visible_productive_idle_ns) /
               1000.0)
        << "`\n";
    out << "- visible_productive_idle_ns: `"
        << options.visible_productive_idle_ns << "`\n";
    out << "- directly_explained_us: `"
        << fmt(static_cast<double>(options.direct_explained_idle_ns) / 1000.0)
        << "` (`" << fmt(direct_pct) << "%`)\n";
    out << "- directly_explained_ns: `"
        << options.direct_explained_idle_ns << "`\n";
    out << "- semantic_boundary: gaps in profiler-visible productive work; "
           "not proof of hardware idleness or causality\n\n";
    out << "| Category | Slices | Duration (ns) | Duration (us) | Gap % |\n";
    out << "| --- | ---: | ---: | ---: | ---: |\n";
    for (const IdleExplanationSummaryCount& count :
         options.idle_explanation_counts) {
      const double category_pct =
          options.visible_productive_idle_ns == 0
              ? 0.0
              : 100.0 * static_cast<double>(count.duration_ns) /
                    static_cast<double>(options.visible_productive_idle_ns);
      out << "| `" << count.category << "` | " << count.slice_count << " | "
          << count.duration_ns << " | "
          << fmt(static_cast<double>(count.duration_ns) / 1000.0) << " | "
          << fmt(category_pct) << "% |\n";
    }
    if (options.idle_explanation_counts.empty()) {
      out << "| `(no visible productive idle)` | 0 | 0 | 0.000 | 0.000% |\n";
    }

    const double anchor_attribution_pct =
        options.visible_productive_idle_ns == 0
            ? 0.0
            : 100.0 * static_cast<double>(
                          options.anchor_prelude_attributed_idle_ns) /
                  static_cast<double>(options.visible_productive_idle_ns);
    out << "\n### Anchor-Prelude Attribution\n\n";
    out << "- attributed_visible_productive_idle_us: `"
        << fmt(static_cast<double>(
                   options.anchor_prelude_attributed_idle_ns) /
               1000.0)
        << "` (`" << fmt(anchor_attribution_pct) << "%`)\n";
    out << "- attributed_visible_productive_idle_ns: `"
        << options.anchor_prelude_attributed_idle_ns << "`\n";
    out << "- device_only_unassigned_us: `"
        << fmt(static_cast<double>(options.device_only_unassigned_idle_ns) /
               1000.0)
        << "`\n";
    out << "- device_only_unassigned_ns: `"
        << options.device_only_unassigned_idle_ns << "`\n";
    out << "- attribution_boundary: exact intersections with the disjoint "
           "prelude window before each anchor; device-only residual is not "
           "forced onto a node\n";
    out << "- hierarchy_boundary: parent and child hotspot rows overlap by "
           "construction and are not additive\n\n";
    out << "| Node | Kind | Total (us) | Avg (us) | Wait | Capture | Runtime "
           "| No work | Unattributed |\n";
    out << "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: "
           "|\n";
    for (const IdleExplanationNodeHotspot& hotspot :
         options.idle_node_hotspots) {
      out << "| `" << hotspot.node_id << "` " << hotspot.label << " | `"
          << hotspot.kind << "` | "
          << fmt(static_cast<double>(hotspot.attributed_ns) / 1000.0) << " | "
          << fmt(hotspot.average_attributed_ns / 1000.0) << " | "
          << fmt(static_cast<double>(hotspot.wait_ns) / 1000.0) << " | "
          << fmt(static_cast<double>(hotspot.capture_control_ns) / 1000.0)
          << " | "
          << fmt(static_cast<double>(hotspot.runtime_control_ns) / 1000.0)
          << " | "
          << fmt(static_cast<double>(hotspot.no_observed_work_ns) / 1000.0)
          << " | "
          << fmt(static_cast<double>(hotspot.unattributed_ns) / 1000.0)
          << " |\n";
    }
    if (options.idle_node_hotspots.empty()) {
      out << "| `(no anchor-prelude attribution)` |  | 0.00 | 0.00 | 0.00 | "
             "0.00 | 0.00 | 0.00 | 0.00 |\n";
    }
  }

  constexpr std::size_t kTreeColumnWidth = 48;
  constexpr std::size_t kCategoryColumnWidth = 14;

  out << "\n## Root\n\n";
  out << "```\n";
  out << pad_right("tree", kTreeColumnWidth) << "  "
      << pad_right("cat", kCategoryColumnWidth)
      << "  |  occ   total_us    avg_us  avg_idle  avg_aux avg_self\n";
  out << std::string(kTreeColumnWidth, '-')
      << "  " << std::string(kCategoryColumnWidth, '-')
      << "  | ---- ---------- --------- --------- -------- --------\n";

  const std::vector<RenderRow> render_rows =
      edge_ordered_rows(rows, selected, options.db_idx, filter_device,
                        device_id);

  for (std::size_t row_index = 0; row_index < render_rows.size(); ++row_index) {
    const compat::VizNodeSqlRow* row = render_rows[row_index].row;
    const std::string indent(render_rows[row_index].depth * 2, ' ');
    const std::string tree_label =
        indent + row->local_node_id +
        (row->label.empty() ? std::string() : " " + row->label);
    const double average_divisor = cost_average_divisor(*row);
    const double avg_aux_us = row->aux_us / average_divisor;
    const double avg_self_us = row->self_us / average_divisor;
    out << pad_right(shorten(tree_label, kTreeColumnWidth), kTreeColumnWidth) << "  "
        << pad_right(shorten(row->category, kCategoryColumnWidth),
                     kCategoryColumnWidth)
        << "  | "
        << pad_left(fmt(row->occurrence_count), 4) << " "
        << pad_left(fmt(row->total_us), 10) << " "
        << pad_left(fmt(row->avg_total_us), 9) << " "
        << pad_left(fmt(row->avg_idle_us), 9) << " "
        << pad_left(fmt(avg_aux_us), 8) << " "
        << pad_left(fmt(avg_self_us), 8) << "\n";
  }

  out << "```\n";
}

}  // namespace traceloom
