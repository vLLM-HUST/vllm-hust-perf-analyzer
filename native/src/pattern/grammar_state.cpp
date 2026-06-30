#include "traceloom/pattern/grammar_state.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "traceloom/sequence/protected_sequence.h"

namespace traceloom {
namespace {

BoundarySummary summarize_chunk(const std::vector<GrammarNode>& nodes,
                                const GrammarChunk& chunk,
                                std::uint64_t generation) {
  BoundarySummary summary;
  summary.chunk_id = chunk.id;
  summary.generation = generation;
  summary.first_live_node_id = chunk.first_node_id;
  summary.last_live_node_id = chunk.last_node_id;
  summary.live_count = chunk.live_count;
  if (chunk.live_count == 0) {
    return summary;
  }

  const std::size_t first = chunk.first_node_id.value();
  const std::size_t last = chunk.last_node_id.value();
  summary.first_live_symbol = nodes[first].symbol_id;
  summary.last_live_symbol = nodes[last].symbol_id;

  summary.prefix_run_symbol = summary.first_live_symbol;
  summary.prefix_run_len = 0;
  for (std::size_t index = first; index <= last; ++index) {
    if (nodes[index].symbol_id != summary.prefix_run_symbol) {
      break;
    }
    ++summary.prefix_run_len;
  }

  summary.suffix_run_symbol = summary.last_live_symbol;
  summary.suffix_run_len = 0;
  for (std::size_t index = last + 1; index > first; --index) {
    const std::size_t node_index = index - 1;
    if (nodes[node_index].symbol_id != summary.suffix_run_symbol) {
      break;
    }
    ++summary.suffix_run_len;
  }
  summary.all_same_symbol = summary.prefix_run_len == chunk.live_count;
  return summary;
}

}  // namespace

GlobalGrammarState build_initial_grammar_state(
    const NativeIr& ir,
    const GrammarStateConfig& config) {
  if (config.target_nodes_per_chunk == 0) {
    throw std::invalid_argument("target_nodes_per_chunk must be nonzero");
  }
  if (config.worker_count == 0) {
    throw std::invalid_argument("worker_count must be nonzero");
  }
  if (ir.tokens.empty()) {
    throw std::invalid_argument("grammar state requires non-empty TokenTable");
  }

  const ProtectedSequence sequence =
      ProtectedSequence::from_token_table(ir.tokens);
  const BoundaryIndex boundary_index =
      BoundaryIndex::build(sequence, ir.protected_intervals);

  GlobalGrammarState state;
  state.metadata = default_grammar_metadata(config.mode);
  state.metadata.full_discovery_cap = config.full_discovery_cap;
  state.stage = GrammarStage::kRunFold;
  state.generation = 0;
  state.live_node_count = sequence.size();
  state.protected_intervals = boundary_index.intervals();
  state.target_nodes_per_chunk = config.target_nodes_per_chunk;
  state.worker_count = config.worker_count;

  SymbolId::value_type max_symbol_value = 0;
  state.nodes.reserve(sequence.size());
  for (std::size_t index = 0; index < sequence.size(); ++index) {
    const ProtectedSequenceToken& token = sequence.token_at(index);
    if (token.symbol_id.valid()) {
      max_symbol_value = std::max(max_symbol_value, token.symbol_id.value());
    }
    const auto node_id = checked_next_id<GrammarNodeId>(state.nodes.size());
    state.nodes.push_back(GrammarNode{
        node_id,
        token.symbol_id,
        MacroDefId::invalid(),
        index,
        index + 1,
        token.start_ns,
        token.end_ns,
        GrammarChunkId::invalid(),
        GrammarNodeId::invalid(),
        GrammarNodeId::invalid(),
        true});
  }
  if (max_symbol_value == std::numeric_limits<SymbolId::value_type>::max()) {
    throw std::overflow_error("symbol id space exhausted");
  }
  state.next_macro_symbol_id = SymbolId(max_symbol_value + 1);

  for (std::size_t begin = 0; begin < sequence.size();
       begin += config.target_nodes_per_chunk) {
    const std::size_t end =
        std::min(sequence.size(), begin + config.target_nodes_per_chunk);
    const auto chunk_id = checked_next_id<GrammarChunkId>(state.chunks.size());
    const std::size_t owner_worker_id =
        state.chunks.size() % config.worker_count;
    state.chunks.push_back(GrammarChunk{
        chunk_id,
        state.chunks.size(),
        owner_worker_id,
        GrammarNodeId(static_cast<GrammarNodeId::value_type>(begin)),
        GrammarNodeId(static_cast<GrammarNodeId::value_type>(end - 1)),
        end - begin,
        state.generation});
    for (std::size_t index = begin; index < end; ++index) {
      state.nodes[index].owner_chunk_id = chunk_id;
      state.nodes[index].local_prev =
          index == begin ? GrammarNodeId::invalid()
                         : GrammarNodeId(static_cast<GrammarNodeId::value_type>(
                               index - 1));
      state.nodes[index].local_next =
          index + 1 == end
              ? GrammarNodeId::invalid()
              : GrammarNodeId(
                    static_cast<GrammarNodeId::value_type>(index + 1));
    }
  }

  state.boundary_summaries.reserve(state.chunks.size());
  for (const GrammarChunk& chunk : state.chunks) {
    state.boundary_summaries.push_back(
        summarize_chunk(state.nodes, chunk, state.generation));
  }
  for (std::size_t index = 0; index < state.boundary_summaries.size();
       ++index) {
    if (index > 0) {
      state.boundary_summaries[index].prev_chunk_id =
          state.boundary_summaries[index - 1].chunk_id;
    }
    if (index + 1 < state.boundary_summaries.size()) {
      state.boundary_summaries[index].next_chunk_id =
          state.boundary_summaries[index + 1].chunk_id;
    }
  }
  return state;
}

}  // namespace traceloom
