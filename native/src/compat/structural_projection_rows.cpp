#include "traceloom/compat/structural_projection_rows.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "traceloom/analysis/structural_occurrence_builder.h"
#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/aux_attribution_rows.h"

namespace traceloom::compat {
namespace {
double ns_to_us(std::int64_t ns) {
  return static_cast<double>(ns) / 1000.0;
}

const char* structural_node_kind_name(StructuralNodeKind kind) {
  switch (kind) {
    case StructuralNodeKind::kSeq:
      return "seq";
    case StructuralNodeKind::kRepeat:
      return "repeat";
    case StructuralNodeKind::kAtom:
      return "atom";
  }
  return "unknown";
}

const char* structural_node_type_name(StructuralNodeKind kind) {
  switch (kind) {
    case StructuralNodeKind::kSeq:
      return "Seq";
    case StructuralNodeKind::kRepeat:
      return "Repeat";
    case StructuralNodeKind::kAtom:
      return "Atom";
  }
  return "Unknown";
}

std::string node_id_for_def(const StructuralNodeDef& def,
                            std::uint32_t device_id,
                            bool scope_by_device) {
  if (!scope_by_device) {
    return "node-" + def.local_node_id;
  }
  return "node-d" + std::to_string(device_id) + "-" + def.local_node_id;
}

std::uint32_t anchor_idx_for_token(const StructuralProjectionToken& token) {
  if (!token.anchor_id.valid()) {
    return 0;
  }
  return token.anchor_id.value() + 1;
}

double token_duration_us(const StructuralProjectionToken& token) {
  return ns_to_us(token.end_ns - token.start_ns);
}

double token_timeline_anchor_us(const StructuralProjectionToken& token) {
  return token.has_prelude_cost ? token.timeline_anchor_us
                                : token_duration_us(token);
}

bool is_comm_token(const StructuralProjectionToken& token) {
  return token.anchor_kind == StructuralAnchorKind::kCollective;
}

std::string coverage_kind_name(StructuralCoverageKind kind) {
  switch (kind) {
    case StructuralCoverageKind::kDirectBody:
      return "body";
    case StructuralCoverageKind::kAtomLeaf:
      return "self";
  }
  return "unknown";
}

struct NodeAccum {
  std::set<std::uint32_t> token_ordinals;
  std::uint32_t occurrence_count = 0;
  std::uint32_t first_anchor_idx = 0;
  std::uint32_t last_anchor_idx = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  double compute_us = 0.0;
  double comm_us = 0.0;
  double idle_us = 0.0;
  double self_us = 0.0;
  double aux_event_count = 0.0;
  double aux_us = 0.0;
};

struct TokenCostPacket {
  double compute_us = 0.0;
  double comm_us = 0.0;
  double idle_us = 0.0;
  double aux_event_count = 0.0;
  double aux_us = 0.0;
};

struct PreludeCost {
  double exec_aux_us = 0.0;
  double comm_us = 0.0;
  double idle_us = 0.0;
  double aux_event_count = 0.0;
  double aux_us = 0.0;
};

struct DeviceEventIndex {
  std::vector<const TraceEventRow*> events;
  std::vector<std::int64_t> prefix_max_end_ns;
};

std::uint32_t primary_device_id(const std::vector<StructuralProjectionToken>& tokens) {
  return tokens.empty() ? 0 : tokens.front().device_id;
}

std::string macro_discovery_status(const StructuralOccurrenceGraph& tree) {
  for (const Diagnostic& diagnostic : tree.diagnostics) {
    if (diagnostic.code ==
        "grammar_partial_sequence_too_large_for_full_pair_discovery") {
      return "native_report_tree_partial_size_limit";
    }
    if (diagnostic.code == "grammar_recovery_rejected") {
      return "native_report_tree_flat_fallback_rejected";
    }
    if (diagnostic.code == "grammar_recovery_exception") {
      return "native_report_tree_flat_fallback_exception";
    }
  }
  return "native_report_tree_complete";
}

struct AuxAttributionIndex {
  std::map<std::string, std::vector<const AuxLinkSqlRow*>> links_by_anchor;
};

AuxAttributionIndex build_aux_attribution_index(
    const AuxAttributionSqlRows& aux_rows) {
  AuxAttributionIndex out;
  for (const AuxLinkSqlRow& link : aux_rows.aux_links) {
    out.links_by_anchor[link.anchor_id].push_back(&link);
  }
  return out;
}

TokenCostPacket token_cost_packet(const StructuralProjectionToken& token,
                                  const AuxAttributionIndex& aux_index) {
  TokenCostPacket packet;
  const double anchor_us = token_timeline_anchor_us(token);
  if (is_comm_token(token)) {
    packet.comm_us = anchor_us;
  } else {
    packet.compute_us = anchor_us;
  }
  packet.compute_us += token.prelude_exec_aux_us;
  packet.comm_us += token.prelude_comm_us;
  packet.idle_us += token.prelude_idle_us;
  if (token.has_prelude_cost) {
    packet.aux_event_count = token.prelude_aux_event_count;
    packet.aux_us = token.prelude_aux_us;
    return packet;
  }

  if (!token.anchor_id.valid()) {
    return packet;
  }
  const auto found =
      aux_index.links_by_anchor.find(anchor_compat_id(token.anchor_id));
  if (found == aux_index.links_by_anchor.end()) {
    return packet;
  }
  packet.aux_event_count = static_cast<double>(found->second.size());
  for (const AuxLinkSqlRow* link : found->second) {
    packet.aux_us += link->aux_dur_us;
  }
  return packet;
}

std::vector<std::uint32_t> occurrence_counts_by_def(const StructuralOccurrenceGraph& tree) {
  std::vector<std::uint32_t> out(tree.node_defs.size(), 0);
  for (const StructuralNodeOccurrence& occurrence : tree.occurrences) {
    if (!occurrence.node_def_id.valid() ||
        occurrence.node_def_id.value() >= tree.node_defs.size()) {
      throw std::invalid_argument("StructuralOccurrenceGraph occurrence references bad def");
    }
    out[occurrence.node_def_id.value()] += 1;
  }
  return out;
}

std::vector<NodeAccum> accumulate_nodes(
    const StructuralOccurrenceGraph& tree,
    const std::vector<StructuralProjectionToken>& tokens,
    const AuxAttributionIndex& aux_index) {
  std::vector<NodeAccum> out(tree.node_defs.size());
  const std::vector<std::uint32_t> occurrence_counts =
      occurrence_counts_by_def(tree);
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i].occurrence_count = occurrence_counts[i];
  }

