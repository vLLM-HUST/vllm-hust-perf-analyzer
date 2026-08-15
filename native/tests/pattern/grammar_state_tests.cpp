#include "traceloom/adapters/fixture_adapter.h"
#include "traceloom/analysis/structural_occurrence_builder.h"
#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/pattern/grammar_state.h"
#include "traceloom/testing/test_util.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

traceloom::NativeIr make_ir() {
  using namespace traceloom;

  FixtureInput input;
  input.tokens = {
      FixtureToken{"A", AnchorKind::kDeviceEvent, 0, 0, 0, 10},
      FixtureToken{"A", AnchorKind::kDeviceEvent, 0, 0, 10, 20},
      FixtureToken{"B", AnchorKind::kDeviceEvent, 0, 0, 20, 30},
      FixtureToken{"C", AnchorKind::kDeviceEvent, 0, 0, 30, 40},
      FixtureToken{"C", AnchorKind::kDeviceEvent, 0, 0, 40, 50},
  };
  input.protected_intervals = {
      FixtureProtectedInterval{ProtectedIntervalKind::kGraphReplayUnit,
                               BoundaryPolicy::kNoCross, 1, 3},
  };
  return FixtureAdapter(input).load();
}

traceloom::NativeIr make_exact_replay_ir() {
  using namespace traceloom;

  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "exact_replay", "TASK", 0);
  const SymbolId h = ir.symbols.intern("ACLH");
  const SymbolId l = ir.symbols.intern("ACLL");
  const SymbolId t = ir.symbols.intern("ACLT");
  const SymbolId eager = ir.symbols.intern("Eager");
  const GraphTemplateId graph_template =
      ir.graph_templates.append(source, 77, 3);
  const GraphLaunchOccurrenceId launch =
      ir.graph_launch_occurrences.append(
          source, source, 0, 1, 1, 1, -1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 0, 30, 0,
          GraphLaunchMatchPolicy::kNotifyCompletionAdjacent);
  const ReplayCompositionCandidateId candidate =
      ir.replay_composition_candidates.append(
          source, 0, launch, launch, 12, 0, 3, 4, 0, 88,
          ReplayCompositionIdentityPolicy::kGraphConnection,
          ReplayCompositionOrderPolicy::kHostSubmissionOrder,
          ReplayCompositionShapePolicy::kHeadRepeatedLayerTail,
          ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix);
  const ReplayCompositionRegionId region0 =
      ir.replay_composition_regions.append(
          candidate, 0, launch, launch, 0, 30, 3, 3,
          ReplayCompositionRegionStatus::kRecognizedCompletePattern);
  const ReplayCompositionRegionId region1 =
      ir.replay_composition_regions.append(
          candidate, 1, launch, launch, 40, 70, 3, 3,
          ReplayCompositionRegionStatus::kRecognizedCompletePattern);
  const ReplayCompositionRegionId region2 =
      ir.replay_composition_regions.append(
          candidate, 2, launch, launch, 80, 110, 3, 3,
          ReplayCompositionRegionStatus::kRecognizedCompletePattern);
  const ReplayCompositionRegionId region3 =
      ir.replay_composition_regions.append(
          candidate, 3, launch, launch, 120, 150, 3, 3,
          ReplayCompositionRegionStatus::kRecognizedCompletePattern);
  const ReplayUnitId unit0 = ir.replay_units.append(
      graph_template, source, AnchorId::invalid(), AnchorId::invalid(),
      TraceEventId::invalid(), region0);
  const ReplayUnitId unit1 = ir.replay_units.append(
      graph_template, source, AnchorId::invalid(), AnchorId::invalid(),
      TraceEventId::invalid(), region1);
  const ReplayUnitId unit2 = ir.replay_units.append(
      graph_template, source, AnchorId::invalid(), AnchorId::invalid(),
      TraceEventId::invalid(), region2);
  const ReplayUnitId unit3 = ir.replay_units.append(
      graph_template, source, AnchorId::invalid(), AnchorId::invalid(),
      TraceEventId::invalid(), region3);

  const auto append_token = [&](SymbolId symbol, AnchorKind kind,
                                ReplayUnitId unit, std::int64_t start_ns) {
    const AnchorId anchor = ir.anchors.append(
        source, TraceEventId::invalid(), unit, kind, symbol, 0, 0, start_ns,
        start_ns + 5);
    ir.tokens.append(anchor, symbol, 0,
                     static_cast<std::uint32_t>(ir.tokens.size()), start_ns,
                     start_ns + 5);
    return anchor;
  };
  const AnchorId h0 = append_token(h, AnchorKind::kGraphH, unit0, 0);
  append_token(l, AnchorKind::kGraphL, unit0, 10);
  const AnchorId t0 = append_token(t, AnchorKind::kGraphT, unit0, 20);
  append_token(eager, AnchorKind::kDeviceEvent, ReplayUnitId::invalid(), 30);
  const AnchorId h1 = append_token(h, AnchorKind::kGraphH, unit1, 40);
  append_token(l, AnchorKind::kGraphL, unit1, 50);
  const AnchorId t1 = append_token(t, AnchorKind::kGraphT, unit1, 60);
  append_token(eager, AnchorKind::kDeviceEvent, ReplayUnitId::invalid(), 70);
  const AnchorId h2 = append_token(h, AnchorKind::kGraphH, unit2, 80);
  append_token(l, AnchorKind::kGraphL, unit2, 90);
  const AnchorId t2 = append_token(t, AnchorKind::kGraphT, unit2, 100);
  append_token(eager, AnchorKind::kDeviceEvent, ReplayUnitId::invalid(), 110);
  const AnchorId h3 = append_token(h, AnchorKind::kGraphH, unit3, 120);
  append_token(l, AnchorKind::kGraphL, unit3, 130);
  const AnchorId t3 = append_token(t, AnchorKind::kGraphT, unit3, 140);
  append_token(eager, AnchorKind::kDeviceEvent, ReplayUnitId::invalid(), 150);
  ir.replay_units.set_anchor_bounds(unit0, h0, t0);
  ir.replay_units.set_anchor_bounds(unit1, h1, t1);
  ir.replay_units.set_anchor_bounds(unit2, h2, t2);
  ir.replay_units.set_anchor_bounds(unit3, h3, t3);
  ir.protected_intervals.append(
      ProtectedIntervalKind::kGraphReplayUnit, BoundaryPolicy::kNoCross,
      TokenId(0), TokenId(2), h0, t0, source);
  ir.protected_intervals.append(
      ProtectedIntervalKind::kGraphReplayUnit, BoundaryPolicy::kNoCross,
      TokenId(4), TokenId(6), h1, t1, source);
  ir.protected_intervals.append(
      ProtectedIntervalKind::kGraphReplayUnit, BoundaryPolicy::kNoCross,
      TokenId(8), TokenId(10), h2, t2, source);
  ir.protected_intervals.append(
      ProtectedIntervalKind::kGraphReplayUnit, BoundaryPolicy::kNoCross,
      TokenId(12), TokenId(14), h3, t3, source);
  return ir;
}

