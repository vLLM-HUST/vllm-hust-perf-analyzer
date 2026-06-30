#include "traceloom/pattern/grammar_apply.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

#include "traceloom/pattern/grammar_snapshot.h"

namespace traceloom {
namespace {

void reject(GrammarApplyResult& result, GrammarApplyDiagnosticCode code) {
  result.status = GrammarApplyStatus::kRejected;
  result.diagnostics.push_back(GrammarApplyDiagnostic{code});
}

bool replacement_spans_equal(const std::vector<GrammarReplacementSpan>& lhs,
                             const std::vector<GrammarReplacementSpan>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (lhs[index].begin_dense_index != rhs[index].begin_dense_index ||
        lhs[index].end_dense_index_exclusive !=
            rhs[index].end_dense_index_exclusive ||
        lhs[index].begin_node_id != rhs[index].begin_node_id ||
        lhs[index].last_node_id != rhs[index].last_node_id) {
      return false;
    }
  }
  return true;
}

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
  for (std::size_t index = first; index <= last; ++index) {
    if (nodes[index].symbol_id != summary.prefix_run_symbol) {
      break;
    }
    ++summary.prefix_run_len;
  }

  summary.suffix_run_symbol = summary.last_live_symbol;
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

void rebuild_chunks(GlobalGrammarState& state) {
  state.chunks.clear();
  state.boundary_summaries.clear();
  if (state.nodes.empty()) {
    state.live_node_count = 0;
    return;
  }
  if (state.target_nodes_per_chunk == 0 || state.worker_count == 0) {
    throw std::invalid_argument("invalid grammar state chunking config");
  }

  for (std::size_t begin = 0; begin < state.nodes.size();
       begin += state.target_nodes_per_chunk) {
    const std::size_t end =
        std::min(state.nodes.size(), begin + state.target_nodes_per_chunk);
    const auto chunk_id = checked_next_id<GrammarChunkId>(state.chunks.size());
    const std::size_t owner_worker_id =
        state.chunks.size() % state.worker_count;
    state.chunks.push_back(GrammarChunk{
        chunk_id,
        state.chunks.size(),
        owner_worker_id,
        GrammarNodeId(static_cast<GrammarNodeId::value_type>(begin)),
        GrammarNodeId(static_cast<GrammarNodeId::value_type>(end - 1)),
        end - begin,
        state.generation});
    for (std::size_t index = begin; index < end; ++index) {
      state.nodes[index].id =
          GrammarNodeId(static_cast<GrammarNodeId::value_type>(index));
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
      state.nodes[index].alive = true;
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
  state.live_node_count = state.nodes.size();
}

bool protected_intervals_remain_contiguous(
    const std::vector<GrammarNode>& nodes,
    const std::vector<ProtectedIntervalSpan>& intervals) {
  for (const ProtectedIntervalSpan& interval : intervals) {
    bool seen = false;
    bool closed = false;
    for (const GrammarNode& node : nodes) {
      const bool overlaps =
          node.source_begin_token_index <= interval.last_token_index &&
          node.source_end_token_index_exclusive > interval.first_token_index;
      if (overlaps) {
        if (closed) {
          return false;
        }
        seen = true;
      } else if (seen) {
        closed = true;
      }
    }
    if (!seen) {
      return false;
    }
  }
  return true;
}

SymbolId allocate_macro_symbol(GlobalGrammarState& state) {
  const SymbolId symbol = state.next_macro_symbol_id;
  if (!symbol.valid() ||
      symbol.value() == std::numeric_limits<SymbolId::value_type>::max()) {
    throw std::overflow_error("macro symbol id space exhausted");
  }
  state.next_macro_symbol_id = SymbolId(symbol.value() + 1);
  return symbol;
}

}  // namespace

const char* grammar_apply_diagnostic_code_name(
    GrammarApplyDiagnosticCode code) {
  switch (code) {
    case GrammarApplyDiagnosticCode::kInvalidCommitPlan:
      return "invalid_commit_plan";
    case GrammarApplyDiagnosticCode::kStaleStateGeneration:
      return "stale_state_generation";
    case GrammarApplyDiagnosticCode::kCommitPlanRevalidationFailed:
      return "commit_plan_revalidation_failed";
    case GrammarApplyDiagnosticCode::kProtectedIntervalMappingFailed:
      return "protected_interval_mapping_failed";
    case GrammarApplyDiagnosticCode::kNoProgress:
      return "no_progress";
  }
  return "unknown";
}

GrammarApplyResult apply_adjacent_run_commit_plan(
    GlobalGrammarState& state,
    const GrammarCommitPlan& plan) {
  GrammarApplyResult result;
  if (!plan.valid()) {
    reject(result, GrammarApplyDiagnosticCode::kInvalidCommitPlan);
    return result;
  }
  if (plan.snapshot_generation != state.generation ||
      plan.action.snapshot_generation != state.generation) {
    reject(result, GrammarApplyDiagnosticCode::kStaleStateGeneration);
    return result;
  }

  const GrammarSnapshot snapshot = freeze_grammar_snapshot(state);
  const GrammarCommitPlan revalidated =
      build_adjacent_run_commit_plan(snapshot, plan.action);
  if (!revalidated.valid() ||
      !replacement_spans_equal(revalidated.replacement_spans,
                               plan.replacement_spans)) {
    reject(result, GrammarApplyDiagnosticCode::kCommitPlanRevalidationFailed);
    return result;
  }

  GlobalGrammarState next = state;
  const MacroDefId macro_def_id =
      checked_next_id<MacroDefId>(next.macro_defs.size());
  const SymbolId macro_symbol_id = allocate_macro_symbol(next);
  std::vector<SymbolId> rhs_symbols(plan.action.key.run_len,
                                    plan.action.key.symbol_id);
  next.macro_defs.push_back(MacroDefRow{
      macro_def_id,
      macro_symbol_id,
      MacroLevel::kLP,
      rhs_symbols,
      plan.action.key.run_len,
      plan.replacement_spans.size(),
      static_cast<std::ptrdiff_t>(plan.action.gain),
      plan.action.first_dense_index});

  std::vector<GrammarNode> rewritten_nodes;
  rewritten_nodes.reserve(snapshot.nodes.size());
  std::size_t replacement_index = 0;
  std::size_t dense_index = 0;
  while (dense_index < snapshot.nodes.size()) {
    if (replacement_index < plan.replacement_spans.size() &&
        dense_index ==
            plan.replacement_spans[replacement_index].begin_dense_index) {
      const GrammarReplacementSpan& span =
          plan.replacement_spans[replacement_index];
      rewritten_nodes.push_back(GrammarNode{
          GrammarNodeId::invalid(),
          macro_symbol_id,
          macro_def_id,
          span.source_begin_token_index,
          span.source_end_token_index_exclusive,
          span.start_ns,
          span.end_ns,
          GrammarChunkId::invalid(),
          GrammarNodeId::invalid(),
          GrammarNodeId::invalid(),
          true});
      dense_index = span.end_dense_index_exclusive;
      ++replacement_index;
      continue;
    }

    const GrammarSnapshotNode& old = snapshot.nodes[dense_index];
    rewritten_nodes.push_back(GrammarNode{
        GrammarNodeId::invalid(),
        old.symbol_id,
        old.macro_def_id,
        old.source_begin_token_index,
        old.source_end_token_index_exclusive,
        old.start_ns,
        old.end_ns,
        GrammarChunkId::invalid(),
        GrammarNodeId::invalid(),
        GrammarNodeId::invalid(),
        true});
    ++dense_index;
  }

  if (rewritten_nodes.size() >= snapshot.nodes.size()) {
    reject(result, GrammarApplyDiagnosticCode::kNoProgress);
    return result;
  }
  if (!protected_intervals_remain_contiguous(rewritten_nodes,
                                             state.protected_intervals)) {
    reject(result,
           GrammarApplyDiagnosticCode::kProtectedIntervalMappingFailed);
    return result;
  }

  next.nodes = std::move(rewritten_nodes);
  next.generation = state.generation + 1;
  rebuild_chunks(next);
  state = std::move(next);
  result.status = GrammarApplyStatus::kApplied;
  return result;
}

}  // namespace traceloom
