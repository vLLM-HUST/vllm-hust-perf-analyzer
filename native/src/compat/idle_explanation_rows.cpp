#include "traceloom/compat/idle_explanation_rows.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "traceloom/compat/anchor_sequence_rows.h"

namespace traceloom::compat {
namespace {

struct PreludeWindow {
  const ReportToken* token = nullptr;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

struct CategoryAccum {
  std::uint64_t slice_count = 0;
  std::uint64_t duration_ns = 0;
};

bool selected_device(std::uint32_t candidate,
                     const std::optional<std::uint32_t>& selected) {
  return !selected.has_value() || candidate == *selected;
}

std::map<std::uint32_t, std::vector<PreludeWindow>> build_prelude_windows(
    const std::vector<ReportToken>& tokens,
    const std::optional<std::uint32_t>& selected) {
  std::map<std::uint32_t, std::vector<const ReportToken*>> by_device;
  for (const ReportToken& token : tokens) {
    if (!selected_device(token.device_id, selected)) {
      continue;
    }
    if (!token.anchor_id.valid()) {
      throw std::invalid_argument(
          "idle attribution requires every ReportToken to own an anchor");
    }
    if (token.end_ns <= token.start_ns) {
      throw std::invalid_argument(
          "idle attribution received a non-positive ReportToken interval");
    }
    by_device[token.device_id].push_back(&token);
  }

  std::map<std::uint32_t, std::vector<PreludeWindow>> windows;
  for (auto& item : by_device) {
    std::vector<const ReportToken*>& device_tokens = item.second;
    std::stable_sort(
        device_tokens.begin(), device_tokens.end(),
        [](const ReportToken* lhs, const ReportToken* rhs) {
          if (lhs->start_ns != rhs->start_ns) {
            return lhs->start_ns < rhs->start_ns;
          }
          if (lhs->end_ns != rhs->end_ns) {
            return lhs->end_ns < rhs->end_ns;
          }
          return lhs->ordinal < rhs->ordinal;
        });
    std::int64_t frontier_ns = device_tokens.front()->start_ns;
    for (const ReportToken* token : device_tokens) {
      if (token->start_ns > frontier_ns) {
        windows[item.first].push_back(
            PreludeWindow{token, frontier_ns, token->start_ns});
      }
      frontier_ns = std::max(frontier_ns, token->end_ns);
    }
  }
  return windows;
}

std::map<std::uint32_t, std::vector<const IdleExplanationRow*>>
index_explanations(const IdleExplanationRunResult& explanations,
                   const std::optional<std::uint32_t>& selected,
                   std::uint64_t* total_ns) {
  std::map<std::uint32_t, std::vector<const IdleExplanationRow*>> by_device;
  for (const IdleExplanationDeviceResult& device : explanations.devices) {
    if (!selected_device(device.device_id, selected)) {
      continue;
    }
    for (const IdleExplanationRow& row : device.explanations) {
      if (row.end_ns <= row.start_ns) {
        throw std::invalid_argument(
            "idle attribution received a non-positive explanation interval");
      }
      *total_ns += static_cast<std::uint64_t>(row.end_ns - row.start_ns);
      by_device[device.device_id].push_back(&row);
    }
  }
  for (auto& item : by_device) {
    std::stable_sort(item.second.begin(), item.second.end(),
                     [](const IdleExplanationRow* lhs,
                        const IdleExplanationRow* rhs) {
                       if (lhs->start_ns != rhs->start_ns) {
                         return lhs->start_ns < rhs->start_ns;
                       }
                       return lhs->end_ns < rhs->end_ns;
                     });
    std::int64_t cursor = 0;
    bool have_cursor = false;
    for (const IdleExplanationRow* row : item.second) {
      if (have_cursor && row->start_ns < cursor) {
        throw std::invalid_argument(
            "idle attribution requires non-overlapping E4 explanations");
      }
      cursor = row->end_ns;
      have_cursor = true;
    }
  }
  return by_device;
}

}  // namespace

IdleExplanationAttributionRows build_idle_explanation_attribution_rows(
    const std::vector<ReportToken>& tokens,
    const IdleExplanationRunResult& explanations,
    const NodeCoverageSqlRows& node_coverage,
    std::uint32_t db_idx,
    std::optional<std::uint32_t> device_id) {
  IdleExplanationAttributionRows out;
  const auto windows = build_prelude_windows(tokens, device_id);
  const auto explanations_by_device =
      index_explanations(explanations, device_id,
                         &out.visible_productive_idle_ns);

  using AnchorKey =
      std::tuple<std::uint32_t, AnchorId::value_type, std::string, std::string>;
  std::map<AnchorKey, CategoryAccum> anchor_accum;
  for (const auto& item : windows) {
    const auto explanation_it = explanations_by_device.find(item.first);
    if (explanation_it == explanations_by_device.end()) {
      continue;
    }
    const std::vector<const IdleExplanationRow*>& device_explanations =
        explanation_it->second;
    std::size_t explanation_index = 0;
    for (const PreludeWindow& window : item.second) {
      while (explanation_index < device_explanations.size() &&
             device_explanations[explanation_index]->end_ns <=
                 window.start_ns) {
        ++explanation_index;
      }
      for (std::size_t index = explanation_index;
           index < device_explanations.size() &&
           device_explanations[index]->start_ns < window.end_ns;
           ++index) {
        const IdleExplanationRow& explanation =
            *device_explanations[index];
        const std::int64_t overlap_start =
            std::max(window.start_ns, explanation.start_ns);
        const std::int64_t overlap_end =
            std::min(window.end_ns, explanation.end_ns);
        if (overlap_end <= overlap_start) {
          continue;
        }
        const std::string category(
            idle_explanation_category_name(explanation.category));
        const std::string evidence_level(
            idle_evidence_level_name(explanation.evidence_level));
        CategoryAccum& accum = anchor_accum[AnchorKey{
            item.first, window.token->anchor_id.value(), category,
            evidence_level}];
        ++accum.slice_count;
        const std::uint64_t duration_ns =
            static_cast<std::uint64_t>(overlap_end - overlap_start);
        accum.duration_ns += duration_ns;
        out.anchor_prelude_attributed_ns += duration_ns;
      }
    }
  }

  out.anchors.reserve(anchor_accum.size());
  std::map<std::string, std::vector<const AnchorIdleExplanationRow*>>
      anchors_by_id;
  for (const auto& item : anchor_accum) {
    AnchorIdleExplanationRow row;
    row.anchor_id = anchor_compat_id(AnchorId(std::get<1>(item.first)));
    row.db_idx = db_idx;
    row.device_id = std::get<0>(item.first);
    row.anchor_idx = std::get<1>(item.first) + 1;
    row.category = std::get<2>(item.first);
    row.evidence_level = std::get<3>(item.first);
    row.slice_count = item.second.slice_count;
    row.duration_ns = item.second.duration_ns;
    out.anchors.push_back(std::move(row));
  }
  for (const AnchorIdleExplanationRow& row : out.anchors) {
    anchors_by_id[row.anchor_id].push_back(&row);
  }

  using NodeKey = std::tuple<std::string, std::uint32_t, std::string,
                             std::string, std::string>;
  std::map<NodeKey, CategoryAccum> node_accum;
  std::set<std::tuple<std::string, std::string, std::uint32_t, std::string>>
      seen_node_anchors;
  for (const VizNodeAnchorSqlRow& link : node_coverage.node_anchors) {
    if (link.db_idx != db_idx ||
        !selected_device(link.device_id, device_id)) {
      continue;
    }
    const auto dedupe_key =
        std::make_tuple(link.node_id, link.anchor_id, link.db_idx,
                        link.view_name);
    if (!seen_node_anchors.insert(dedupe_key).second) {
      throw std::invalid_argument(
          "duplicate node/anchor coverage in idle attribution");
    }
    const auto anchor_it = anchors_by_id.find(link.anchor_id);
    if (anchor_it == anchors_by_id.end()) {
      continue;
    }
    for (const AnchorIdleExplanationRow* anchor : anchor_it->second) {
      if (anchor->device_id != link.device_id) {
        throw std::invalid_argument(
            "node/anchor device mismatch in idle attribution");
      }
      CategoryAccum& accum = node_accum[NodeKey{
          link.node_id, link.device_id, link.view_name, anchor->category,
          anchor->evidence_level}];
      accum.slice_count += anchor->slice_count;
      accum.duration_ns += anchor->duration_ns;
    }
  }

  out.nodes.reserve(node_accum.size());
  for (const auto& item : node_accum) {
    NodeIdleExplanationRow row;
    row.node_id = std::get<0>(item.first);
    row.db_idx = db_idx;
    row.device_id = std::get<1>(item.first);
    row.view_name = std::get<2>(item.first);
    row.category = std::get<3>(item.first);
    row.evidence_level = std::get<4>(item.first);
    row.slice_count = item.second.slice_count;
    row.duration_ns = item.second.duration_ns;
    out.nodes.push_back(std::move(row));
  }
  if (out.anchor_prelude_attributed_ns > out.visible_productive_idle_ns) {
    throw std::logic_error(
        "anchor-prelude idle attribution exceeds device explanation total");
  }
  out.device_only_unassigned_ns =
      out.visible_productive_idle_ns - out.anchor_prelude_attributed_ns;
  return out;
}

}  // namespace traceloom::compat