traceloom::NativeIr make_generic_exact_replay_ir() {
  using namespace traceloom;

  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "generic_exact_replay", "TASK", 0);
  const SymbolId graph = ir.symbols.intern("ACLG");
  const GraphTemplateId graph_template =
      ir.graph_templates.append(source, 91, 1);
  const GraphLaunchOccurrenceId launch =
      ir.graph_launch_occurrences.append(
          source, source, 0, 1, 1, 1, -1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 0, 10, 0,
          GraphLaunchMatchPolicy::kNotifyCompletionAdjacent);
  const ReplayCompositionCandidateId candidate =
      ir.replay_composition_candidates.append(
          source, 0, launch, launch, 4, 0, 1, 4, 0, 92,
          ReplayCompositionIdentityPolicy::kGraphConnection,
          ReplayCompositionOrderPolicy::kHostSubmissionOrder,
          ReplayCompositionShapePolicy::kUnclassified,
          ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix);

  for (std::uint32_t index = 0; index < 4; ++index) {
    const std::int64_t start_ns = static_cast<std::int64_t>(index) * 20;
    const ReplayCompositionRegionId region =
        ir.replay_composition_regions.append(
            candidate, index, launch, launch, start_ns, start_ns + 10, 1, 1,
            ReplayCompositionRegionStatus::kRecognizedCompletePattern);
    const ReplayUnitId unit = ir.replay_units.append(
        graph_template, source, AnchorId::invalid(), AnchorId::invalid(),
        TraceEventId::invalid(), region);
    const AnchorId anchor = ir.anchors.append(
        source, TraceEventId::invalid(), unit,
        AnchorKind::kGraphReplayUnit, graph, 0, 0, start_ns, start_ns + 10);
    const TokenId token = ir.tokens.append(
        anchor, graph, 0, index, start_ns, start_ns + 10);
    ir.replay_units.set_anchor_bounds(unit, anchor, anchor);
    ir.protected_intervals.append(
        ProtectedIntervalKind::kGraphReplayUnit, BoundaryPolicy::kNoCross,
        token, token, anchor, anchor, source);
  }
  return ir;
}