  for (const StructuralNodeCoverage& coverage : tree.coverage) {
    if (!coverage.node_occurrence_id.valid() ||
        coverage.node_occurrence_id.value() >= tree.occurrences.size()) {
      throw std::invalid_argument("StructuralOccurrenceGraph coverage references bad occurrence");
    }
    if (coverage.token_end_ordinal < coverage.token_start_ordinal ||
        coverage.token_end_ordinal > tokens.size()) {
      throw std::invalid_argument("StructuralOccurrenceGraph coverage token span is out of range");
    }
    const StructuralNodeOccurrence& occurrence =
        tree.occurrences[coverage.node_occurrence_id.value()];
    NodeAccum& accum = out[occurrence.node_def_id.value()];
    for (std::uint32_t token_index = coverage.token_start_ordinal;
         token_index < coverage.token_end_ordinal; ++token_index) {
      const StructuralProjectionToken& token = tokens[token_index];
      const std::uint32_t anchor_idx = anchor_idx_for_token(token);
      if (accum.token_ordinals.insert(token_index).second) {
        const TokenCostPacket packet = token_cost_packet(token, aux_index);
        accum.aux_event_count += packet.aux_event_count;
        accum.aux_us += packet.aux_us;
        if (accum.first_anchor_idx == 0 ||
            (anchor_idx != 0 && anchor_idx < accum.first_anchor_idx)) {
          accum.first_anchor_idx = anchor_idx;
        }
        accum.last_anchor_idx = std::max(accum.last_anchor_idx, anchor_idx);
        if (accum.start_ns == 0 || token.start_ns < accum.start_ns) {
          accum.start_ns = token.start_ns;
        }
        accum.end_ns = std::max(accum.end_ns, token.end_ns);
        accum.compute_us += packet.compute_us;
        accum.comm_us += packet.comm_us;
        accum.idle_us += packet.idle_us;
      }
      if (coverage.kind == StructuralCoverageKind::kAtomLeaf) {
        accum.self_us += token_duration_us(token);
      }
    }
  }
  return out;
}

