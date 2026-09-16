#include "traceloom/pattern/grammar_snapshot.h"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <string>

namespace traceloom {
namespace {

SuffixMarkerSummary concatenate_suffix(SuffixMarkerSummary left, SuffixMarkerSummary right) {
  return {left.has_marker || right.has_marker, left.has_other || right.has_other,
          left.valid && right.valid && !(left.has_marker && right.has_other)};
}
SuffixMarkerSummary suffix_of(const std::map<SymbolId, SuffixMarkerSummary>& summaries, SymbolId id) {
  auto it = summaries.find(id);
  return it == summaries.end() ? SuffixMarkerSummary{false, true, true} : it->second;
}

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
  snapshot.token_partition_runs = state.token_partition_runs;
  snapshot.marker_balances = state.marker_seeds;
  for (auto& balances : snapshot.marker_balances) {
    for (const auto& macro : state.macro_defs) {
      std::int64_t net = 0;
      // Existing exact replay units retain their externally established protection.
      if (macro.level != MacroLevel::kSemantic)
        for (auto child : macro.rhs_symbols) {
          auto i = balances.find(child);
          if (i != balances.end()) net += i->second;
        }
      balances[macro.symbol_id] = net;
    }
  }
  for (const auto& seeds : state.suffix_marker_seeds) {
    std::map<SymbolId, SuffixMarkerSummary> summaries;
    for (const auto& [symbol, marker] : seeds) summaries[symbol] = {marker, !marker, true};
    for (const auto& macro : state.macro_defs) {
      SuffixMarkerSummary summary;
      // Exact replay units remain opaque; their separate body grammar is unchanged.
      if (macro.level == MacroLevel::kSemantic) summary.has_other = true;
      else for (auto child : macro.rhs_symbols)
        summary = concatenate_suffix(summary, suffix_of(summaries, child));
      summaries[macro.symbol_id] = summary;
    }
    snapshot.suffix_markers.push_back(std::move(summaries));
  }
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

bool macro_partition_allowed(const GrammarSnapshot& snapshot, std::size_t begin, std::size_t end) {
  if (snapshot.token_partition_runs.empty()) return true;
  if (begin >= end || end > snapshot.nodes.size()) return false;
  const auto first = snapshot.nodes[begin].source_begin_token_index;
  const auto last = snapshot.nodes[end-1].source_end_token_index_exclusive;
  return first < last && last <= snapshot.token_partition_runs.size() &&
      snapshot.token_partition_runs[first] == snapshot.token_partition_runs[last-1];
}

bool macro_match_allowed(const GrammarSnapshot& snapshot, std::size_t begin, std::size_t end) {
  if (!macro_partition_allowed(snapshot, begin, end)) return false;
  for (const auto& summaries : snapshot.suffix_markers) {
    SuffixMarkerSummary summary;
    for (auto i = begin; i < end; ++i) {
      summary = concatenate_suffix(summary, suffix_of(summaries, snapshot.nodes.at(i).symbol_id));
      if (!summary.valid) return false;
    }
  }
  for (const auto& balances : snapshot.marker_balances) {
    std::int64_t net = 0;
    for (auto i = begin; i < end; ++i) {
      auto entry = balances.find(snapshot.nodes.at(i).symbol_id);
      const auto next = entry == balances.end() ? 0 : entry->second;
      if (net < 0 && next > 0) return false;
      net += next;
    }
  }
  return true;
}

}  // namespace traceloom
