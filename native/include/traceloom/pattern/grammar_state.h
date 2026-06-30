#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/pattern/grammar_modes.h"
#include "traceloom/sequence/boundary_index.h"

namespace traceloom {

enum class GrammarStage {
  kInit,
  kRunFold,
  kPairGrammar,
  kMacroRunFold,
  kDone,
  kError,
};

enum class MacroLevel {
  kRP,
  kLP,
};

struct GrammarStateConfig {
  GrammarAlgorithmMode mode = GrammarAlgorithmMode::kAnalysisQualityV1;
  std::size_t target_nodes_per_chunk = 4096;
  std::size_t worker_count = 1;
  std::size_t full_discovery_cap = 50000;
};

struct GrammarNode {
  GrammarNodeId id;
  SymbolId symbol_id;
  MacroDefId macro_def_id = MacroDefId::invalid();
  std::size_t source_begin_token_index = 0;
  std::size_t source_end_token_index_exclusive = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  GrammarChunkId owner_chunk_id;
  GrammarNodeId local_prev = GrammarNodeId::invalid();
  GrammarNodeId local_next = GrammarNodeId::invalid();
  bool alive = true;
};

struct MacroDefRow {
  MacroDefId id;
  MacroLevel level = MacroLevel::kRP;
  std::vector<SymbolId> rhs_symbols;
  std::size_t definition_len = 0;
  std::size_t replace_count = 0;
  std::ptrdiff_t gain = 0;
  std::size_t first_pos = 0;
};

struct GrammarChunk {
  GrammarChunkId id;
  std::size_t chunk_order_key = 0;
  std::size_t owner_worker_id = 0;
  GrammarNodeId first_node_id;
  GrammarNodeId last_node_id;
  std::size_t live_count = 0;
  std::uint64_t generation = 0;
};

struct BoundarySummary {
  GrammarChunkId chunk_id;
  std::uint64_t generation = 0;
  GrammarNodeId first_live_node_id;
  GrammarNodeId last_live_node_id;
  SymbolId first_live_symbol = SymbolId::invalid();
  SymbolId last_live_symbol = SymbolId::invalid();
  SymbolId prefix_run_symbol = SymbolId::invalid();
  std::size_t prefix_run_len = 0;
  SymbolId suffix_run_symbol = SymbolId::invalid();
  std::size_t suffix_run_len = 0;
  bool all_same_symbol = false;
  std::size_t live_count = 0;
  GrammarChunkId prev_chunk_id = GrammarChunkId::invalid();
  GrammarChunkId next_chunk_id = GrammarChunkId::invalid();
};

struct GlobalGrammarState {
  GrammarAlgorithmMetadata metadata;
  GrammarStage stage = GrammarStage::kInit;
  std::uint64_t generation = 0;
  std::vector<GrammarNode> nodes;
  std::vector<GrammarChunk> chunks;
  std::vector<BoundarySummary> boundary_summaries;
  std::vector<MacroDefRow> macro_defs;
  std::vector<ProtectedIntervalSpan> protected_intervals;
  std::size_t live_node_count = 0;
};

GlobalGrammarState build_initial_grammar_state(
    const NativeIr& ir,
    const GrammarStateConfig& config = GrammarStateConfig{});

}  // namespace traceloom
