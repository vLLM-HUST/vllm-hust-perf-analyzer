#include "traceloom/analysis/replay_body_pattern.h"
#include "traceloom/testing/test_util.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace {

using namespace traceloom;
using traceloom::testing::require;

ReplayAlignedCostAggregateRow aggregate(SymbolId identity,
                                        std::uint32_t position,
                                        std::uint32_t stream = 7,
                                        GraphLaunchBodyMemberRow::Kind kind =
                                            GraphLaunchBodyMemberRow::Kind::
                                                kCompute) {
  ReplayAlignedCostAggregateRow row;
  row.graph_template_id = GraphTemplateId(0);
  row.device_id = 0;
  row.slot_role = ReplayCompositionSlotRole::kCudaGraph;
  row.aggregation_scope = ReplayAggregationScope::kRoleCollapsed;
  row.replay_body_template_id = ReplayBodyTemplateId(0);
  row.stream_id = stream;
  row.within_stream_position = position;
  row.identity_symbol_id = identity;
  row.kind = kind;
  row.member_occurrence_count = 5;
  row.replay_unit_count = 5;
  row.launch_member_count = 5;
  row.kind_consistent = true;
  row.lane_consistent = true;
  row.distribution_supported = true;
  row.duration_p25_ns = 8 + position;
  row.duration_median_ns = 10 + position;
  row.duration_p75_ns = 12 + position;
  row.scheduled_work_share_ppm = 1000 + position;
  row.scheduled_work_share_supported = true;
  return row;
}

ReplayInternalCostMapResult repeated_body(SymbolId head,
                                          SymbolId a,
                                          SymbolId c,
                                          SymbolId b,
                                          SymbolId tail) {
  ReplayInternalCostMapResult cost;
  std::vector<SymbolId> symbols{head};
  for (std::size_t group = 0; group < 6; ++group) {
    for (std::size_t repeat = 0; repeat < 3; ++repeat) {
      symbols.push_back(a);
      symbols.push_back(c);
    }
    symbols.push_back(b);
  }
  symbols.push_back(tail);
  for (std::size_t index = 0; index < symbols.size(); ++index) {
    cost.aggregates.push_back(
        aggregate(symbols[index], static_cast<std::uint32_t>(index)));
  }
  return cost;
}

std::vector<std::tuple<StructuralNodeKind, std::uint32_t, std::string,
                       std::string>>
definition_signature(const StructuralOccurrenceGraph& graph) {
  std::vector<std::tuple<StructuralNodeKind, std::uint32_t, std::string,
                         std::string>> out;
  for (const StructuralNodeDef& def : graph.node_defs) {
    out.emplace_back(def.kind, def.repeat_count, def.display_op,
                     def.display_category);
  }
  return out;
}

void test_nested_replay_body_recovery_is_deterministic() {
  NativeIr ir;
  const SymbolId head = ir.symbols.intern("Head");
  const SymbolId a = ir.symbols.intern("LayerA");
  const SymbolId c = ir.symbols.intern("LayerAContinuation");
  const SymbolId b = ir.symbols.intern("LayerB");
  const SymbolId tail = ir.symbols.intern("Tail");
  const ReplayInternalCostMapResult cost =
      repeated_body(head, a, c, b, tail);

  ReplayBodyPatternConfig serial;
  serial.worker_count = 1;
  serial.target_nodes_per_chunk = 4;
  const ReplayBodyPatternResult one =
      build_replay_body_patterns(ir, cost, serial);
  require(one.domains.size() == 1);
  require(one.supported_domain_count == 1 &&
          one.rejected_domain_count == 0);
  require(one.domains[0].support_status ==
          ReplayBodyPatternSupportStatus::kSupported);
  require(one.domains[0].aggregate_indices.size() == 44);

  bool saw_repeat_six = false;
  bool saw_repeat_three = false;
  for (const StructuralNodeDef& def : one.domains[0].graph.node_defs) {
    if (def.kind == StructuralNodeKind::kRepeat && def.repeat_count == 6) {
      saw_repeat_six = true;
    }
    if (def.kind == StructuralNodeKind::kRepeat && def.repeat_count == 3) {
      saw_repeat_three = true;
    }
  }
  require(saw_repeat_six, "recovers six repeated composite blocks");
  require(saw_repeat_three, "recovers the adjacent A x3 body");

  ReplayBodyPatternConfig parallel = serial;
  parallel.worker_count = 4;
  const ReplayBodyPatternResult four =
      build_replay_body_patterns(ir, cost, parallel);
  require(four.supported_domain_count == 1);
  require(definition_signature(one.domains[0].graph) ==
              definition_signature(four.domains[0].graph),
          "worker count does not change recovered definitions");
  require(one.domains[0].graph.occurrences.size() ==
              four.domains[0].graph.occurrences.size(),
          "worker count does not change occurrence population");
  for (std::size_t index = 0;
       index < one.domains[0].graph.occurrences.size(); ++index) {
    const StructuralNodeOccurrence& lhs =
        one.domains[0].graph.occurrences[index];
    const StructuralNodeOccurrence& rhs =
        four.domains[0].graph.occurrences[index];
    require(lhs.token_start_ordinal == rhs.token_start_ordinal &&
            lhs.token_end_ordinal == rhs.token_end_ordinal &&
            lhs.repeat_iteration == rhs.repeat_iteration);
  }
}

