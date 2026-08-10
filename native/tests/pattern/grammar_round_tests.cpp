#include "traceloom/adapters/fixture_adapter.h"
#include "traceloom/pattern/grammar_apply.h"
#include "traceloom/pattern/grammar_commit_plan.h"
#include "traceloom/pattern/grammar_round.h"
#include "traceloom/pattern/grammar_snapshot.h"
#include "traceloom/pattern/grammar_state.h"
#include "traceloom/testing/test_util.h"

#include <string>
#include <vector>

namespace {

traceloom::NativeIr make_ir(const std::vector<std::string>& symbols) {
  using namespace traceloom;

  FixtureInput input;
  std::int64_t ts = 0;
  for (const std::string& symbol : symbols) {
    input.tokens.push_back(
        FixtureToken{symbol, AnchorKind::kDeviceEvent, 0, 0, ts, ts + 10});
    ts += 10;
  }
  return FixtureAdapter(input).load();
}

traceloom::GrammarRoundResult round_for(
    const std::vector<std::string>& symbols,
    std::size_t target_nodes_per_chunk,
    std::size_t worker_count) {
  traceloom::GrammarStateConfig config;
  config.target_nodes_per_chunk = target_nodes_per_chunk;
  config.worker_count = worker_count;
  return traceloom::run_adjacent_run_readonly_round(
      traceloom::build_initial_grammar_state(make_ir(symbols), config));
}

traceloom::GrammarRoundResult pair_round_for(
    const std::vector<std::string>& symbols,
    std::size_t target_nodes_per_chunk,
    std::size_t worker_count) {
  traceloom::GrammarStateConfig config;
  config.target_nodes_per_chunk = target_nodes_per_chunk;
  config.worker_count = worker_count;
  return traceloom::run_pair_grammar_readonly_round(
      traceloom::build_initial_grammar_state(make_ir(symbols), config));
}

traceloom::GrammarRoundResult block_round_for(
    const std::vector<std::string>& symbols,
    std::size_t target_nodes_per_chunk,
    std::size_t worker_count) {
  traceloom::GrammarStateConfig config;
  config.target_nodes_per_chunk = target_nodes_per_chunk;
  config.worker_count = worker_count;
  return traceloom::run_exact_repeated_block_readonly_round(
      traceloom::build_initial_grammar_state(make_ir(symbols), config));
}

traceloom::GlobalGrammarState pair_compressed_state(
    const std::vector<std::string>& symbols,
    std::size_t target_nodes_per_chunk = 2,
    std::size_t worker_count = 2) {
  traceloom::GrammarStateConfig config;
  config.target_nodes_per_chunk = target_nodes_per_chunk;
  config.worker_count = worker_count;
  traceloom::GlobalGrammarState state =
      traceloom::build_initial_grammar_state(make_ir(symbols), config);
  const traceloom::GrammarSnapshot snapshot =
      traceloom::freeze_grammar_snapshot(state);
  const traceloom::GrammarRoundResult round =
      traceloom::run_pair_grammar_readonly_round(state);
  const traceloom::GrammarCommitPlan plan =
      traceloom::build_pair_grammar_commit_plan(snapshot, round.action);
  const traceloom::GrammarApplyResult applied =
      traceloom::apply_pair_grammar_commit_plan(state, plan);
  traceloom::testing::require(applied.applied());
  return state;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  const GrammarRoundResult cross_chunk =
      round_for({"A", "A", "A", "B", "C", "C"}, 2, 3);
  require(cross_chunk.status == GrammarRoundStatus::kActionSelected);
  require(cross_chunk.snapshot_generation == 0);
  require(cross_chunk.local_outputs.size() == 3);
  require(cross_chunk.occurrences.size() == 2);
  require(cross_chunk.candidate_stats.size() == 2);
  require(cross_chunk.action.key.run_len == 3);
  require(cross_chunk.action.replace_count == 1);
  require(cross_chunk.action.gain == 2);
  require(cross_chunk.action.first_dense_index == 0);
  require(cross_chunk.action.occurrences.size() == 1);
  require(cross_chunk.action.occurrences[0].begin_node_id ==
          GrammarNodeId(0));
  require(cross_chunk.action.occurrences[0].last_node_id == GrammarNodeId(2));
  require(cross_chunk.action.occurrences[0].crosses_chunk_boundary);
  require(cross_chunk.local_outputs[0].occurrences.size() == 1);
  require(cross_chunk.local_outputs[2].occurrences.size() == 1);

  const GrammarRoundResult one_worker =
      round_for({"A", "A", "A", "B", "C", "C"}, 2, 1);
  require(one_worker.status == GrammarRoundStatus::kActionSelected);
  require(one_worker.action.key.symbol_id == cross_chunk.action.key.symbol_id);
  require(one_worker.action.key.run_len == cross_chunk.action.key.run_len);
  require(one_worker.action.replace_count == cross_chunk.action.replace_count);
  require(one_worker.action.gain == cross_chunk.action.gain);
  require(one_worker.action.first_dense_index ==
          cross_chunk.action.first_dense_index);

  const GrammarRoundResult tie_break =
      round_for({"A", "A", "B", "B", "C", "C"}, 2, 2);
  require(tie_break.status == GrammarRoundStatus::kActionSelected);
  require(tie_break.action.key.run_len == 2);
  require(tie_break.action.replace_count == 1);
  require(tie_break.action.first_dense_index == 0);

  const GrammarRoundResult stop = round_for({"A", "B", "C"}, 2, 4);
  require(stop.status == GrammarRoundStatus::kStop);
  require(stop.occurrences.empty());
  require(stop.candidate_stats.empty());

  const GrammarRoundResult pair =
      pair_round_for({"A", "B", "A", "B", "A", "B", "A", "B"}, 3, 2);
  require(pair.status == GrammarRoundStatus::kActionSelected);
  require(pair.producer_id == GrammarProducerId::kPairGrammar);
  require(pair.action.kind == GrammarActionKind::kReplacePair);
  require(pair.action.key.run_len == 2);
  require(pair.action.replace_count == 4);
  require(pair.action.gain == 1);
  require(pair.action.first_dense_index == 0);
  require(pair.action.occurrences.size() == 4);
  require(pair.action.occurrences[0].begin_dense_index == 0);
  require(pair.action.occurrences[3].begin_dense_index == 6);

  const GrammarRoundResult boundary_pair =
      pair_round_for({"A", "B", "A", "B", "A", "B", "A", "B"}, 1, 3);
  require(boundary_pair.status == GrammarRoundStatus::kActionSelected);
  require(boundary_pair.action.key == pair.action.key);
  require(boundary_pair.action.occurrences.size() == 4);
  for (std::size_t index = 0; index < boundary_pair.action.occurrences.size();
       ++index) {
    const GrammarCandidateOccurrence& occurrence =
        boundary_pair.action.occurrences[index];
    require(occurrence.crosses_chunk_boundary);
    require(occurrence.owner_chunk_id ==
            GrammarChunkId(static_cast<GrammarChunkId::value_type>(
                occurrence.begin_dense_index)));
    require(occurrence.owner_worker_id ==
            occurrence.owner_chunk_id.value() % 3);
  }

  const GrammarRoundResult pair_one_worker =
      pair_round_for({"A", "B", "A", "B", "A", "B", "A", "B"}, 3, 1);
  require(pair_one_worker.status == GrammarRoundStatus::kActionSelected);
  require(pair_one_worker.action.key == pair.action.key);
  require(pair_one_worker.action.replace_count == pair.action.replace_count);
  require(pair_one_worker.action.gain == pair.action.gain);
  require(pair_one_worker.action.first_dense_index ==
          pair.action.first_dense_index);

  const GrammarRoundResult pair_stop =
      pair_round_for({"A", "B", "A", "B", "A", "B"}, 3, 2);
  require(pair_stop.status == GrammarRoundStatus::kStop);

  const GrammarRoundResult pair_tie =
      pair_round_for({"A", "B", "C", "D", "A", "B", "C", "D",
                      "A", "B", "C", "D", "A", "B", "C", "D"},
                     3, 2);
  require(pair_tie.status == GrammarRoundStatus::kActionSelected);
  require(pair_tie.action.first_dense_index == 0);
  require(pair_tie.action.key.symbol_id == SymbolId(0));
  require(pair_tie.action.key.second_symbol_id == SymbolId(1));

  GlobalGrammarState macro_state =
      pair_compressed_state({"A", "B", "A", "B", "C", "A",
                             "B", "A", "B", "A", "B"});
  const GrammarRoundResult macro_run =
      run_native_macro_run_readonly_round(macro_state);
  require(macro_run.status == GrammarRoundStatus::kActionSelected);
  require(macro_run.producer_id == GrammarProducerId::kNativeMacroRun);
  require(macro_run.action.kind == GrammarActionKind::kCompressMaximalRuns);
  require(macro_run.action.key.producer_id == GrammarProducerId::kNativeMacroRun);
  require(macro_run.action.key.run_len == 0);
  require(macro_run.action.replace_count == 2);
  require(macro_run.action.gain == 3);
  require(macro_run.action.max_run_len == 3);
  require(macro_run.action.first_dense_index == 0);
  require(macro_run.action.occurrences.size() == 2);
  require(macro_run.action.occurrences[0].begin_dense_index == 0);
  require(macro_run.action.occurrences[0].end_dense_index_exclusive == 2);
  require(macro_run.action.occurrences[1].begin_dense_index == 3);
  require(macro_run.action.occurrences[1].end_dense_index_exclusive == 6);

  const GrammarRoundResult macro_one_worker =
      run_native_macro_run_readonly_round(pair_compressed_state(
          {"A", "B", "A", "B", "C", "A", "B", "A", "B", "A", "B"}));
  require(macro_one_worker.status == GrammarRoundStatus::kActionSelected);
  require(macro_one_worker.action.key == macro_run.action.key);
  require(macro_one_worker.action.gain == macro_run.action.gain);
  require(macro_one_worker.action.max_run_len == macro_run.action.max_run_len);

  GlobalGrammarState boundary_macro_state =
      pair_compressed_state({"A", "B", "A", "B", "A",
                             "B", "A", "B", "C"},
                            1, 3);
  const GrammarRoundResult boundary_macro =
      run_native_macro_run_readonly_round(boundary_macro_state);
  require(boundary_macro.status == GrammarRoundStatus::kActionSelected);
  require(boundary_macro.action.occurrences.size() == 1);
  require(boundary_macro.action.occurrences[0].begin_dense_index == 0);
  require(boundary_macro.action.occurrences[0].end_dense_index_exclusive == 4);
  require(boundary_macro.action.occurrences[0].crosses_chunk_boundary);
  require(boundary_macro.action.max_run_len == 4);
  require(boundary_macro.action.gain == 3);

  const GrammarRoundResult repeated_block =
      block_round_for({"A", "B", "C", "D", "A", "B", "C", "D",
                       "A", "B", "C", "D"},
                      2, 2);
  require(repeated_block.status == GrammarRoundStatus::kActionSelected);
  require(repeated_block.producer_id ==
          GrammarProducerId::kExactRepeatedBlock);
  require(repeated_block.action.kind ==
          GrammarActionKind::kReplaceRepeatedBlock);
  require(repeated_block.action.key.producer_id ==
          GrammarProducerId::kExactRepeatedBlock);
  require(repeated_block.action.key.symbol_id == SymbolId(0));
  require(repeated_block.action.key.run_len == 4);
  require(repeated_block.action.block_rhs_symbols.size() == 4);
  require(repeated_block.action.block_rhs_symbols[0] == SymbolId(0));
  require(repeated_block.action.block_rhs_symbols[3] == SymbolId(3));
  require(repeated_block.action.repeat_count == 3);
  require(repeated_block.action.replace_count == 3);
  require(repeated_block.action.gain == 11);
  require(repeated_block.action.first_dense_index == 0);
  require(repeated_block.action.max_run_len == 4);
  require(repeated_block.action.occurrences.size() == 3);
  require(repeated_block.action.occurrences[0].begin_dense_index == 0);
  require(repeated_block.action.occurrences[0].end_dense_index_exclusive == 4);
  require(repeated_block.action.occurrences[1].begin_dense_index == 4);
  require(repeated_block.action.occurrences[2].begin_dense_index == 8);
  require(repeated_block.action.occurrences[2].end_dense_index_exclusive == 12);
  require(repeated_block.candidate_stats.size() == 1);
  require(repeated_block.candidate_stats[0].occurrence_count == 3);
  require(repeated_block.candidate_stats[0].gain == 11);
  require(repeated_block.candidate_stats[0].max_run_len == 4);
  require(repeated_block.local_outputs.size() == 2);

  const GrammarRoundResult repeated_block_one_worker =
      block_round_for({"A", "B", "C", "D", "A", "B", "C", "D",
                       "A", "B", "C", "D"},
                      2, 1);
  require(repeated_block_one_worker.status ==
          GrammarRoundStatus::kActionSelected);
  require(repeated_block_one_worker.action.key ==
          repeated_block.action.key);
  require(repeated_block_one_worker.action.block_rhs_symbols ==
          repeated_block.action.block_rhs_symbols);
  require(repeated_block_one_worker.action.repeat_count ==
          repeated_block.action.repeat_count);
  require(repeated_block_one_worker.action.gain ==
          repeated_block.action.gain);
  require(repeated_block_one_worker.local_outputs.size() == 1);

  const GrammarRoundResult repeated_block_boundary =
      block_round_for({"A", "B", "C", "D", "A", "B", "C", "D",
                       "A", "B", "C", "D"},
                      1, 3);
  require(repeated_block_boundary.status ==
          GrammarRoundStatus::kActionSelected);
  require(repeated_block_boundary.action.occurrences.size() == 3);
  for (const GrammarCandidateOccurrence& occurrence :
       repeated_block_boundary.action.occurrences) {
    require(occurrence.crosses_chunk_boundary);
  }

  const GrammarRoundResult repeated_block_primitive =
      block_round_for({"A", "B", "A", "B", "A", "B", "A", "B"}, 2, 2);
  require(repeated_block_primitive.status ==
          GrammarRoundStatus::kActionSelected);
  require(repeated_block_primitive.action.key.run_len == 2);
  require(repeated_block_primitive.action.repeat_count == 4);
  require(repeated_block_primitive.action.block_rhs_symbols.size() == 2);

  const GrammarRoundResult repeated_block_two =
      block_round_for({"A", "B", "A", "B"}, 2, 2);
  require(repeated_block_two.status == GrammarRoundStatus::kActionSelected);
  require(repeated_block_two.action.key.run_len == 2);
  require(repeated_block_two.action.repeat_count == 2);

  const GrammarRoundResult repeated_block_near_miss =
      block_round_for({"A", "B", "C", "D", "A", "B", "C", "D",
                       "A", "B", "C"},
                      2, 2);
  require(repeated_block_near_miss.status == GrammarRoundStatus::kStop);
  require(repeated_block_near_miss.occurrences.empty());
  require(repeated_block_near_miss.candidate_stats.empty());

  const GrammarRoundResult repeated_block_short =
      block_round_for({"A", "B", "C"}, 2, 2);
  require(repeated_block_short.status == GrammarRoundStatus::kStop);

  const GrammarRoundResult repeated_block_uniform =
      block_round_for({"A", "A", "A", "A", "A", "A"}, 2, 2);
  require(repeated_block_uniform.status == GrammarRoundStatus::kStop);
  require(repeated_block_uniform.occurrences.empty());

  traceloom::GrammarStateConfig capped_config;
  capped_config.target_nodes_per_chunk = 2;
  capped_config.worker_count = 2;
  capped_config.full_discovery_cap = 8;
  const traceloom::GrammarRoundResult repeated_block_capped =
      traceloom::run_exact_repeated_block_readonly_round(
          traceloom::build_initial_grammar_state(
              make_ir({"A", "B", "C", "D", "A", "B", "C", "D",
                       "A", "B", "C", "D"}),
              capped_config));
  require(repeated_block_capped.status == GrammarRoundStatus::kStop);
  require(repeated_block_capped.occurrences.empty());

  GlobalGrammarState state =
      build_initial_grammar_state(make_ir({"A", "A", "A"}));
  const std::size_t node_count = state.nodes.size();
  const std::size_t chunk_count = state.chunks.size();
  const std::size_t live_count = state.live_node_count;
  const std::uint64_t generation = state.generation;
  const GrammarRoundResult readonly =
      run_adjacent_run_readonly_round(state);
  require(readonly.status == GrammarRoundStatus::kActionSelected);
  require(state.nodes.size() == node_count);
  require(state.chunks.size() == chunk_count);
  require(state.live_node_count == live_count);
  require(state.generation == generation);
  require(state.macro_defs.empty());

  return 0;
}
