#include "traceloom/materialize/loop_tree_markdown.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <map>
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

const char* markdown_view_name(LoopTreeMarkdownView view) {
  switch (view) {
    case LoopTreeMarkdownView::kCompact:
      return "compact_grammar";
    case LoopTreeMarkdownView::kExpanded:
      return "expanded_tree";
    case LoopTreeMarkdownView::kBoth:
      return "compact_grammar_and_expanded_tree";
  }
  return "unknown";
}

std::string markdown_cell(std::string text) {
  std::string out;
  out.reserve(text.size());
  for (char ch : text) {
    if (ch == '|') {
      out += "\\|";
    } else if (ch == '`') {
      out += '\'';
    } else if (ch == '\n' || ch == '\r') {
      out += ' ';
    } else {
      out += ch;
    }
  }
  return out;
}

std::string compact_rhs(
    const compat::NativeGrammarMacroSummary& macro,
    std::size_t symbol_limit) {
  std::ostringstream out;
  const std::size_t emitted = std::min(symbol_limit, macro.rhs_labels.size());
  for (std::size_t index = 0; index < emitted; ++index) {
    if (index != 0) {
      out << " ";
    }
    out << markdown_cell(macro.rhs_labels[index]);
  }
  if (emitted < macro.rhs_labels.size()) {
    out << " ... +" << (macro.rhs_labels.size() - emitted);
  }
  return out.str();
}

struct OperatorFamilySummary {
  std::string symbol;
  std::string category;
  std::uint64_t node_def_count = 0;
  std::uint64_t occurrence_count = 0;
  double self_us = 0.0;
  double aux_us = 0.0;
};

std::vector<OperatorFamilySummary> aggregate_operator_families(
    const std::vector<const compat::VizNodeSqlRow*>& selected) {
  std::map<std::pair<std::string, std::string>, OperatorFamilySummary>
      by_identity;
  for (const compat::VizNodeSqlRow* row : selected) {
    if (row->kind != "atom") {
      continue;
    }
    OperatorFamilySummary& summary =
        by_identity[{row->symbol, row->category}];
    summary.symbol = row->symbol;
    summary.category = row->category;
    ++summary.node_def_count;
    summary.occurrence_count += row->occurrence_count;
    summary.self_us += row->self_us;
    summary.aux_us += row->aux_us;
  }
  std::vector<OperatorFamilySummary> out;
  out.reserve(by_identity.size());
  for (auto& item : by_identity) {
    out.push_back(std::move(item.second));
  }
  std::stable_sort(out.begin(), out.end(),
                   [](const OperatorFamilySummary& left,
                      const OperatorFamilySummary& right) {
                     if (left.self_us != right.self_us) {
                       return left.self_us > right.self_us;
                     }
                     if (left.symbol != right.symbol) {
                       return left.symbol < right.symbol;
                     }
                     return left.category < right.category;
                   });
  return out;
}

double root_total_us(
    const std::vector<const compat::VizNodeSqlRow*>& selected) {
  double total_us = 0.0;
  for (const compat::VizNodeSqlRow* row : selected) {
    if (row->kind == "seq") {
      total_us = std::max(total_us, row->total_us);
    }
  }
  if (total_us != 0.0) {
    return total_us;
  }
  for (const compat::VizNodeSqlRow* row : selected) {
    total_us = std::max(total_us, row->total_us);
  }
  return total_us;
}

