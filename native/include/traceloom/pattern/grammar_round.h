#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"
#include "traceloom/pattern/grammar_snapshot.h"
#include "traceloom/pattern/grammar_state.h"

namespace traceloom {

enum class GrammarProducerId {
  kAdjacentRun,
};

enum class GrammarRoundStatus {
  kStop,
  kActionSelected,
  kError,
};

enum class GrammarActionKind {
  kReplaceExactRuns,
};

struct GrammarCandidateKey {
  GrammarProducerId producer_id = GrammarProducerId::kAdjacentRun;
  SymbolId symbol_id;
  std::size_t run_len = 0;
};

inline bool operator==(const GrammarCandidateKey& lhs,
                       const GrammarCandidateKey& rhs) noexcept {
  return lhs.producer_id == rhs.producer_id &&
         lhs.symbol_id == rhs.symbol_id && lhs.run_len == rhs.run_len;
}

inline bool operator<(const GrammarCandidateKey& lhs,
                      const GrammarCandidateKey& rhs) noexcept {
  if (lhs.producer_id != rhs.producer_id) {
    return lhs.producer_id < rhs.producer_id;
  }
  if (lhs.symbol_id != rhs.symbol_id) {
    return lhs.symbol_id < rhs.symbol_id;
  }
  return lhs.run_len < rhs.run_len;
}

struct GrammarCandidateOccurrence {
  GrammarCandidateKey key;
  GrammarNodeId begin_node_id;
  GrammarNodeId last_node_id;
  std::size_t begin_dense_index = 0;
  std::size_t end_dense_index_exclusive = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  GrammarChunkId owner_chunk_id;
  std::size_t owner_worker_id = 0;
  bool crosses_chunk_boundary = false;
};

struct GrammarLocalMapOutput {
  std::size_t worker_id = 0;
  std::vector<GrammarChunkId> chunk_ids;
  std::vector<GrammarCandidateOccurrence> occurrences;
};

struct GrammarCandidateStats {
  GrammarCandidateKey key;
  std::size_t occurrence_count = 0;
  std::size_t first_dense_index = 0;
  std::size_t gain = 0;
};

struct GrammarGlobalAction {
  GrammarActionKind kind = GrammarActionKind::kReplaceExactRuns;
  GrammarCandidateKey key;
  std::size_t replace_count = 0;
  std::size_t gain = 0;
  std::size_t first_dense_index = 0;
  std::vector<GrammarCandidateOccurrence> occurrences;
};

struct GrammarRoundResult {
  GrammarRoundStatus status = GrammarRoundStatus::kStop;
  std::uint64_t snapshot_generation = 0;
  GrammarProducerId producer_id = GrammarProducerId::kAdjacentRun;
  std::vector<GrammarLocalMapOutput> local_outputs;
  std::vector<GrammarCandidateOccurrence> occurrences;
  std::vector<GrammarCandidateStats> candidate_stats;
  GrammarGlobalAction action;
};

const char* grammar_producer_id_name(GrammarProducerId producer_id);

GrammarRoundResult run_adjacent_run_readonly_round(
    const GlobalGrammarState& state);

}  // namespace traceloom
