#include "traceloom/pattern/grammar_state.h"

#include <algorithm>
#include <limits>
#include <map>
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

struct SemanticReplayInterval {
  ProtectedIntervalId protected_interval_id;
  ReplayUnitId replay_unit_id;
  GraphTemplateId graph_template_id;
  std::size_t first_token_index = 0;
  std::size_t last_token_index = 0;
};

std::vector<SemanticReplayInterval> semantic_replay_intervals(
    const NativeIr& ir,
    const ProtectedSequence& sequence,
    const BoundaryIndex& boundary_index) {
  std::vector<SemanticReplayInterval> out;
  for (const ProtectedIntervalSpan& span : boundary_index.intervals()) {
    const ProtectedIntervalRow& interval =
        ir.protected_intervals.row(span.id);
    if (interval.kind != ProtectedIntervalKind::kGraphReplayUnit) {
      continue;
    }
    const AnchorId first_anchor_id =
        sequence.token_at(span.first_token_index).anchor_id;
    const AnchorId last_anchor_id =
        sequence.token_at(span.last_token_index).anchor_id;
    if (!first_anchor_id.valid() || !last_anchor_id.valid()) {
      continue;
    }
    const AnchorRow& first_anchor = ir.anchors.row(first_anchor_id);
    const AnchorRow& last_anchor = ir.anchors.row(last_anchor_id);
    if (!first_anchor.replay_unit_id.valid() ||
        first_anchor.replay_unit_id != last_anchor.replay_unit_id) {
      continue;
    }
    const ReplayUnitRow& replay =
        ir.replay_units.row(first_anchor.replay_unit_id);
    if (!replay.replay_composition_region_id.valid()) {
      continue;
    }
    if (replay.replay_composition_region_id.value() >=
            ir.replay_composition_regions.size() ||
        !replay.graph_template_id.valid() ||
        replay.graph_template_id.value() >= ir.graph_templates.size()) {
      throw std::invalid_argument(
          "exact replay interval references invalid semantic evidence");
    }
    const ReplayCompositionRegionRow& region =
        ir.replay_composition_regions.row(
            replay.replay_composition_region_id);
    if (region.status !=
            ReplayCompositionRegionStatus::kRecognizedCompletePattern ||
        region.observed_launch_count == 0 ||
        region.observed_launch_count != region.expected_launch_count ||
        !region.replay_composition_candidate_id.valid() ||
        region.replay_composition_candidate_id.value() >=
            ir.replay_composition_candidates.size() ||
        replay.first_anchor_id != first_anchor_id ||
        replay.last_anchor_id != last_anchor_id) {
      throw std::invalid_argument(
          "exact replay interval is not a complete composition");
    }
    const ReplayCompositionCandidateRow& candidate =
        ir.replay_composition_candidates.row(
            region.replay_composition_candidate_id);
    if (candidate.shape_policy !=
        ReplayCompositionShapePolicy::kHeadRepeatedLayerTail) {
      throw std::invalid_argument(
          "exact replay interval has unsupported composition shape");
    }
    for (std::size_t token_index = span.first_token_index;
         token_index <= span.last_token_index; ++token_index) {
      const AnchorId anchor_id = sequence.token_at(token_index).anchor_id;
      if (!anchor_id.valid() ||
          ir.anchors.row(anchor_id).replay_unit_id != replay.id) {
        throw std::invalid_argument(
            "exact replay interval mixes replay-unit membership");
      }
    }
    if (!out.empty() &&
        span.first_token_index <= out.back().last_token_index) {
      throw std::invalid_argument("exact replay intervals overlap");
    }
    out.push_back(SemanticReplayInterval{
        span.id, replay.id, replay.graph_template_id,
        span.first_token_index, span.last_token_index});
  }
  return out;
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
  state.target_nodes_per_chunk = config.target_nodes_per_chunk;
  state.worker_count = config.worker_count;

  SymbolId::value_type max_symbol_value = 0;
  for (std::size_t index = 0; index < sequence.size(); ++index) {
    const ProtectedSequenceToken& token = sequence.token_at(index);
    if (token.symbol_id.valid()) {
      max_symbol_value = std::max(max_symbol_value, token.symbol_id.value());
    }
  }
  if (max_symbol_value == std::numeric_limits<SymbolId::value_type>::max()) {
    throw std::overflow_error("symbol id space exhausted");
  }
  SymbolId next_symbol(max_symbol_value + 1);

  const std::vector<SemanticReplayInterval> semantic_intervals =
      semantic_replay_intervals(ir, sequence, boundary_index);
  std::map<GraphTemplateId::value_type, MacroDefId>
      semantic_macro_by_template;
  state.nodes.reserve(sequence.size());
  std::size_t semantic_interval_index = 0;
  for (std::size_t index = 0; index < sequence.size();) {
    if (semantic_interval_index < semantic_intervals.size() &&
        semantic_intervals[semantic_interval_index].first_token_index ==
            index) {
      const SemanticReplayInterval& interval =
          semantic_intervals[semantic_interval_index];
      std::vector<SymbolId> rhs_symbols;
      rhs_symbols.reserve(interval.last_token_index -
                          interval.first_token_index + 1);
      for (std::size_t token_index = interval.first_token_index;
           token_index <= interval.last_token_index; ++token_index) {
        rhs_symbols.push_back(sequence.token_at(token_index).symbol_id);
      }

      MacroDefId macro_def_id;
      SymbolId macro_symbol_id;
      const auto existing = semantic_macro_by_template.find(
          interval.graph_template_id.value());
      if (existing == semantic_macro_by_template.end()) {
        macro_def_id = checked_next_id<MacroDefId>(state.macro_defs.size());
        macro_symbol_id = next_symbol;
        if (macro_symbol_id.value() ==
            std::numeric_limits<SymbolId::value_type>::max()) {
          throw std::overflow_error("symbol id space exhausted");
        }
        next_symbol = SymbolId(macro_symbol_id.value() + 1);
        state.macro_defs.push_back(MacroDefRow{
            macro_def_id,
            macro_symbol_id,
            MacroLevel::kSemantic,
            rhs_symbols,
            rhs_symbols.size(),
            1,
            static_cast<std::ptrdiff_t>(rhs_symbols.size() - 1),
            interval.first_token_index,
            "ReplayUnit T" +
                std::to_string(interval.graph_template_id.value() + 1)});
        semantic_macro_by_template.emplace(
            interval.graph_template_id.value(), macro_def_id);
      } else {
        macro_def_id = existing->second;
        MacroDefRow& macro = state.macro_defs[macro_def_id.value()];
        if (macro.rhs_symbols != rhs_symbols) {
          throw std::invalid_argument(
              "graph template has inconsistent semantic token bodies");
        }
        macro_symbol_id = macro.symbol_id;
        ++macro.replace_count;
        macro.gain += static_cast<std::ptrdiff_t>(rhs_symbols.size() - 1);
      }

      const ProtectedSequenceToken& first =
          sequence.token_at(interval.first_token_index);
      const ProtectedSequenceToken& last =
          sequence.token_at(interval.last_token_index);
      const auto node_id = checked_next_id<GrammarNodeId>(state.nodes.size());
      state.nodes.push_back(GrammarNode{
          node_id, macro_symbol_id, macro_def_id,
          interval.first_token_index, interval.last_token_index + 1,
          first.start_ns, last.end_ns, GrammarChunkId::invalid(),
          GrammarNodeId::invalid(), GrammarNodeId::invalid(), true});
      index = interval.last_token_index + 1;
      ++semantic_interval_index;
      continue;
    }

    const ProtectedSequenceToken& token = sequence.token_at(index);
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
    ++index;
  }
  state.live_node_count = state.nodes.size();
  state.next_macro_symbol_id = next_symbol;

  for (const ProtectedIntervalSpan& span : boundary_index.intervals()) {
    const bool consumed = std::any_of(
        semantic_intervals.begin(), semantic_intervals.end(),
        [&](const SemanticReplayInterval& interval) {
          return interval.protected_interval_id == span.id;
        });
    if (!consumed) {
      state.protected_intervals.push_back(span);
    }
  }

  for (std::size_t begin = 0; begin < state.nodes.size();
       begin += config.target_nodes_per_chunk) {
    const std::size_t end =
        std::min(state.nodes.size(), begin + config.target_nodes_per_chunk);
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
