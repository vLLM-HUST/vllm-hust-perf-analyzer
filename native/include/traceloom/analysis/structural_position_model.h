#pragma once

#include <cstdint>
#include <vector>

#include "traceloom/analysis/structural_occurrence_graph.h"

namespace traceloom {

// A composite Position exposes one or more locally ordered structural slots.
// Repeated realizations of one slot are represented by member_order rather
// than by inventing additional slots.
struct StructuralPositionRefinement {
  StructuralNodeDefId parent_position_id;
  std::uint32_t slot_ordinal = 0;
  StructuralNodeDefId child_position_id;
};

enum class StructuralPositionMemberKind {
  kChildOccurrence,
  kTerminalToken,
};

const char* structural_position_member_kind_name(
    StructuralPositionMemberKind kind) noexcept;

struct StructuralPositionMember {
  StructuralNodeOccurrenceId parent_occurrence_id;
  // Composite slots are one-based. Zero is the distinguished terminal slot
  // of an atomic Position.
  std::uint32_t slot_ordinal = 0;
  // One-based order among measured realizations of the same structural slot.
  std::uint32_t member_order = 0;
  StructuralPositionMemberKind kind =
      StructuralPositionMemberKind::kChildOccurrence;
  StructuralNodeDefId child_position_id;
  StructuralNodeOccurrenceId child_occurrence_id;
  std::uint32_t terminal_token_ordinal = 0;
};

struct StructuralPositionModel {
  std::vector<StructuralPositionRefinement> refinements;
  std::vector<StructuralPositionMember> members;
};

// Contracts the alternating definition/occurrence graph into the HPO kernel:
// ordered Position refinement, Position Occurrences, and direct measured
// membership. This validates that every Occurrence of one Position conforms
// to the same local structural slots.
StructuralPositionModel build_structural_position_model(
    const StructuralOccurrenceGraph& graph, std::uint32_t token_count);

}  // namespace traceloom