void write_compact_projection(
    std::ostream& out,
    const std::vector<const compat::VizNodeSqlRow*>& selected,
    const LoopTreeMarkdownOptions& options,
    const compat::NativeCompactGrammarProjection* grammar,
    std::uint32_t device_id) {
  const std::vector<OperatorFamilySummary> families =
      aggregate_operator_families(selected);
  const double root_us = root_total_us(selected);

  out << "\n## Compact Grammar Summary\n\n";
  out << "This is the bounded reading surface. Repeated atom realizations are "
         "aggregated below; the `native_report_tree` rows remain the "
         "authoritative positional drill-down. Infrastructure and profiler "
         "markers are retained observations, not causal explanations for an "
         "adjacent gap.\n\n";
  out << "- expanded_node_def_count: `" << selected.size() << "`\n";
  out << "- operator_family_count: `" << families.size() << "`\n";
  if (grammar == nullptr || !grammar->available) {
    out << "- compact_grammar_state: `unavailable`\n";
    if (grammar != nullptr && !grammar->stop_reason.empty()) {
      out << "- compact_grammar_stop_reason: `" << grammar->stop_reason
          << "`\n";
    }
  } else {
    out << "- compact_grammar_state: `available`\n";
    out << "- compact_grammar_stop_reason: `" << grammar->stop_reason
        << "`\n";
    out << "- source_token_count: `" << grammar->source_token_count << "`\n";
    out << "- grammar_engine_steps: `" << grammar->engine_step_count << "`\n";
    out << "- live_grammar_nodes: `" << grammar->live_nodes.size() << "`\n";
    out << "- macro_definitions: `" << grammar->macro_defs.size() << "`\n";
  }

  out << "\n### Operator families by exact leaf self cost\n\n";
  out << "Leaf self costs are additive across this table. Inclusive macro "
         "spans below are nested and are not additive.\n\n";
  out << "| Family | Category | Node defs | Occurrences | Self us | Root % | Aux us |\n";
  out << "| --- | --- | ---: | ---: | ---: | ---: | ---: |\n";
  const std::size_t family_limit =
      std::min(options.compact_operator_family_limit, families.size());
  for (std::size_t index = 0; index < family_limit; ++index) {
    const OperatorFamilySummary& family = families[index];
    const double root_pct =
        root_us == 0.0 ? 0.0 : family.self_us * 100.0 / root_us;
    out << "| `" << markdown_cell(family.symbol) << "` | `"
        << markdown_cell(family.category) << "` | " << family.node_def_count
        << " | " << family.occurrence_count << " | " << fmt(family.self_us)
        << " | " << fmt(root_pct) << "% | " << fmt(family.aux_us) << " |\n";
  }
  if (family_limit < families.size()) {
    OperatorFamilySummary remainder;
    for (std::size_t index = family_limit; index < families.size(); ++index) {
      remainder.node_def_count += families[index].node_def_count;
      remainder.occurrence_count += families[index].occurrence_count;
      remainder.self_us += families[index].self_us;
      remainder.aux_us += families[index].aux_us;
    }
    const double root_pct =
        root_us == 0.0 ? 0.0 : remainder.self_us * 100.0 / root_us;
    out << "| _other " << (families.size() - family_limit)
        << " families_ |  | " << remainder.node_def_count << " | "
        << remainder.occurrence_count << " | " << fmt(remainder.self_us)
        << " | " << fmt(root_pct) << "% | " << fmt(remainder.aux_us)
        << " |\n";
  }

  if (grammar != nullptr && grammar->available) {
    out << "\n### Live grammar sequence\n\n";
    out << "Each `G*` entry is one final live grammar node. Token spans are "
           "half-open; anchor ranges give the exact expanded evidence window.\n\n";
    out << "| Live ID | Macro | Symbol | Token span | Anchor range | Span us |\n";
    out << "| --- | --- | --- | ---: | ---: | ---: |\n";
    for (const compat::NativeGrammarLiveNodeSummary& node :
         grammar->live_nodes) {
      out << "| `G" << node.grammar_node_id << "` | ";
      if (node.has_macro_def_id) {
        out << "`M" << node.macro_def_id << "`";
      }
      out << " | `" << markdown_cell(node.label) << "` | `"
          << node.source_begin_token_index << ".."
          << node.source_end_token_index_exclusive << "` | `"
          << node.first_anchor_idx << ".." << node.last_anchor_idx << "` | "
          << fmt(node.span_us) << " |\n";
    }

    out << "\n### Macro definitions\n\n";
    out << "`Occurrences` counts exact grammar-node realizations. `Inclusive "
           "span us` includes nested bodies and gaps, so rows must not be "
           "summed. The anchor range is the coverage envelope; use the live "
           "sequence or expanded tree for exact positions.\n\n";
    out << "| Macro | Level | Label / RHS prefix | Occurrences | Replacements | Gain | Inclusive span us | Anchor envelope |\n";
    out << "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |\n";
    for (const compat::NativeGrammarMacroSummary& macro :
         grammar->macro_defs) {
      const std::string rhs =
          compact_rhs(macro, options.compact_rhs_symbol_limit);
      out << "| `M" << macro.macro_def_id << "` | `"
          << markdown_cell(macro.level) << "` | `"
          << markdown_cell(macro.label);
      if (!rhs.empty()) {
        out << " := " << rhs;
      }
      out << "` | " << macro.occurrence_count << " | "
          << macro.replace_count << " | " << macro.gain << " | "
          << fmt(macro.inclusive_span_us) << " | `"
          << macro.first_anchor_idx << ".." << macro.last_anchor_idx
          << "` |\n";
    }
  }

  out << "\n### Expanded evidence drill-down\n\n";
  out << "The queryable database keeps every expanded position. For this "
         "device, start with:\n\n";
  out << "```sql\n"
      << "SELECT node_id, local_node_id, path, kind, symbol, category,\n"
      << "       occurrence_count, self_us, total_us\n"
      << "FROM traceloom_viz_node\n"
      << "WHERE view_name = 'native_report_tree' AND device_id = "
      << device_id << "\n"
      << "ORDER BY self_us DESC, total_us DESC;\n"
      << "```\n";
}

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
                              const LoopTreeMarkdownOptions& options,
                              const compat::NativeCompactGrammarProjection*
                                  compact_grammar) {
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
  out << "- human_view: `" << markdown_view_name(options.view) << "`\n";
  out << "- renderer: `native_loop_tree_markdown_v1`\n";
  out << "- source_kind: `" << options.source_kind << "`\n";
  out << "- input_format: `" << options.input_format << "`\n";
  out << "- input_evidence_contract: `" << options.input_evidence_contract
      << "`\n";
  out << "- input_scope: `" << options.input_scope << "`\n";
  out << "- input_evidence_state: `" << options.input_evidence_state << "`\n";
  out << "- input_missing_components: `" << options.input_missing_components
      << "`\n";
  out << "- trace_event_count: `" << options.trace_event_count << "`\n";
  out << "- anchor_count: `" << options.anchor_count << "`\n";

  if (options.replay_composition_region_count != 0) {
    out << "\n## ACLGraph Reconstruction\n\n";
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

  if (options.view == LoopTreeMarkdownView::kCompact ||
      options.view == LoopTreeMarkdownView::kBoth) {
    write_compact_projection(out, selected, options, compact_grammar,
                             device_id);
  }
  if (options.view == LoopTreeMarkdownView::kCompact) {
    return;
  }

  constexpr std::size_t kTreeColumnWidth = 48;
  constexpr std::size_t kCategoryColumnWidth = 14;

  out << "\n## Expanded Root\n\n";
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
