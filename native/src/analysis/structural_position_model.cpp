#include "traceloom/analysis/structural_position_model.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "traceloom/analysis/structural_occurrence_builder.h"

namespace traceloom {
namespace {

using OccurrenceChildren =
    std::vector<std::vector<const StructuralNodeOccurrence*>>;

OccurrenceChildren ordered_children(
    const StructuralOccurrenceGraph& graph) {
  OccurrenceChildren out(graph.occurrences.size());
  for (const StructuralNodeOccurrence& occurrence : graph.occurrences) {
    if (!occurrence.parent_occurrence_id.valid()) {
      continue;
    }
    out.at(occurrence.parent_occurrence_id.value()).push_back(&occurrence);
  }
  for (auto& children : out) {
    std::sort(children.begin(), children.end(),
              [](const StructuralNodeOccurrence* lhs,
                 const StructuralNodeOccurrence* rhs) {
                if (lhs->edge_order != rhs->edge_order) {
                  return lhs->edge_order < rhs->edge_order;
                }
                return lhs->id < rhs->id;
              });
  }
  return out;
}

void require_dense_edge_order(
    const std::vector<const StructuralNodeOccurrence*>& children,
    const char* context) {
  for (std::size_t index = 0; index < children.size(); ++index) {
    if (children[index]->edge_order != index + 1) {
      throw std::invalid_argument(std::string(context) +
                                  " child edge order is not dense");
    }
  }
}

using PositionSignature = std::vector<StructuralNodeDefId>;

void commit_or_compare_signature(
    std::map<StructuralNodeDefId::value_type, PositionSignature>& signatures,
    StructuralNodeDefId position_id,
    const PositionSignature& signature) {
  const auto inserted = signatures.emplace(position_id.value(), signature);
  if (!inserted.second && inserted.first->second != signature) {
    throw std::invalid_argument(
        "structural Position occurrences disagree on refinement slots");
  }
}

void append_refinements_once(
    StructuralPositionModel& out,
    std::map<StructuralNodeDefId::value_type, PositionSignature>& signatures,
    StructuralNodeDefId parent_position_id,
    const PositionSignature& signature) {
  const bool first = signatures.find(parent_position_id.value()) ==
                     signatures.end();
  commit_or_compare_signature(signatures, parent_position_id, signature);
  if (!first) {
    return;
  }
  for (std::size_t index = 0; index < signature.size(); ++index) {
    out.refinements.push_back(StructuralPositionRefinement{
        parent_position_id, static_cast<std::uint32_t>(index + 1),
        signature[index]});
  }
}

void append_sequence_members(
    StructuralPositionModel& out,
    std::map<StructuralNodeDefId::value_type, PositionSignature>& signatures,
    const StructuralNodeOccurrence& parent,
    const std::vector<const StructuralNodeOccurrence*>& children) {
  require_dense_edge_order(children, "sequence Position");
  PositionSignature signature;
  signature.reserve(children.size());
  for (const StructuralNodeOccurrence* child : children) {
    signature.push_back(child->node_def_id);
  }
  append_refinements_once(out, signatures, parent.node_def_id, signature);

  for (std::size_t index = 0; index < children.size(); ++index) {
    const StructuralNodeOccurrence& child = *children[index];
    StructuralPositionMember member;
    member.parent_occurrence_id = parent.id;
    member.slot_ordinal = static_cast<std::uint32_t>(index + 1);
    member.member_order = 1;
    member.kind = StructuralPositionMemberKind::kChildOccurrence;
    member.child_position_id = child.node_def_id;
    member.child_occurrence_id = child.id;
    out.members.push_back(member);
  }
}

void append_repeat_members(
    StructuralPositionModel& out,
    std::map<StructuralNodeDefId::value_type, PositionSignature>& signatures,
    const StructuralOccurrenceGraph& graph,
    const StructuralNodeOccurrence& parent,
    const std::vector<const StructuralNodeOccurrence*>& children) {
  const StructuralNodeDef& parent_def =
      structural_node_def(graph, parent.node_def_id);
  require_dense_edge_order(children, "repeat Position");

  std::map<std::uint32_t, std::vector<const StructuralNodeOccurrence*>>
      by_iteration;
  for (const StructuralNodeOccurrence* child : children) {
    if (child->repeat_iteration == 0 ||
        child->repeat_iteration > parent_def.repeat_count) {
      throw std::invalid_argument(
          "repeat Position member has invalid repeat iteration");
    }
    by_iteration[child->repeat_iteration].push_back(child);
  }
  if (by_iteration.size() != parent_def.repeat_count) {
    throw std::invalid_argument(
        "repeat Position does not realize every declared iteration");
  }

  PositionSignature signature;
  for (std::uint32_t iteration = 1; iteration <= parent_def.repeat_count;
       ++iteration) {
    const auto found = by_iteration.find(iteration);
    if (found == by_iteration.end() || found->second.empty()) {
      throw std::invalid_argument(
          "repeat Position has an empty or missing iteration");
    }
    PositionSignature iteration_signature;
    iteration_signature.reserve(found->second.size());
    for (const StructuralNodeOccurrence* child : found->second) {
      iteration_signature.push_back(child->node_def_id);
    }
    if (iteration == 1) {
      signature = std::move(iteration_signature);
    } else if (signature != iteration_signature) {
      throw std::invalid_argument(
          "repeat Position iterations disagree on refinement slots");
    }
  }
  append_refinements_once(out, signatures, parent.node_def_id, signature);

  for (std::uint32_t iteration = 1; iteration <= parent_def.repeat_count;
       ++iteration) {
    const auto& iteration_children = by_iteration.at(iteration);
    for (std::size_t slot = 0; slot < iteration_children.size(); ++slot) {
      const StructuralNodeOccurrence& child = *iteration_children[slot];
      StructuralPositionMember member;
      member.parent_occurrence_id = parent.id;
      member.slot_ordinal = static_cast<std::uint32_t>(slot + 1);
      member.member_order = iteration;
      member.kind = StructuralPositionMemberKind::kChildOccurrence;
      member.child_position_id = child.node_def_id;
      member.child_occurrence_id = child.id;
      out.members.push_back(member);
    }
  }
}

}  // namespace

const char* structural_position_member_kind_name(
    StructuralPositionMemberKind kind) noexcept {
  switch (kind) {
    case StructuralPositionMemberKind::kChildOccurrence:
      return "child_occurrence";
    case StructuralPositionMemberKind::kTerminalToken:
      return "terminal_token";
  }
  return "terminal_token";
}

StructuralPositionModel build_structural_position_model(
    const StructuralOccurrenceGraph& graph, std::uint32_t token_count) {
  validate_structural_occurrence_graph_or_throw(graph, token_count);
  const OccurrenceChildren children = ordered_children(graph);
  StructuralPositionModel out;
  std::map<StructuralNodeDefId::value_type, PositionSignature> signatures;

  for (const StructuralNodeOccurrence& occurrence : graph.occurrences) {
    const StructuralNodeDef& def =
        structural_node_def(graph, occurrence.node_def_id);
    const auto& occurrence_children = children.at(occurrence.id.value());
    switch (def.kind) {
      case StructuralNodeKind::kSeq:
        append_sequence_members(out, signatures, occurrence,
                                occurrence_children);
        break;
      case StructuralNodeKind::kRepeat:
        append_repeat_members(out, signatures, graph, occurrence,
                              occurrence_children);
        break;
      case StructuralNodeKind::kAtom: {
        commit_or_compare_signature(signatures, occurrence.node_def_id, {});
        StructuralPositionMember member;
        member.parent_occurrence_id = occurrence.id;
        member.slot_ordinal = 0;
        member.member_order = 1;
        member.kind = StructuralPositionMemberKind::kTerminalToken;
        member.terminal_token_ordinal = occurrence.token_start_ordinal;
        out.members.push_back(member);
        break;
      }
    }
  }
  return out;
}

}  // namespace traceloom
