#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"
#include "traceloom/pattern/grammar_modes.h"
#include "traceloom/pattern/grammar_state.h"
#include "traceloom/sequence/boundary_index.h"

namespace traceloom {

struct GrammarSnapshotNode {
  GrammarNodeId node_id;
  SymbolId symbol_id;
  MacroDefId macro_def_id = MacroDefId::invalid();
  std::size_t source_begin_token_index = 0;
  std::size_t source_end_token_index_exclusive = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  GrammarChunkId owner_chunk_id;
  std::size_t dense_index = 0;
};

struct GrammarSnapshot {
  GrammarAlgorithmMetadata metadata;
  GrammarStage stage = GrammarStage::kInit;
  std::uint64_t generation = 0;
  std::vector<GrammarSnapshotNode> nodes;
  std::vector<GrammarChunk> chunks;
  std::vector<BoundarySummary> boundary_summaries;
  std::vector<MacroDefRow> macro_defs;
  std::vector<ProtectedIntervalSpan> protected_intervals;
  std::vector<std::size_t> node_dense_index;

  std::size_t size() const noexcept { return nodes.size(); }
  bool empty() const noexcept { return nodes.empty(); }
};

struct DenseGrammarView {
  std::uint64_t generation = 0;
  std::vector<GrammarNodeId> node_ids;
  std::vector<SymbolId> symbols;
  std::vector<MacroDefId> macro_def_ids;
  std::vector<std::int64_t> start_ns;
  std::vector<std::int64_t> end_ns;
  std::vector<std::size_t> source_begin_token_index;
  std::vector<std::size_t> source_end_token_index_exclusive;
  std::vector<std::size_t> node_dense_index;

  std::size_t size() const noexcept { return node_ids.size(); }
  bool empty() const noexcept { return node_ids.empty(); }
};

constexpr std::size_t kInvalidDenseIndex =
    static_cast<std::size_t>(-1);

GrammarSnapshot freeze_grammar_snapshot(const GlobalGrammarState& state);

DenseGrammarView build_dense_grammar_view(const GrammarSnapshot& snapshot);

std::size_t dense_index_of_node(const GrammarSnapshot& snapshot,
                                GrammarNodeId node_id);

std::size_t dense_index_of_node(const DenseGrammarView& view,
                                GrammarNodeId node_id);

}  // namespace traceloom
