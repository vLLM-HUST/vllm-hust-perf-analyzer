#include "traceloom/adapters/fixture_adapter.h"
#include "traceloom/pattern/grammar_snapshot.h"
#include "traceloom/pattern/grammar_state.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>

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

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  GrammarStateConfig config;
  config.target_nodes_per_chunk = 2;
  config.worker_count = 3;
  GlobalGrammarState state = build_initial_grammar_state(make_ir(), config);

  const GrammarSnapshot snapshot = freeze_grammar_snapshot(state);
  require(snapshot.generation == state.generation);
  require(snapshot.stage == GrammarStage::kRunFold);
  require(snapshot.size() == 5);
  require(snapshot.chunks.size() == 3);
  require(snapshot.boundary_summaries.size() == 3);
  require(snapshot.protected_intervals.size() == 1);

  for (std::size_t index = 0; index < snapshot.size(); ++index) {
    require(snapshot.nodes[index].node_id == GrammarNodeId(
                                                static_cast<GrammarNodeId::value_type>(
                                                    index)));
    require(snapshot.nodes[index].dense_index == index);
    require(dense_index_of_node(snapshot, snapshot.nodes[index].node_id) ==
            index);
  }
  require(snapshot.nodes[0].owner_chunk_id == GrammarChunkId(0));
  require(snapshot.nodes[2].owner_chunk_id == GrammarChunkId(1));
  require(snapshot.nodes[4].owner_chunk_id == GrammarChunkId(2));

  const DenseGrammarView dense = build_dense_grammar_view(snapshot);
  require(dense.generation == snapshot.generation);
  require(dense.size() == snapshot.size());
  require(dense.node_ids[3] == GrammarNodeId(3));
  require(dense.symbols[0] == snapshot.nodes[0].symbol_id);
  require(dense.start_ns[4] == 40);
  require(dense.end_ns[4] == 50);
  require(dense.source_begin_token_index[2] == 2);
  require(dense.source_end_token_index_exclusive[2] == 3);
  require(dense_index_of_node(dense, GrammarNodeId(4)) == 4);

  GlobalGrammarState with_dead_node = state;
  with_dead_node.nodes[1].alive = false;
  with_dead_node.nodes[0].local_next = GrammarNodeId::invalid();
  with_dead_node.chunks[0].last_node_id = GrammarNodeId(0);
  with_dead_node.chunks[0].live_count = 1;
  with_dead_node.live_node_count = 4;
  const GrammarSnapshot sparse = freeze_grammar_snapshot(with_dead_node);
  require(sparse.size() == 4);
  require(dense_index_of_node(sparse, GrammarNodeId(0)) == 0);
  require(dense_index_of_node(sparse, GrammarNodeId(2)) == 1);

  bool caught_dead_lookup = false;
  try {
    (void)dense_index_of_node(sparse, GrammarNodeId(1));
  } catch (const std::invalid_argument&) {
    caught_dead_lookup = true;
  }
  require(caught_dead_lookup);

  GlobalGrammarState corrupted = state;
  corrupted.chunks[0].live_count = 3;
  bool caught_bad_count = false;
  try {
    (void)freeze_grammar_snapshot(corrupted);
  } catch (const std::invalid_argument&) {
    caught_bad_count = true;
  }
  require(caught_bad_count);

  GlobalGrammarState bad_owner = state;
  bad_owner.nodes[1].owner_chunk_id = GrammarChunkId(1);
  bool caught_bad_owner = false;
  try {
    (void)freeze_grammar_snapshot(bad_owner);
  } catch (const std::invalid_argument&) {
    caught_bad_owner = true;
  }
  require(caught_bad_owner);

  return 0;
}
