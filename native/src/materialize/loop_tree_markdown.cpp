#include "traceloom/materialize/loop_tree_markdown.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
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

  out << "\n## Root\n\n";
  out << "```\n";
  out << "tree                                                       cat    |  occ   "
         "total_us    avg_us  avg_idle  avg_aux avg_self\n";
  out << "---------------------------------------------------------  -----  | ---- "
         "---------- --------- --------- -------- --------\n";

  for (const compat::VizNodeSqlRow* row : selected) {
    const std::string indent(row->depth * 2, ' ');
    const std::string tree_label =
        indent + row->local_node_id +
        (row->label.empty() ? std::string() : " " + row->label);
    const std::uint32_t occurrences =
        row->occurrence_count == 0 ? 1 : row->occurrence_count;
    const double avg_aux_us = row->aux_us / occurrences;
    const double avg_self_us = row->self_us / occurrences;
    out << pad_right(shorten(tree_label, 57), 57) << "  "
        << pad_right(shorten(row->category, 5), 5) << "  | "
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