void validate_root_wall_clock_conservation(
    const StructuralOccurrenceGraph& tree,
    const std::vector<StructuralProjectionToken>& tokens,
    const std::vector<NodeAccum>& accum) {
  const bool all_normalized =
      std::all_of(tokens.begin(), tokens.end(), [](const StructuralProjectionToken& token) {
        return token.has_prelude_cost;
      });
  if (!all_normalized) {
    return;
  }

  const auto root_found = std::find_if(
      tree.occurrences.begin(), tree.occurrences.end(),
      [](const StructuralNodeOccurrence& occurrence) {
        return !occurrence.parent_occurrence_id.valid();
      });
  if (root_found == tree.occurrences.end()) {
    throw std::invalid_argument(
        "structural cost conservation requires a root occurrence");
  }
  const StructuralNodeDefId root_def_id = root_found->node_def_id;
  const std::size_t root_def_occurrences = static_cast<std::size_t>(
      std::count_if(tree.occurrences.begin(), tree.occurrences.end(),
                    [root_def_id](const StructuralNodeOccurrence& occurrence) {
                      return occurrence.node_def_id == root_def_id;
                    }));
  if (root_def_occurrences != 1) {
    throw std::invalid_argument(
        "structural cost conservation requires a unique root definition");
  }
  const NodeAccum& root_accum = accum[root_def_id.value()];
  if (root_accum.token_ordinals.size() != tokens.size()) {
    throw std::invalid_argument(
        "structural root cost does not cover every token exactly once");
  }

  struct DeviceBounds {
    bool initialized = false;
    std::int64_t start_ns = 0;
    std::int64_t end_ns = 0;
  };
  std::map<std::uint32_t, DeviceBounds> bounds_by_device;
  for (const StructuralProjectionToken& token : tokens) {
    DeviceBounds& bounds = bounds_by_device[token.device_id];
    if (!bounds.initialized) {
      bounds.initialized = true;
      bounds.start_ns = token.start_ns;
      bounds.end_ns = token.end_ns;
      continue;
    }
    bounds.start_ns = std::min(bounds.start_ns, token.start_ns);
    bounds.end_ns = std::max(bounds.end_ns, token.end_ns);
  }
  double expected_us = 0.0;
  for (const auto& item : bounds_by_device) {
    expected_us += ns_to_us(item.second.end_ns - item.second.start_ns);
  }
  const double actual_us =
      root_accum.compute_us + root_accum.comm_us + root_accum.idle_us;
  const double tolerance_us =
      std::max(1e-9, std::abs(expected_us) * 1e-9);
  if (std::abs(actual_us - expected_us) > tolerance_us) {
    throw std::invalid_argument(
        "structural root wall-clock cost is not conserved");
  }
}

std::map<StructuralNodeDefId::value_type, StructuralNodeOccurrenceId>
first_occurrence_by_def(const StructuralOccurrenceGraph& tree) {
  std::map<StructuralNodeDefId::value_type, StructuralNodeOccurrenceId> out;
  for (const StructuralNodeOccurrence& occurrence : tree.occurrences) {
    out.emplace(occurrence.node_def_id.value(), occurrence.id);
  }
  return out;
}

std::string path_for_occurrence(
    const StructuralOccurrenceGraph& tree,
    StructuralNodeOccurrenceId occurrence_id) {
  std::vector<std::string> parts;
  StructuralNodeOccurrenceId cursor = occurrence_id;
  while (cursor.valid()) {
    const StructuralNodeOccurrence& occurrence = structural_node_occurrence(tree, cursor);
    const StructuralNodeDef& def = structural_node_def(tree, occurrence.node_def_id);
    parts.push_back(def.local_node_id);
    cursor = occurrence.parent_occurrence_id;
  }
  std::reverse(parts.begin(), parts.end());

  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) {
      out += "/";
    }
    out += parts[i];
  }
  return out;
}