std::vector<traceloom::StructuralProjectionToken> structural_tokens_for_ir(
    const traceloom::NativeIr& ir) {
  using namespace traceloom;
  std::vector<StructuralProjectionToken> out;
  for (const TokenRow& token : ir.tokens.rows()) {
    const AnchorRow& anchor = ir.anchors.row(token.anchor_id);
    StructuralAnchorKind kind = StructuralAnchorKind::kExec;
    if (anchor.kind == AnchorKind::kGraphH) {
      kind = StructuralAnchorKind::kGraphH;
    } else if (anchor.kind == AnchorKind::kGraphL) {
      kind = StructuralAnchorKind::kGraphL;
    } else if (anchor.kind == AnchorKind::kGraphT) {
      kind = StructuralAnchorKind::kGraphT;
    }
    StructuralProjectionToken structural_token;
    structural_token.ordinal = token.sequence_index;
    structural_token.device_id = token.device_id;
    structural_token.symbol_id = token.symbol_id;
    structural_token.display_op = ir.symbols.value(token.symbol_id);
    structural_token.display_category =
        kind == StructuralAnchorKind::kExec ? "exec" : "graph";
    structural_token.anchor_kind = kind;
    structural_token.anchor_id = token.anchor_id;
    structural_token.start_ns = token.start_ns;
    structural_token.end_ns = token.end_ns;
    out.push_back(std::move(structural_token));
  }
  return out;
}

bool has_value(const std::vector<std::string>& values,
               const std::string& value) {
  for (const std::string& item : values) {
    if (item == value) {
      return true;
    }
  }
  return false;
}

