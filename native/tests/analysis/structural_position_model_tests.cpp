#include "traceloom/analysis/structural_position_model.h"

#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

#include "traceloom/analysis/structural_occurrence_builder.h"

namespace {

traceloom::StructuralProjectionToken token(std::uint32_t ordinal,
                                           std::uint32_t symbol,
                                           std::string label) {
  traceloom::StructuralProjectionToken out;
  out.ordinal = ordinal;
  out.device_id = 0;
  out.symbol_id = traceloom::SymbolId(symbol);
  out.display_op = std::move(label);
  out.display_category = "compute";
  out.anchor_kind = traceloom::StructuralAnchorKind::kExec;
  out.anchor_id = traceloom::AnchorId(ordinal);
  out.start_ns = ordinal * 10;
  out.end_ns = out.start_ns + 10;
  return out;
}

void test_repeat_uses_one_slot_with_ordered_members() {
  const std::vector<traceloom::StructuralProjectionToken> tokens{
      token(0, 0, "A"), token(1, 0, "A"), token(2, 1, "B")};
  const traceloom::StructuralOccurrenceGraph graph =
      traceloom::build_structural_occurrence_graph_from_tokens(tokens);
  const traceloom::StructuralPositionModel model =
      traceloom::build_structural_position_model(graph, tokens.size());

  const traceloom::StructuralNodeDefId repeat_position(1);
  std::vector<traceloom::StructuralPositionRefinement> repeat_slots;
  std::vector<traceloom::StructuralPositionMember> repeat_members;
  for (const auto& row : model.refinements) {
    if (row.parent_position_id == repeat_position) {
      repeat_slots.push_back(row);
    }
  }
  for (const auto& row : model.members) {
    if (row.parent_occurrence_id == traceloom::StructuralNodeOccurrenceId(1)) {
      repeat_members.push_back(row);
    }
  }
  assert(repeat_slots.size() == 1);
  assert(repeat_slots[0].slot_ordinal == 1);
  assert(repeat_members.size() == 2);
  assert(repeat_members[0].slot_ordinal == 1);
  assert(repeat_members[0].member_order == 1);
  assert(repeat_members[1].slot_ordinal == 1);
  assert(repeat_members[1].member_order == 2);
}

void test_atomic_positions_bind_terminal_tokens() {
  const std::vector<traceloom::StructuralProjectionToken> tokens{
      token(0, 0, "A"), token(1, 1, "B")};
  const traceloom::StructuralOccurrenceGraph graph =
      traceloom::build_structural_occurrence_graph_from_tokens(tokens);
  const traceloom::StructuralPositionModel model =
      traceloom::build_structural_position_model(graph, tokens.size());

  std::size_t terminal_count = 0;
  for (const auto& row : model.members) {
    if (row.kind == traceloom::StructuralPositionMemberKind::kTerminalToken) {
      assert(row.slot_ordinal == 0);
      assert(row.member_order == 1);
      ++terminal_count;
    }
  }
  assert(terminal_count == tokens.size());
}

void test_repeat_iterations_must_be_dense() {
  const std::vector<traceloom::StructuralProjectionToken> tokens{
      token(0, 0, "A"), token(1, 0, "A")};
  traceloom::StructuralOccurrenceGraph graph =
      traceloom::build_structural_occurrence_graph_from_tokens(tokens);
  graph.occurrences.at(3).repeat_iteration = 1;

  bool threw = false;
  try {
    (void)traceloom::build_structural_position_model(graph, tokens.size());
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);
}

}  // namespace

int main() {
  test_repeat_uses_one_slot_with_ordered_members();
  test_atomic_positions_bind_terminal_tokens();
  test_repeat_iterations_must_be_dense();
  return 0;
}