std::string repeat_context_for_occurrence(const StructuralOccurrenceGraph& tree,
                                          const StructuralNodeOccurrence& occurrence) {
  std::vector<std::string> parts;
  StructuralNodeOccurrenceId cursor = occurrence.id;
  while (cursor.valid()) {
    const StructuralNodeOccurrence& current = structural_node_occurrence(tree, cursor);
    if (current.parent_occurrence_id.valid()) {
      const StructuralNodeOccurrence& parent =
          structural_node_occurrence(tree, current.parent_occurrence_id);
      const StructuralNodeDef& parent_def = structural_node_def(tree, parent.node_def_id);
      if (parent_def.kind == StructuralNodeKind::kRepeat) {
        parts.push_back(parent_def.local_node_id + "#" +
                        std::to_string(current.repeat_iteration));
      }
    }
    cursor = current.parent_occurrence_id;
  }
  std::reverse(parts.begin(), parts.end());

  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) {
      out += "/";
    }
    out += parts[i];
  }
  return out;
}

std::uint32_t loop_depth_for_occurrence(const StructuralOccurrenceGraph& tree,
                                        StructuralNodeOccurrenceId occurrence_id) {
  std::uint32_t depth = 0;
  StructuralNodeOccurrenceId cursor = occurrence_id;
  while (cursor.valid()) {
    const StructuralNodeOccurrence& occurrence = structural_node_occurrence(tree, cursor);
    const StructuralNodeDef& def = structural_node_def(tree, occurrence.node_def_id);
    if (def.kind == StructuralNodeKind::kRepeat) {
      ++depth;
    }
    cursor = occurrence.parent_occurrence_id;
  }
  return depth;
}

std::uint32_t display_occurrence_count(const NodeAccum& accum) {
  return accum.occurrence_count == 0 ? 1 : accum.occurrence_count;
}

double cost_average_divisor(const StructuralNodeDef& def,
                            std::uint32_t occurrence_count) {
  double divisor = static_cast<double>(occurrence_count);
  if (def.kind == StructuralNodeKind::kRepeat && def.repeat_count > 0) {
    divisor *= static_cast<double>(def.repeat_count);
  }
  return divisor;
}

}  // namespace

NodeCoverageSqlRows build_structural_node_coverage_sql_rows(
    const StructuralOccurrenceGraph& tree,
    const std::vector<StructuralProjectionToken>& tokens,
    std::uint32_t db_idx,
    std::string view_name,
    bool scope_node_ids_by_device) {
  return build_structural_node_coverage_sql_rows(
      tree, tokens, AuxAttributionSqlRows{}, db_idx, std::move(view_name),
      scope_node_ids_by_device);
}