bool semantic_nodes_equal(const traceloom::GlobalGrammarState& lhs,
                          const traceloom::GlobalGrammarState& rhs) {
  if (lhs.nodes.size() != rhs.nodes.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.nodes.size(); ++index) {
    if (lhs.nodes[index].symbol_id != rhs.nodes[index].symbol_id) {
      return false;
    }
    if (lhs.nodes[index].source_begin_token_index !=
        rhs.nodes[index].source_begin_token_index) {
      return false;
    }
    if (lhs.nodes[index].source_end_token_index_exclusive !=
        rhs.nodes[index].source_end_token_index_exclusive) {
      return false;
    }
    if (lhs.nodes[index].local_prev != rhs.nodes[index].local_prev) {
      return false;
    }
    if (lhs.nodes[index].local_next != rhs.nodes[index].local_next) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir = make_ir();
  GrammarStateConfig config;
  config.mode = GrammarAlgorithmMode::kAnalysisQualityV1;
  config.target_nodes_per_chunk = 2;
  config.worker_count = 3;
  const GlobalGrammarState state = build_initial_grammar_state(ir, config);

  require(state.stage == GrammarStage::kRunFold);
  require(state.generation == 0);
  require(state.nodes.size() == 5);
  require(state.live_node_count == 5);
  require(state.chunks.size() == 3);
  require(state.boundary_summaries.size() == 3);
  require(state.protected_intervals.size() == 1);
  require(state.macro_defs.empty());
  require(state.next_macro_symbol_id.value() == 3);
  require(state.target_nodes_per_chunk == 2);
  require(state.worker_count == 3);

  require(state.metadata.mode == GrammarAlgorithmMode::kAnalysisQualityV1);
  require(std::string(grammar_algorithm_mode_name(state.metadata.mode)) ==
          "analysis_quality_v1");
  require(has_value(state.metadata.producer_sequence, "AdjacentRunProducer"));
  require(has_value(state.metadata.producer_sequence, "PairGrammarProducer"));
  require(has_value(state.metadata.known_deltas,
                    "generic_repeated_block_skipped_native_v1"));

  require(state.nodes[0].local_prev == GrammarNodeId::invalid());
  require(state.nodes[0].local_next == GrammarNodeId(1));
  require(state.nodes[1].local_next == GrammarNodeId::invalid());
  require(state.nodes[2].local_prev == GrammarNodeId::invalid());
  require(state.nodes[4].local_prev == GrammarNodeId::invalid());
  require(state.nodes[4].local_next == GrammarNodeId::invalid());
  require(state.nodes[0].owner_chunk_id == GrammarChunkId(0));
  require(state.nodes[2].owner_chunk_id == GrammarChunkId(1));
  require(state.nodes[4].owner_chunk_id == GrammarChunkId(2));

  require(state.chunks[0].owner_worker_id == 0);
  require(state.chunks[1].owner_worker_id == 1);
  require(state.chunks[2].owner_worker_id == 2);
  require(state.chunks[0].live_count == 2);
  require(state.chunks[1].live_count == 2);
  require(state.chunks[2].live_count == 1);

  const BoundarySummary& first = state.boundary_summaries[0];
  require(first.first_live_node_id == GrammarNodeId(0));
  require(first.last_live_node_id == GrammarNodeId(1));
  require(first.prefix_run_len == 2);
  require(first.suffix_run_len == 2);
  require(first.all_same_symbol);
  require(first.prev_chunk_id == GrammarChunkId::invalid());
  require(first.next_chunk_id == GrammarChunkId(1));

  const BoundarySummary& second = state.boundary_summaries[1];
  require(second.first_live_node_id == GrammarNodeId(2));
  require(second.last_live_node_id == GrammarNodeId(3));
  require(second.prefix_run_len == 1);
  require(second.suffix_run_len == 1);
  require(!second.all_same_symbol);
  require(second.prev_chunk_id == GrammarChunkId(0));
  require(second.next_chunk_id == GrammarChunkId(2));

  GrammarStateConfig one_worker_config = config;
  one_worker_config.worker_count = 1;
  const GlobalGrammarState one_worker =
      build_initial_grammar_state(ir, one_worker_config);
  require(semantic_nodes_equal(state, one_worker));
  require(one_worker.chunks.size() == state.chunks.size());
  require(one_worker.chunks[1].owner_worker_id == 0);

  GrammarStateConfig compat_config = config;
  compat_config.mode = GrammarAlgorithmMode::kPythonCompatFull;
  const GlobalGrammarState compat =
      build_initial_grammar_state(ir, compat_config);
  require(has_value(compat.metadata.producer_sequence,
                    "ExactRepeatedBlockProducer"));
  require(compat.metadata.known_deltas.empty());

  bool caught_bad_chunk = false;
  try {
    GrammarStateConfig bad = config;
    bad.target_nodes_per_chunk = 0;
    (void)build_initial_grammar_state(ir, bad);
  } catch (const std::invalid_argument&) {
    caught_bad_chunk = true;
  }
  require(caught_bad_chunk);

  bool caught_bad_worker = false;
  try {
    GrammarStateConfig bad = config;
    bad.worker_count = 0;
    (void)build_initial_grammar_state(ir, bad);
  } catch (const std::invalid_argument&) {
    caught_bad_worker = true;
  }
  require(caught_bad_worker);

  NativeIr exact_ir = make_exact_replay_ir();
  GlobalGrammarState exact_state =
      build_initial_grammar_state(exact_ir, config);
  require(exact_state.nodes.size() == 8, "semantic seed node count");
  require(exact_state.live_node_count == 8, "semantic seed live count");
  require(exact_state.protected_intervals.empty(),
          "semantic seed consumed intervals");
  require(exact_state.macro_defs.size() == 1,
          "semantic seed macro count");
  require(exact_state.macro_defs[0].level == MacroLevel::kSemantic,
          "semantic seed macro level");
  require(exact_state.macro_defs[0].display_label == "ReplayUnit T1",
          "semantic seed label");
  require(exact_state.macro_defs[0].rhs_symbols.size() == 3,
          "semantic seed rhs size");
  require(exact_state.macro_defs[0].replace_count == 4,
          "semantic seed replace count");
  require(exact_state.nodes[0].source_begin_token_index == 0 &&
              exact_state.nodes[0].source_end_token_index_exclusive == 3 &&
              exact_state.nodes[2].source_begin_token_index == 4 &&
              exact_state.nodes[2].source_end_token_index_exclusive == 7,
          "semantic replay seed lost exact token spans");

  const GrammarEngineResult exact_result =
      run_grammar_state_machine(exact_state);
  require(exact_result.ok(), "semantic grammar engine result");
  require(exact_state.stage == GrammarStage::kDone,
          "semantic grammar engine stage");
  require(exact_state.live_node_count == 1,
          "semantic grammar outer repeat compression");
  const StructuralOccurrenceGraph exact_tree = build_structural_occurrence_graph_from_grammar_state(
      structural_tokens_for_ir(exact_ir), exact_state);
  const auto semantic_def = std::find_if(
      exact_tree.node_defs.begin(), exact_tree.node_defs.end(),
      [](const StructuralNodeDef& def) {
        return def.display_op == "ReplayUnit T1";
      });
  require(semantic_def != exact_tree.node_defs.end(),
          "semantic structural unit def");
  require(semantic_def->kind == StructuralNodeKind::kSeq,
          "semantic structural unit kind");
  require(semantic_def->display_category == "graph_unit",
          "semantic structural unit category");
  require(structural_occurrence_count_for_def(exact_tree, semantic_def->id) == 4,
          "semantic structural unit occurrences");
  require(std::any_of(exact_tree.node_defs.begin(), exact_tree.node_defs.end(),
                      [](const StructuralNodeDef& def) {
                        return def.kind == StructuralNodeKind::kRepeat &&
                               def.repeat_count == 4;
                      }),
          "semantic structural outer repeat");

  NativeIr generic_exact_ir = make_generic_exact_replay_ir();
  GlobalGrammarState generic_exact_state =
      build_initial_grammar_state(generic_exact_ir, config);
  require(generic_exact_state.nodes.size() == 4 &&
              generic_exact_state.live_node_count == 4 &&
              generic_exact_state.protected_intervals.empty(),
          "generic exact replay intervals were not consumed");
  require(generic_exact_state.macro_defs.size() == 1 &&
              generic_exact_state.macro_defs[0].rhs_symbols.size() == 1 &&
              generic_exact_state.macro_defs[0].replace_count == 4,
          "generic exact replay did not seed a shared semantic macro");
  const GrammarEngineResult generic_exact_result =
      run_grammar_state_machine(generic_exact_state);
  require(generic_exact_result.ok() &&
              generic_exact_state.live_node_count == 1,
          "generic exact replay did not participate in outer compression");

  return 0;
}
