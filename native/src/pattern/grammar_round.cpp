#include "traceloom/pattern/grammar_round.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <vector>

namespace traceloom {
namespace {

std::vector<std::size_t> build_worker_by_chunk(
    const GrammarSnapshot& snapshot) {
  std::vector<std::size_t> worker_by_chunk(snapshot.chunks.size(), 0);
  for (const GrammarChunk& chunk : snapshot.chunks) {
    if (!chunk.id.valid() || chunk.id.value() >= worker_by_chunk.size()) {
      throw std::invalid_argument("grammar round references invalid chunk id");
    }
    worker_by_chunk[chunk.id.value()] = chunk.owner_worker_id;
  }
  return worker_by_chunk;
}

std::size_t worker_count_from_chunks(const GrammarSnapshot& snapshot) {
  std::size_t worker_count = 0;
  for (const GrammarChunk& chunk : snapshot.chunks) {
    worker_count = std::max(worker_count, chunk.owner_worker_id + 1);
  }
  return worker_count;
}

bool occurrence_less(const GrammarCandidateOccurrence& lhs,
                     const GrammarCandidateOccurrence& rhs) {
  if (lhs.key < rhs.key) {
    return true;
  }
  if (rhs.key < lhs.key) {
    return false;
  }
  if (lhs.begin_dense_index != rhs.begin_dense_index) {
    return lhs.begin_dense_index < rhs.begin_dense_index;
  }
  if (lhs.end_dense_index_exclusive != rhs.end_dense_index_exclusive) {
    return lhs.end_dense_index_exclusive < rhs.end_dense_index_exclusive;
  }
  return lhs.owner_worker_id < rhs.owner_worker_id;
}

bool same_occurrence_identity(const GrammarCandidateOccurrence& lhs,
                              const GrammarCandidateOccurrence& rhs) {
  return lhs.key == rhs.key &&
         lhs.begin_dense_index == rhs.begin_dense_index &&
         lhs.end_dense_index_exclusive == rhs.end_dense_index_exclusive;
}

bool stats_better(const GrammarCandidateStats& lhs,
                  const GrammarCandidateStats& rhs) {
  if (lhs.gain != rhs.gain) {
    return lhs.gain > rhs.gain;
  }
  if (lhs.key.run_len != rhs.key.run_len) {
    return lhs.key.run_len > rhs.key.run_len;
  }
  if (lhs.occurrence_count != rhs.occurrence_count) {
    return lhs.occurrence_count > rhs.occurrence_count;
  }
  if (lhs.first_dense_index != rhs.first_dense_index) {
    return lhs.first_dense_index < rhs.first_dense_index;
  }
  return lhs.key.symbol_id < rhs.key.symbol_id;
}

bool pair_stats_better(const GrammarCandidateStats& lhs,
                       const GrammarCandidateStats& rhs) {
  if (lhs.occurrence_count != rhs.occurrence_count) {
    return lhs.occurrence_count > rhs.occurrence_count;
  }
  if (lhs.first_dense_index != rhs.first_dense_index) {
    return lhs.first_dense_index < rhs.first_dense_index;
  }
  return lhs.key < rhs.key;
}

bool macro_run_stats_better(const GrammarCandidateStats& lhs,
                            const GrammarCandidateStats& rhs) {
  if (lhs.gain != rhs.gain) {
    return lhs.gain > rhs.gain;
  }
  if (lhs.max_run_len != rhs.max_run_len) {
    return lhs.max_run_len > rhs.max_run_len;
  }
  if (lhs.occurrence_count != rhs.occurrence_count) {
    return lhs.occurrence_count > rhs.occurrence_count;
  }
  if (lhs.first_dense_index != rhs.first_dense_index) {
    return lhs.first_dense_index < rhs.first_dense_index;
  }
  return lhs.key.symbol_id < rhs.key.symbol_id;
}

std::set<SymbolId> eligible_macro_symbols(const GrammarSnapshot& snapshot) {
  std::set<SymbolId> symbols;
  for (const MacroDefRow& macro : snapshot.macro_defs) {
    if (macro.level != MacroLevel::kLP) {
      symbols.insert(macro.symbol_id);
    }
  }
  return symbols;
}

std::vector<GrammarCandidateStats> reduce_adjacent_run_candidates(
    std::vector<GrammarCandidateOccurrence> occurrences) {
  std::sort(occurrences.begin(), occurrences.end(), occurrence_less);
  occurrences.erase(std::unique(occurrences.begin(), occurrences.end(),
                                same_occurrence_identity),
                    occurrences.end());

  std::vector<GrammarCandidateStats> stats;
  for (const GrammarCandidateOccurrence& occurrence : occurrences) {
    if (stats.empty() || !(stats.back().key == occurrence.key)) {
      stats.push_back(GrammarCandidateStats{
          occurrence.key,
          1,
          occurrence.begin_dense_index,
          occurrence.key.run_len > 0 ? occurrence.key.run_len - 1 : 0,
          occurrence.key.run_len});
      continue;
    }
    GrammarCandidateStats& row = stats.back();
    row.occurrence_count += 1;
    row.first_dense_index =
        std::min(row.first_dense_index, occurrence.begin_dense_index);
    row.gain = row.occurrence_count * (row.key.run_len - 1);
  }
  return stats;
}

std::vector<GrammarCandidateStats> reduce_pair_grammar_candidates(
    std::vector<GrammarCandidateOccurrence> occurrences) {
  std::sort(occurrences.begin(), occurrences.end(), occurrence_less);
  occurrences.erase(std::unique(occurrences.begin(), occurrences.end(),
                                same_occurrence_identity),
                    occurrences.end());

  std::vector<GrammarCandidateStats> stats;
  for (const GrammarCandidateOccurrence& occurrence : occurrences) {
    if (stats.empty() || !(stats.back().key == occurrence.key)) {
      stats.push_back(GrammarCandidateStats{
          occurrence.key,
          1,
          occurrence.begin_dense_index,
          0,
          occurrence.key.run_len});
      continue;
    }
    GrammarCandidateStats& row = stats.back();
    row.occurrence_count += 1;
    row.first_dense_index =
        std::min(row.first_dense_index, occurrence.begin_dense_index);
  }
  for (GrammarCandidateStats& row : stats) {
    row.gain = row.occurrence_count > 3 ? row.occurrence_count - 3 : 0;
  }
  return stats;
}

std::vector<GrammarCandidateStats> reduce_macro_run_candidates(
    std::vector<GrammarCandidateOccurrence> occurrences) {
  std::sort(occurrences.begin(), occurrences.end(), occurrence_less);
  occurrences.erase(std::unique(occurrences.begin(), occurrences.end(),
                                same_occurrence_identity),
                    occurrences.end());

  std::vector<GrammarCandidateStats> stats;
  for (const GrammarCandidateOccurrence& occurrence : occurrences) {
    const std::size_t run_len =
        occurrence.end_dense_index_exclusive - occurrence.begin_dense_index;
    const std::size_t saved_nodes = run_len > 0 ? run_len - 1 : 0;
    if (stats.empty() || !(stats.back().key == occurrence.key)) {
      stats.push_back(GrammarCandidateStats{
          occurrence.key,
          1,
          occurrence.begin_dense_index,
          saved_nodes,
          run_len});
      continue;
    }
    GrammarCandidateStats& row = stats.back();
    row.occurrence_count += 1;
    row.first_dense_index =
        std::min(row.first_dense_index, occurrence.begin_dense_index);
    row.gain += saved_nodes;
    row.max_run_len = std::max(row.max_run_len, run_len);
  }
  return stats;
}

}  // namespace

const char* grammar_producer_id_name(GrammarProducerId producer_id) {
  switch (producer_id) {
    case GrammarProducerId::kAdjacentRun:
      return "AdjacentRunProducer";
    case GrammarProducerId::kPairGrammar:
      return "PairGrammarProducer";
    case GrammarProducerId::kNativeMacroRun:
      return "NativeMacroRunProducer";
    case GrammarProducerId::kExactRepeatedBlock:
      return "ExactRepeatedBlockProducer";
  }
  return "unknown";
}

GrammarRoundResult run_adjacent_run_readonly_round(
    const GlobalGrammarState& state) {
  const GrammarSnapshot snapshot = freeze_grammar_snapshot(state);
  const DenseGrammarView dense = build_dense_grammar_view(snapshot);
  const std::vector<std::size_t> worker_by_chunk =
      build_worker_by_chunk(snapshot);

  GrammarRoundResult result;
  result.status = GrammarRoundStatus::kStop;
  result.snapshot_generation = snapshot.generation;
  result.producer_id = GrammarProducerId::kAdjacentRun;

  const std::size_t worker_count = worker_count_from_chunks(snapshot);
  result.local_outputs.reserve(worker_count);
  for (std::size_t worker_id = 0; worker_id < worker_count; ++worker_id) {
    result.local_outputs.push_back(GrammarLocalMapOutput{worker_id, {}, {}});
  }
  for (const GrammarChunk& chunk : snapshot.chunks) {
    result.local_outputs[chunk.owner_worker_id].chunk_ids.push_back(chunk.id);
  }

  std::size_t begin = 0;
  while (begin < dense.size()) {
    std::size_t end = begin + 1;
    while (end < dense.size() && dense.symbols[end] == dense.symbols[begin]) {
      ++end;
    }
    const std::size_t run_len = end - begin;
    if (run_len >= 2) {
      const GrammarSnapshotNode& begin_node = snapshot.nodes[begin];
      const GrammarSnapshotNode& last_node = snapshot.nodes[end - 1];
      if (!begin_node.owner_chunk_id.valid() ||
          begin_node.owner_chunk_id.value() >= worker_by_chunk.size()) {
        throw std::invalid_argument(
            "grammar round occurrence has invalid owner chunk");
      }
      const std::size_t owner_worker_id =
          worker_by_chunk[begin_node.owner_chunk_id.value()];
      GrammarCandidateOccurrence occurrence;
      occurrence.key =
          GrammarCandidateKey{GrammarProducerId::kAdjacentRun,
                              dense.symbols[begin], SymbolId::invalid(),
                              run_len};
      occurrence.begin_node_id = dense.node_ids[begin];
      occurrence.last_node_id = dense.node_ids[end - 1];
      occurrence.begin_dense_index = begin;
      occurrence.end_dense_index_exclusive = end;
      occurrence.start_ns = dense.start_ns[begin];
      occurrence.end_ns = dense.end_ns[end - 1];
      occurrence.owner_chunk_id = begin_node.owner_chunk_id;
      occurrence.owner_worker_id = owner_worker_id;
      occurrence.crosses_chunk_boundary =
          begin_node.owner_chunk_id != last_node.owner_chunk_id;
      result.local_outputs[owner_worker_id].occurrences.push_back(occurrence);
      result.occurrences.push_back(occurrence);
    }
    begin = end;
  }

  result.candidate_stats = reduce_adjacent_run_candidates(result.occurrences);
  if (result.candidate_stats.empty()) {
    return result;
  }

  const auto best = std::max_element(
      result.candidate_stats.begin(), result.candidate_stats.end(),
      [](const GrammarCandidateStats& lhs,
         const GrammarCandidateStats& rhs) { return stats_better(rhs, lhs); });
  if (best == result.candidate_stats.end() || best->gain == 0) {
    return result;
  }

  result.status = GrammarRoundStatus::kActionSelected;
  result.action.kind = GrammarActionKind::kReplaceExactRuns;
  result.action.snapshot_generation = snapshot.generation;
  result.action.key = best->key;
  result.action.replace_count = best->occurrence_count;
  result.action.gain = best->gain;
  result.action.first_dense_index = best->first_dense_index;
  result.action.max_run_len = best->max_run_len;
  for (const GrammarCandidateOccurrence& occurrence : result.occurrences) {
    if (occurrence.key == best->key) {
      result.action.occurrences.push_back(occurrence);
    }
  }
  std::sort(result.action.occurrences.begin(),
            result.action.occurrences.end(), occurrence_less);
  return result;
}

GrammarRoundResult run_pair_grammar_readonly_round(
    const GlobalGrammarState& state) {
  const GrammarSnapshot snapshot = freeze_grammar_snapshot(state);
  const DenseGrammarView dense = build_dense_grammar_view(snapshot);
  const std::vector<std::size_t> worker_by_chunk =
      build_worker_by_chunk(snapshot);

  GrammarRoundResult result;
  result.status = GrammarRoundStatus::kStop;
  result.snapshot_generation = snapshot.generation;
  result.producer_id = GrammarProducerId::kPairGrammar;

  const std::size_t worker_count = worker_count_from_chunks(snapshot);
  result.local_outputs.reserve(worker_count);
  for (std::size_t worker_id = 0; worker_id < worker_count; ++worker_id) {
    result.local_outputs.push_back(GrammarLocalMapOutput{worker_id, {}, {}});
  }
  for (const GrammarChunk& chunk : snapshot.chunks) {
    result.local_outputs[chunk.owner_worker_id].chunk_ids.push_back(chunk.id);
  }

  if (dense.size() < 2) {
    return result;
  }
  for (std::size_t begin = 0; begin + 1 < dense.size(); ++begin) {
    if (dense.symbols[begin] == dense.symbols[begin + 1]) {
      continue;
    }
    const GrammarSnapshotNode& begin_node = snapshot.nodes[begin];
    const GrammarSnapshotNode& last_node = snapshot.nodes[begin + 1];
    if (!begin_node.owner_chunk_id.valid() ||
        begin_node.owner_chunk_id.value() >= worker_by_chunk.size()) {
      throw std::invalid_argument(
          "grammar pair occurrence has invalid owner chunk");
    }
    const std::size_t owner_worker_id =
        worker_by_chunk[begin_node.owner_chunk_id.value()];
    GrammarCandidateOccurrence occurrence;
    occurrence.key = GrammarCandidateKey{
        GrammarProducerId::kPairGrammar,
        dense.symbols[begin],
        dense.symbols[begin + 1],
        2};
    occurrence.begin_node_id = dense.node_ids[begin];
    occurrence.last_node_id = dense.node_ids[begin + 1];
    occurrence.begin_dense_index = begin;
    occurrence.end_dense_index_exclusive = begin + 2;
    occurrence.start_ns = dense.start_ns[begin];
    occurrence.end_ns = dense.end_ns[begin + 1];
    occurrence.owner_chunk_id = begin_node.owner_chunk_id;
    occurrence.owner_worker_id = owner_worker_id;
    occurrence.crosses_chunk_boundary =
        begin_node.owner_chunk_id != last_node.owner_chunk_id;
    result.local_outputs[owner_worker_id].occurrences.push_back(occurrence);
    result.occurrences.push_back(occurrence);
  }

  result.candidate_stats = reduce_pair_grammar_candidates(result.occurrences);
  if (result.candidate_stats.empty()) {
    return result;
  }

  const auto best = std::max_element(
      result.candidate_stats.begin(), result.candidate_stats.end(),
      [](const GrammarCandidateStats& lhs,
         const GrammarCandidateStats& rhs) {
        return pair_stats_better(rhs, lhs);
      });
  if (best == result.candidate_stats.end() || best->gain == 0 ||
      best->occurrence_count < 4) {
    return result;
  }

  result.status = GrammarRoundStatus::kActionSelected;
  result.action.kind = GrammarActionKind::kReplacePair;
  result.action.snapshot_generation = snapshot.generation;
  result.action.key = best->key;
  result.action.replace_count = best->occurrence_count;
  result.action.gain = best->gain;
  result.action.first_dense_index = best->first_dense_index;
  result.action.max_run_len = best->max_run_len;
  for (const GrammarCandidateOccurrence& occurrence : result.occurrences) {
    if (occurrence.key == best->key) {
      result.action.occurrences.push_back(occurrence);
    }
  }
  std::sort(result.action.occurrences.begin(),
            result.action.occurrences.end(), occurrence_less);
  return result;
}

GrammarRoundResult run_native_macro_run_readonly_round(
    const GlobalGrammarState& state) {
  const GrammarSnapshot snapshot = freeze_grammar_snapshot(state);
  const DenseGrammarView dense = build_dense_grammar_view(snapshot);
  const std::vector<std::size_t> worker_by_chunk =
      build_worker_by_chunk(snapshot);
  const std::set<SymbolId> eligible_symbols = eligible_macro_symbols(snapshot);

  GrammarRoundResult result;
  result.status = GrammarRoundStatus::kStop;
  result.snapshot_generation = snapshot.generation;
  result.producer_id = GrammarProducerId::kNativeMacroRun;

  const std::size_t worker_count = worker_count_from_chunks(snapshot);
  result.local_outputs.reserve(worker_count);
  for (std::size_t worker_id = 0; worker_id < worker_count; ++worker_id) {
    result.local_outputs.push_back(GrammarLocalMapOutput{worker_id, {}, {}});
  }
  for (const GrammarChunk& chunk : snapshot.chunks) {
    result.local_outputs[chunk.owner_worker_id].chunk_ids.push_back(chunk.id);
  }

  std::size_t begin = 0;
  while (begin < dense.size()) {
    std::size_t end = begin + 1;
    while (end < dense.size() && dense.symbols[end] == dense.symbols[begin]) {
      ++end;
    }
    const std::size_t run_len = end - begin;
    if (run_len >= 2 &&
        eligible_symbols.find(dense.symbols[begin]) != eligible_symbols.end()) {
      const GrammarSnapshotNode& begin_node = snapshot.nodes[begin];
      const GrammarSnapshotNode& last_node = snapshot.nodes[end - 1];
      if (!begin_node.owner_chunk_id.valid() ||
          begin_node.owner_chunk_id.value() >= worker_by_chunk.size()) {
        throw std::invalid_argument(
            "grammar macro-run occurrence has invalid owner chunk");
      }
      const std::size_t owner_worker_id =
          worker_by_chunk[begin_node.owner_chunk_id.value()];
      GrammarCandidateOccurrence occurrence;
      occurrence.key =
          GrammarCandidateKey{GrammarProducerId::kNativeMacroRun,
                              dense.symbols[begin], SymbolId::invalid(),
                              0};
      occurrence.begin_node_id = dense.node_ids[begin];
      occurrence.last_node_id = dense.node_ids[end - 1];
      occurrence.begin_dense_index = begin;
      occurrence.end_dense_index_exclusive = end;
      occurrence.start_ns = dense.start_ns[begin];
      occurrence.end_ns = dense.end_ns[end - 1];
      occurrence.owner_chunk_id = begin_node.owner_chunk_id;
      occurrence.owner_worker_id = owner_worker_id;
      occurrence.crosses_chunk_boundary =
          begin_node.owner_chunk_id != last_node.owner_chunk_id;
      result.local_outputs[owner_worker_id].occurrences.push_back(occurrence);
      result.occurrences.push_back(occurrence);
    }
    begin = end;
  }

  result.candidate_stats = reduce_macro_run_candidates(result.occurrences);
  if (result.candidate_stats.empty()) {
    return result;
  }

  const auto best = std::max_element(
      result.candidate_stats.begin(), result.candidate_stats.end(),
      [](const GrammarCandidateStats& lhs,
         const GrammarCandidateStats& rhs) {
        return macro_run_stats_better(rhs, lhs);
      });
  if (best == result.candidate_stats.end() || best->gain <= 1) {
    return result;
  }

  result.status = GrammarRoundStatus::kActionSelected;
  result.action.kind = GrammarActionKind::kCompressMaximalRuns;
  result.action.snapshot_generation = snapshot.generation;
  result.action.key = best->key;
  result.action.replace_count = best->occurrence_count;
  result.action.gain = best->gain;
  result.action.first_dense_index = best->first_dense_index;
  result.action.max_run_len = best->max_run_len;
  for (const GrammarCandidateOccurrence& occurrence : result.occurrences) {
    if (occurrence.key == best->key) {
      result.action.occurrences.push_back(occurrence);
    }
  }
  std::sort(result.action.occurrences.begin(),
            result.action.occurrences.end(), occurrence_less);
  return result;
}

GrammarRoundResult run_exact_repeated_block_readonly_round(
    const GlobalGrammarState& state) {
  const GrammarSnapshot snapshot = freeze_grammar_snapshot(state);
  const DenseGrammarView dense = build_dense_grammar_view(snapshot);
  const std::vector<std::size_t> worker_by_chunk =
      build_worker_by_chunk(snapshot);

  GrammarRoundResult result;
  result.status = GrammarRoundStatus::kStop;
  result.snapshot_generation = snapshot.generation;
  result.producer_id = GrammarProducerId::kExactRepeatedBlock;

  const std::size_t worker_count = worker_count_from_chunks(snapshot);
  result.local_outputs.reserve(worker_count);
  for (std::size_t worker_id = 0; worker_id < worker_count; ++worker_id) {
    result.local_outputs.push_back(GrammarLocalMapOutput{worker_id, {}, {}});
  }
  for (const GrammarChunk& chunk : snapshot.chunks) {
    result.local_outputs[chunk.owner_worker_id].chunk_ids.push_back(chunk.id);
  }

  const std::size_t sequence_len = dense.size();
  if (sequence_len < 4 ||
      sequence_len > state.metadata.full_discovery_cap) {
    return result;
  }

  // Deterministic exact tiling discovery: scan block lengths ascending and
  // pick the smallest length that tiles the entire live sequence exactly with
  // exact symbol equality. The chosen block is required to be non-uniform
  // (contain at least two distinct symbols): uniform runs are owned by the
  // adjacent-run producer, which yields maximal runs for them. For non-uniform
  // input the smallest tiling length is the primitive multi-symbol block
  // (the minimal period of the pure power), so the choice is unique and
  // maximizes the repeat count.
  std::size_t block_len = 0;
  for (std::size_t len = 2; len * 2 <= sequence_len; ++len) {
    if (sequence_len % len != 0) {
      continue;
    }
    bool non_uniform = false;
    for (std::size_t index = 1; index < len; ++index) {
      if (dense.symbols[index] != dense.symbols[0]) {
        non_uniform = true;
        break;
      }
    }
    if (!non_uniform) {
      continue;
    }
    bool tiles = true;
    for (std::size_t index = 0; index < sequence_len && tiles; ++index) {
      if (dense.symbols[index] != dense.symbols[index % len]) {
        tiles = false;
      }
    }
    if (tiles) {
      block_len = len;
      break;
    }
  }
  if (block_len == 0) {
    return result;
  }

  const std::size_t repeat_count = sequence_len / block_len;
  const GrammarCandidateKey key{
      GrammarProducerId::kExactRepeatedBlock,
      dense.symbols[0], SymbolId::invalid(), block_len};

  std::vector<SymbolId> block_symbols;
  block_symbols.reserve(block_len);
  for (std::size_t index = 0; index < block_len; ++index) {
    block_symbols.push_back(dense.symbols[index]);
  }

  for (std::size_t repetition = 0; repetition < repeat_count; ++repetition) {
    const std::size_t begin = repetition * block_len;
    const std::size_t end = begin + block_len;
    const GrammarSnapshotNode& begin_node = snapshot.nodes[begin];
    const GrammarSnapshotNode& last_node = snapshot.nodes[end - 1];
    if (!begin_node.owner_chunk_id.valid() ||
        begin_node.owner_chunk_id.value() >= worker_by_chunk.size()) {
      throw std::invalid_argument(
          "grammar repeated-block occurrence has invalid owner chunk");
    }
    const std::size_t owner_worker_id =
        worker_by_chunk[begin_node.owner_chunk_id.value()];
    GrammarCandidateOccurrence occurrence;
    occurrence.key = key;
    occurrence.begin_node_id = dense.node_ids[begin];
    occurrence.last_node_id = dense.node_ids[end - 1];
    occurrence.begin_dense_index = begin;
    occurrence.end_dense_index_exclusive = end;
    occurrence.start_ns = dense.start_ns[begin];
    occurrence.end_ns = dense.end_ns[end - 1];
    occurrence.owner_chunk_id = begin_node.owner_chunk_id;
    occurrence.owner_worker_id = owner_worker_id;
    occurrence.crosses_chunk_boundary =
        begin_node.owner_chunk_id != last_node.owner_chunk_id;
    result.local_outputs[owner_worker_id].occurrences.push_back(occurrence);
    result.occurrences.push_back(occurrence);
  }

  result.candidate_stats.push_back(GrammarCandidateStats{
      key,
      repeat_count,
      0,
      repeat_count * block_len - 1,
      block_len});

  result.status = GrammarRoundStatus::kActionSelected;
  result.action.kind = GrammarActionKind::kReplaceRepeatedBlock;
  result.action.snapshot_generation = snapshot.generation;
  result.action.key = key;
  result.action.block_rhs_symbols = std::move(block_symbols);
  result.action.repeat_count = repeat_count;
  result.action.replace_count = repeat_count;
  result.action.gain = repeat_count * block_len - 1;
  result.action.first_dense_index = 0;
  result.action.max_run_len = block_len;
  result.action.occurrences = result.occurrences;
  return result;
}

}  // namespace traceloom