NodeCoverageSqlRows build_structural_node_coverage_sql_rows(
    const StructuralOccurrenceGraph& tree,
    const std::vector<StructuralProjectionToken>& tokens,
    const AuxAttributionSqlRows& aux_rows,
    std::uint32_t db_idx,
    std::string view_name,
    bool scope_node_ids_by_device) {
  validate_structural_occurrence_graph_or_throw(tree, static_cast<std::uint32_t>(tokens.size()));

  NodeCoverageSqlRows rows;
  const std::uint32_t device_id = primary_device_id(tokens);
  const AuxAttributionIndex aux_index = build_aux_attribution_index(aux_rows);
  const std::vector<NodeAccum> accum =
      accumulate_nodes(tree, tokens, aux_index);
  validate_root_wall_clock_conservation(tree, tokens, accum);
  const auto first_occurrences = first_occurrence_by_def(tree);

  rows.nodes.reserve(tree.node_defs.size());
  for (const StructuralNodeDef& def : tree.node_defs) {
    const NodeAccum& node_accum = accum[def.id.value()];
    const std::uint32_t occurrence_count = display_occurrence_count(node_accum);
    const double average_divisor =
        cost_average_divisor(def, occurrence_count);
    const double total_us =
        node_accum.compute_us + node_accum.comm_us + node_accum.idle_us;

    VizNodeSqlRow row;
    row.node_id = node_id_for_def(def, device_id, scope_node_ids_by_device);
    row.db_idx = db_idx;
    row.device_id = device_id;
    row.view_name = view_name;
    row.local_node_id = def.local_node_id;
    const auto occurrence_found = first_occurrences.find(def.id.value());
    row.path = occurrence_found == first_occurrences.end()
                   ? def.local_node_id
                   : path_for_occurrence(tree, occurrence_found->second);
    row.node_type = structural_node_type_name(def.kind);
    row.kind = structural_node_kind_name(def.kind);
    row.symbol = def.display_op;
    row.label = def.display_op;
    row.category = def.display_category;
    row.depth = def.display_depth;
    row.level = def.display_depth;
    row.repeat_label = def.kind == StructuralNodeKind::kRepeat ? def.display_op : "";
    row.repeat_count = def.repeat_count;
    row.occurrence_count = occurrence_count;
    row.anchor_count = static_cast<std::uint32_t>(node_accum.token_ordinals.size());
    row.anchors_per_occurrence =
        occurrence_count == 0
            ? 0.0
            : static_cast<double>(row.anchor_count) / occurrence_count;
    row.first_anchor_idx = node_accum.first_anchor_idx;
    row.last_anchor_idx = node_accum.last_anchor_idx;
    row.compute_us = node_accum.compute_us;
    row.comm_us = node_accum.comm_us;
    row.idle_us = node_accum.idle_us;
    row.total_us = total_us;
    row.avg_compute_us = node_accum.compute_us / average_divisor;
    row.avg_comm_us = node_accum.comm_us / average_divisor;
    row.avg_idle_us = node_accum.idle_us / average_divisor;
    row.avg_total_us = total_us / average_divisor;
    row.self_us = node_accum.self_us;
    row.aux_events = node_accum.aux_event_count;
    row.aux_us = node_accum.aux_us;
    rows.nodes.push_back(std::move(row));

    if (def.kind == StructuralNodeKind::kRepeat) {
      LoopNodeSqlRow loop;
      loop.node_id = node_id_for_def(def, device_id, scope_node_ids_by_device);
      loop.db_idx = db_idx;
      loop.device_id = device_id;
      loop.view_name = view_name;
      loop.loop_rank = static_cast<std::uint32_t>(rows.loop_nodes.size() + 1);
      loop.repeat_label = def.display_op;
      loop.repeat_count = def.repeat_count;
      loop.occurrence_count = occurrence_count;
      loop.anchor_count =
          static_cast<std::uint32_t>(node_accum.token_ordinals.size());
      loop.total_us = total_us;
      loop.avg_total_us = total_us / average_divisor;
      loop.compute_us = node_accum.compute_us;
      loop.comm_us = node_accum.comm_us;
      loop.idle_us = node_accum.idle_us;
      rows.loop_nodes.push_back(std::move(loop));
    }
  }

  std::set<std::pair<std::string, std::string>> seen_edges;
  for (const StructuralOccurrenceEdge& edge : tree.edges) {
    const StructuralNodeOccurrence& parent =
        structural_node_occurrence(tree, edge.parent_occurrence_id);
    const StructuralNodeOccurrence& child =
        structural_node_occurrence(tree, edge.child_occurrence_id);
    const StructuralNodeDef& parent_def = structural_node_def(tree, parent.node_def_id);
    const StructuralNodeDef& child_def = structural_node_def(tree, child.node_def_id);
    const std::pair<std::string, std::string> key{
        node_id_for_def(parent_def, device_id, scope_node_ids_by_device),
        node_id_for_def(child_def, device_id, scope_node_ids_by_device)};
    if (!seen_edges.insert(key).second) {
      continue;
    }
    VizEdgeSqlRow row;
    row.parent_node_id = key.first;
    row.child_node_id = key.second;
    row.db_idx = db_idx;
    row.device_id = device_id;
    row.view_name = view_name;
    row.edge_order = edge.edge_order;
    row.edge_kind = "tree";
    rows.edges.push_back(std::move(row));
  }

  std::set<std::string> primary_anchor_ids;
  for (const StructuralNodeCoverage& coverage : tree.coverage) {
    const StructuralNodeOccurrence& occurrence =
        structural_node_occurrence(tree, coverage.node_occurrence_id);
    const StructuralNodeDef& def = structural_node_def(tree, occurrence.node_def_id);
    std::uint32_t anchor_order = 1;
    for (std::uint32_t token_index = coverage.token_start_ordinal;
         token_index < coverage.token_end_ordinal; ++token_index) {
      const StructuralProjectionToken& token = tokens[token_index];
      if (!token.anchor_id.valid()) {
        continue;
      }
      const std::string anchor_id = anchor_compat_id(token.anchor_id);

      VizNodeAnchorSqlRow row;
      row.node_id = node_id_for_def(def, device_id, scope_node_ids_by_device);
      row.anchor_id = anchor_id;
      row.db_idx = db_idx;
      row.device_id = device_id;
      row.view_name = view_name;
      row.occurrence_idx = occurrence.occurrence_index_for_def + 1;
      row.anchor_order = anchor_order++;
      row.coverage_kind = coverage_kind_name(coverage.kind);
      row.repeat_context = repeat_context_for_occurrence(tree, occurrence);
      const TokenCostPacket packet = token_cost_packet(token, aux_index);
      row.compute_us = packet.compute_us;
      row.comm_us = packet.comm_us;
      row.idle_us = packet.idle_us;
      row.total_us = packet.compute_us + packet.comm_us + packet.idle_us;
      row.self_us = coverage.kind == StructuralCoverageKind::kAtomLeaf
                        ? token_duration_us(token)
                        : 0.0;
      row.aux_events = packet.aux_event_count;
      row.aux_us = packet.aux_us;
      rows.node_anchors.push_back(std::move(row));

      if (coverage.kind == StructuralCoverageKind::kAtomLeaf &&
          primary_anchor_ids.insert(anchor_id).second) {
        AnchorPrimaryNodeSqlRow primary;
        primary.anchor_id = anchor_id;
        primary.node_id =
            node_id_for_def(def, device_id, scope_node_ids_by_device);
        primary.db_idx = db_idx;
        primary.device_id = device_id;
        primary.view_name = view_name;
        primary.reason = "atom_leaf";
        rows.anchor_primary_nodes.push_back(std::move(primary));
      }
    }
  }

  return rows;
}