void test_streams_are_independent_domains() {
  NativeIr ir;
  const SymbolId a = ir.symbols.intern("A");
  ReplayInternalCostMapResult cost;
  cost.aggregates.push_back(aggregate(a, 0, 7));
  cost.aggregates.push_back(aggregate(a, 1, 7));
  cost.aggregates.push_back(aggregate(a, 0, 9));
  cost.aggregates.push_back(aggregate(a, 1, 9));
  const ReplayBodyPatternResult result =
      build_replay_body_patterns(ir, cost);
  require(result.domains.size() == 2);
  require(result.supported_domain_count == 2);
  require(result.domains[0].key.stream_id == 7);
  require(result.domains[1].key.stream_id == 9);
  require(result.domains[0].aggregate_indices.size() == 2);
  require(result.domains[1].aggregate_indices.size() == 2);
}

void test_sparse_or_inconsistent_domains_fail_closed() {
  NativeIr ir;
  const SymbolId a = ir.symbols.intern("A");
  ReplayInternalCostMapResult sparse;
  sparse.aggregates.push_back(aggregate(a, 0));
  sparse.aggregates.push_back(aggregate(a, 2));
  ReplayBodyPatternResult result = build_replay_body_patterns(ir, sparse);
  require(result.supported_domain_count == 0 &&
          result.rejected_domain_count == 1);
  require(result.domains[0].reason_code ==
          "non_dense_position_sequence");
  require(result.domains[0].graph.node_defs.empty());

  ReplayInternalCostMapResult inconsistent;
  inconsistent.aggregates.push_back(aggregate(a, 0));
  inconsistent.aggregates[0].distribution_supported = false;
  result = build_replay_body_patterns(ir, inconsistent);
  require(result.domains[0].reason_code ==
          "inconsistent_position_evidence");
  require(result.domains[0].graph.occurrences.empty());
}

void test_member_kind_is_part_of_the_exact_alphabet() {
  NativeIr ir;
  const SymbolId same_identity = ir.symbols.intern("SameIdentity");
  ReplayInternalCostMapResult cost;
  cost.aggregates.push_back(aggregate(
      same_identity, 0, 7, GraphLaunchBodyMemberRow::Kind::kCompute));
  cost.aggregates.push_back(aggregate(
      same_identity, 1, 7,
      GraphLaunchBodyMemberRow::Kind::kCommunication));
  const ReplayBodyPatternResult result =
      build_replay_body_patterns(ir, cost);
  require(result.supported_domain_count == 1);
  bool saw_compute = false;
  bool saw_communication = false;
  for (const StructuralNodeDef& def : result.domains[0].graph.node_defs) {
    saw_compute = saw_compute || def.display_category == "compute";
    saw_communication =
        saw_communication || def.display_category == "communication";
    require(!(def.kind == StructuralNodeKind::kRepeat &&
              def.repeat_count == 2),
            "same identity with different kinds must not alias");
  }
  require(saw_compute && saw_communication);
}

void test_oversized_and_empty_inputs_are_typed() {
  NativeIr ir;
  ReplayInternalCostMapResult empty;
  empty.result_reason_codes.push_back("no_replay_units");
  ReplayBodyPatternResult result = build_replay_body_patterns(ir, empty);
  require(result.domains.empty());
  require(result.result_reason_codes.size() == 1 &&
          result.result_reason_codes[0] == "no_replay_units");

  ReplayInternalCostMapResult large;
  for (std::uint32_t index = 0; index < 5; ++index) {
    large.aggregates.push_back(
        aggregate(ir.symbols.intern("U" + std::to_string(index)), index));
  }
  ReplayBodyPatternConfig config;
  config.full_discovery_cap = 2;
  result = build_replay_body_patterns(ir, large, config);
  require(result.supported_domain_count == 0 &&
          result.rejected_domain_count == 1);
  require(result.domains[0].reason_code == "grammar_recovery_rejected");
  require(!result.issues.empty());
  require(result.issues[0].detail.find("sequence_too_large") !=
          std::string::npos);
}

}  // namespace

int main() {
  test_nested_replay_body_recovery_is_deterministic();
  test_streams_are_independent_domains();
  test_sparse_or_inconsistent_domains_fail_closed();
  test_member_kind_is_part_of_the_exact_alphabet();
  test_oversized_and_empty_inputs_are_typed();
  return 0;
}
