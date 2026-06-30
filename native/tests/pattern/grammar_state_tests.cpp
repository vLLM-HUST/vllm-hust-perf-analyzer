#include "traceloom/adapters/fixture_adapter.h"
#include "traceloom/pattern/grammar_state.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>
#include <string>

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

  return 0;
}