NodeCoverageSqlRows build_native_structural_node_coverage_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx,
    std::string view_name) {
  const std::vector<NativeStructuralDevicePartition> partitions =
      partition_structural_projection_tokens_by_device(ir);
  const AuxAttributionSqlRows aux_rows =
      build_aux_attribution_sql_rows(ir, db_idx);
  const bool scope_node_ids = partitions.size() > 1;
  NodeCoverageSqlRows rows;
  for (const NativeStructuralDevicePartition& partition : partitions) {
    const StructuralOccurrenceGraph tree =
        build_structural_occurrence_graph_from_tokens(partition.tokens);
    NodeCoverageSqlRows device_rows = build_structural_node_coverage_sql_rows(
        tree, partition.tokens, aux_rows, db_idx, view_name, scope_node_ids);
    rows.nodes.insert(rows.nodes.end(), device_rows.nodes.begin(),
                      device_rows.nodes.end());
    rows.edges.insert(rows.edges.end(), device_rows.edges.begin(),
                      device_rows.edges.end());
    rows.loop_nodes.insert(rows.loop_nodes.end(),
                           device_rows.loop_nodes.begin(),
                           device_rows.loop_nodes.end());
    rows.node_anchors.insert(rows.node_anchors.end(),
                             device_rows.node_anchors.begin(),
                             device_rows.node_anchors.end());
    rows.anchor_primary_nodes.insert(
        rows.anchor_primary_nodes.end(),
        device_rows.anchor_primary_nodes.begin(),
        device_rows.anchor_primary_nodes.end());
  }
  return rows;
}

LoopTreeSqlRows split_loop_tree_sql_rows(const NodeCoverageSqlRows& rows) {
  LoopTreeSqlRows out;
  out.nodes = rows.nodes;
  out.edges = rows.edges;
  out.loop_nodes = rows.loop_nodes;
  return out;
}

NodeAnchorCoverageSqlRows split_node_anchor_coverage_sql_rows(
    const NodeCoverageSqlRows& rows) {
  NodeAnchorCoverageSqlRows out;
  out.node_anchors = rows.node_anchors;
  out.anchor_primary_nodes = rows.anchor_primary_nodes;
  return out;
}

