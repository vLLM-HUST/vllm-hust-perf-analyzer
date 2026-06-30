#include "traceloom/pattern/grammar_snapshot.h"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <string>

namespace traceloom {
namespace {

std::size_t require_node_index(GrammarNodeId node_id, std::size_t size) {
  if (!node_id.valid() || node_id.value() >= size) {
    throw std::invalid_argument("grammar snapshot references invalid node id");
  }
  return node_id.value();
}

void append_snapshot_node(GrammarSnapshot& snapshot,
                          const GrammarNode& node) {
  const std::size_t dense_index = snapshot.nodes.size();
  snapshot.nodes.push_back(GrammarSnapshotNode{
      node.id,
      node.symbol_id,
      node.macro_def_id,
      node.source_begin_token_index,
      node.source_end_token_index_exclusive,
      node.start_ns,
      node.end_ns,
      node.owner_chunk_id,
      dense_index});
  snapshot.node_dense_index[node.id.value()] = dense_index;
}

}  // namespace

GrammarSnapshot freeze_grammar_snapshot(const GlobalGrammarState& state) {
  GrammarSnapshot snapshot;
  snapshot.metadata = state.metadata;
  snapshot.stage = state.stage;
  snapshot.generation = state.generation;
  snapshot.chunks = state.chunks;
  snapshot.boundary_summaries = state.boundary_summaries;
  snapshot.macro_defs = state.macro_defs;
  snapshot.protected_intervals = state.protected_intervals;
  snapshot.node_dense_index.assign(state.nodes.size(), kInvalidDenseIndex);
  snapshot.nodes.reserve(state.live_node_count);

  std::vector<std::size_t> chunk_order(state.chunks.size());
  std::iota(chunk_order.begin(), chunk_order.end(), 0);
  std::sort(chunk_order.begin(), chunk_order.end(),
            [&state](std::size_t lhs, std::size_t rhs) {
              const GrammarChunk& left = state.chunks[lhs];
              const GrammarChunk& right = state.chunks[rhs];
              if (left.chunk_order_key != right.chunk_order_key) {
                return left.chunk_order_key < right.chunk_order_key;
              }
              return left.id < right.id;
            });

  for (std::size_t chunk_index : chunk_order) {
    const GrammarChunk& chunk = state.chunks[chunk_index];
    if (chunk.live_count == 0) {
      continue;
    }

    GrammarNodeId current = chunk.first_node_id;
    GrammarNodeId last_seen = GrammarNodeId::invalid();
    std::size_t seen_in_chunk = 0;
    while (current.valid()) {
      const std::size_t node_index =
          require_node_index(current, state.nodes.size());
      const GrammarNode& node = state.nodes[node_index];
      if (!node.alive) {
        throw std::invalid_argument(
            "grammar snapshot encountered dead live-list node");
      }
      if (node.owner_chunk_id != chunk.id) {
        throw std::invalid_argument(
            "grammar snapshot encountered node owned by another chunk");
      }
      if (snapshot.node_dense_index[node.id.value()] != kInvalidDenseIndex) {
        throw std::invalid_argument(
            "grammar snapshot encountered duplicate live node");
      }

      append_snapshot_node(snapshot, node);
      last_seen = current;
      ++seen_in_chunk;
      if (seen_in_chunk > chunk.live_count) {
        throw std::invalid_argument(
            "grammar snapshot chunk live list exceeds live_count");
      }
      current = node.local_next;
    }

    if (seen_in_chunk != chunk.live_count) {
      throw std::invalid_argument(
          "grammar snapshot chunk live_count does not match live list");
    }
    if (last_seen != chunk.last_node_id) {
      throw std::invalid_argument(
          "grammar snapshot chunk last node does not match live list");
    }
  }

  if (snapshot.nodes.size() != state.live_node_count) {
    throw std::invalid_argument(
        "grammar snapshot global live_count does not match chunks");
  }
  return snapshot;
}

DenseGrammarView build_dense_grammar_view(const GrammarSnapshot& snapshot) {
  DenseGrammarView view;
  view.generation = snapshot.generation;
  view.node_dense_index = snapshot.node_dense_index;
  view.node_ids.reserve(snapshot.nodes.size());
  view.symbols.reserve(snapshot.nodes.size());
  view.macro_def_ids.reserve(snapshot.nodes.size());
  view.start_ns.reserve(snapshot.nodes.size());
  view.end_ns.reserve(snapshot.nodes.size());
  view.source_begin_token_index.reserve(snapshot.nodes.size());
  view.source_end_token_index_exclusive.reserve(snapshot.nodes.size());
  for (const GrammarSnapshotNode& node : snapshot.nodes) {
    view.node_ids.push_back(node.node_id);
    view.symbols.push_back(node.symbol_id);
    view.macro_def_ids.push_back(node.macro_def_id);
    view.start_ns.push_back(node.start_ns);
    view.end_ns.push_back(node.end_ns);
    view.source_begin_token_index.push_back(node.source_begin_token_index);
    view.source_end_token_index_exclusive.push_back(
        node.source_end_token_index_exclusive);
  }
  return view;
}

std::size_t dense_index_of_node(const GrammarSnapshot& snapshot,
                                GrammarNodeId node_id) {
  const std::size_t node_index =
      require_node_index(node_id, snapshot.node_dense_index.size());
  const std::size_t dense_index = snapshot.node_dense_index[node_index];
  if (dense_index == kInvalidDenseIndex) {
    throw std::invalid_argument("grammar node is not live in snapshot");
  }
  return dense_index;
}

std::size_t dense_index_of_node(const DenseGrammarView& view,
                                GrammarNodeId node_id) {
  const std::size_t node_index =
      require_node_index(node_id, view.node_dense_index.size());
  const std::size_t dense_index = view.node_dense_index[node_index];
  if (dense_index == kInvalidDenseIndex) {
    throw std::invalid_argument("grammar node is not live in dense view");
  }
  return dense_index;
}

}  // namespace traceloom