SemanticTreeSqlRows build_structural_semantic_sql_rows(
    const StructuralOccurrenceGraph& tree,
    const std::vector<StructuralProjectionToken>& tokens,
    std::uint32_t db_idx,
    std::string tree_id,
    std::string view_name,
    bool scope_node_ids_by_device) {
  return build_structural_semantic_sql_rows(
      tree, tokens, AuxAttributionSqlRows{}, db_idx, std::move(tree_id),
      std::move(view_name), scope_node_ids_by_device);
}

SemanticTreeSqlRows build_structural_semantic_sql_rows(
    const StructuralOccurrenceGraph& tree,
    const std::vector<StructuralProjectionToken>& tokens,
    const AuxAttributionSqlRows& aux_rows,
    std::uint32_t db_idx,
    std::string tree_id,
    std::string view_name,
    bool scope_node_ids_by_device) {
  validate_structural_occurrence_graph_or_throw(tree, static_cast<std::uint32_t>(tokens.size()));

  SemanticTreeSqlRows rows;
  const std::uint32_t device_id = primary_device_id(tokens);
  const AuxAttributionIndex aux_index = build_aux_attribution_index(aux_rows);
  const std::vector<NodeAccum> accum =
      accumulate_nodes(tree, tokens, aux_index);
  validate_root_wall_clock_conservation(tree, tokens, accum);
  const auto first_occurrences = first_occurrence_by_def(tree);

  SemanticTreeHeaderSqlRow header;
  header.tree_id = tree_id;
  header.db_idx = db_idx;
  header.device_id = device_id;
  header.view_name = view_name;
  header.tree_kind = "semantic";
  header.stem = "native_report_tree";
  header.schema_version = "compat-v1";
  header.semantic_projection = "native_report_tree";
  header.macro_discovery = macro_discovery_status(tree);
  header.readable_macro_mode = "native_report_tree";
  header.auxiliary_attribution =
      aux_rows.aux_links.empty() ? "none" : "native_aux_attribution";
  if (!tree.node_defs.empty()) {
    header.root_node_id =
        node_id_for_def(tree.node_defs.front(), device_id,
                        scope_node_ids_by_device);
  }
  rows.trees.push_back(header);

  rows.nodes.reserve(tree.node_defs.size());
  for (const StructuralNodeDef& def : tree.node_defs) {
    const NodeAccum& node_accum = accum[def.id.value()];
    const std::uint32_t occurrence_count = display_occurrence_count(node_accum);
    const double average_divisor =
        cost_average_divisor(def, occurrence_count);
    const double total_us =
        node_accum.compute_us + node_accum.comm_us + node_accum.idle_us;
    const auto occurrence_found = first_occurrences.find(def.id.value());

    SemanticNodeSqlRow row;
    row.node_id = node_id_for_def(def, device_id, scope_node_ids_by_device);
    row.tree_id = tree_id;
    row.db_idx = db_idx;
    row.device_id = device_id;
    row.view_name = view_name;
    row.tree_kind = "semantic";
    row.local_node_id = def.local_node_id;
    row.preorder_idx = def.definition_order;
    row.path = occurrence_found == first_occurrences.end()
                   ? def.local_node_id
                   : path_for_occurrence(tree, occurrence_found->second);
    row.depth = def.display_depth;
    row.display_depth = def.display_depth;
    row.node_type = structural_node_type_name(def.kind);
    row.semantic_kind = structural_node_kind_name(def.kind);
    row.symbol = def.display_op;
    row.label = def.display_op;
    row.category = def.display_category;
    row.repeat_count = def.repeat_count;
    row.occurrence_count = occurrence_count;
    row.anchor_count =
        static_cast<std::uint32_t>(node_accum.token_ordinals.size());
    row.first_anchor_idx = node_accum.first_anchor_idx;
    row.last_anchor_idx = node_accum.last_anchor_idx;
    row.start_ns = node_accum.start_ns;
    row.end_ns = node_accum.end_ns;
    row.compute_us = node_accum.compute_us;
    row.comm_us = node_accum.comm_us;
    row.idle_us = node_accum.idle_us;
    row.total_us = total_us;
    row.avg_compute_us = node_accum.compute_us / average_divisor;
    row.avg_comm_us = node_accum.comm_us / average_divisor;
    row.avg_idle_us = node_accum.idle_us / average_divisor;
    row.avg_total_us = total_us / average_divisor;
    row.self_us = node_accum.self_us;
    row.aux_event_count = node_accum.aux_event_count;
    row.aux_us = node_accum.aux_us;

    if (occurrence_found != first_occurrences.end()) {
      const StructuralNodeOccurrence& occurrence =
          structural_node_occurrence(tree, occurrence_found->second);
      row.sibling_order = occurrence.edge_order;
      row.loop_depth = loop_depth_for_occurrence(tree, occurrence.id);
      if (occurrence.parent_occurrence_id.valid()) {
        const StructuralNodeOccurrence& parent =
            structural_node_occurrence(tree, occurrence.parent_occurrence_id);
        const StructuralNodeDef& parent_def = structural_node_def(tree, parent.node_def_id);
        row.parent_node_id =
            node_id_for_def(parent_def, device_id, scope_node_ids_by_device);
        row.parent_local_node_id = parent_def.local_node_id;
      }
    }
    rows.nodes.push_back(std::move(row));
  }

  std::set<std::pair<std::string, std::string>> seen_edges;
  for (const StructuralOccurrenceEdge& edge : tree.edges) {
    const StructuralNodeOccurrence& parent =
        structural_node_occurrence(tree, edge.parent_occurrence_id);
    const StructuralNodeOccurrence& child =
        structural_node_occurrence(tree, edge.child_occurrence_id);
    const StructuralNodeDef& parent_def = structural_node_def(tree, parent.node_def_id);
    const StructuralNodeDef& child_def = structural_node_def(tree, child.node_def_id);
    const std::pair<std::string, std::string> key{
        node_id_for_def(parent_def, device_id, scope_node_ids_by_device),
        node_id_for_def(child_def, device_id, scope_node_ids_by_device)};
    if (!seen_edges.insert(key).second) {
      continue;
    }
    SemanticEdgeSqlRow row;
    row.parent_node_id = key.first;
    row.child_node_id = key.second;
    row.tree_id = tree_id;
    row.db_idx = db_idx;
    row.device_id = device_id;
    row.view_name = view_name;
    row.tree_kind = "semantic";
    row.edge_order = edge.edge_order;
    row.edge_kind = "child";
    rows.edges.push_back(std::move(row));
  }

  return rows;
}

SemanticTreeSqlRows build_native_structural_semantic_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx,
    std::string tree_id,
    std::string view_name) {
  const std::vector<NativeStructuralDevicePartition> partitions =
      partition_structural_projection_tokens_by_device(ir);
  const AuxAttributionSqlRows aux_rows =
      build_aux_attribution_sql_rows(ir, db_idx);
  const bool scope_node_ids = partitions.size() > 1;
  SemanticTreeSqlRows rows;
  for (const NativeStructuralDevicePartition& partition : partitions) {
    const StructuralOccurrenceGraph tree =
        build_structural_occurrence_graph_from_tokens(partition.tokens);
    const std::string device_tree_id =
        partitions.size() == 1
            ? tree_id
            : tree_id + "-d" + std::to_string(partition.device_id);
    const SemanticTreeSqlRows device_rows =
        build_structural_semantic_sql_rows(
            tree, partition.tokens, aux_rows, db_idx, device_tree_id,
            view_name, scope_node_ids);
    rows.trees.insert(rows.trees.end(), device_rows.trees.begin(),
                      device_rows.trees.end());
    rows.nodes.insert(rows.nodes.end(), device_rows.nodes.begin(),
                      device_rows.nodes.end());
    rows.edges.insert(rows.edges.end(), device_rows.edges.begin(),
                      device_rows.edges.end());
  }
  return rows;
}

std::vector<SemanticTreeHeaderSqlRow> split_semantic_tree_catalog_sql_rows(
    const SemanticTreeSqlRows& rows) {
  return rows.trees;
}

SemanticGraphSqlRows split_semantic_graph_sql_rows(
    const SemanticTreeSqlRows& rows) {
  SemanticGraphSqlRows out;
  out.nodes = rows.nodes;
  out.edges = rows.edges;
  return out;
}

}  // namespace traceloom::compat
