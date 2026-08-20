#include "compact_grammar_projection.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "traceloom/pattern/grammar_snapshot.h"

namespace traceloom::compat::detail {
namespace {

const char* macro_level_label(MacroLevel level) {
  switch (level) {
    case MacroLevel::kRP:
      return "RP";
    case MacroLevel::kLP:
      return "LP";
    case MacroLevel::kSemantic:
      return "semantic";
  }
  return "unknown";
}

std::string grammar_symbol_label(
    const NativeIr& ir,
    const std::unordered_map<SymbolId::value_type, const MacroDefRow*>&
        macro_by_symbol,
    SymbolId symbol_id) {
  if (!symbol_id.valid()) {
    return "<invalid>";
  }
  if (symbol_id.value() < ir.symbols.size()) {
    return ir.symbols.value(symbol_id);
  }
  const auto found = macro_by_symbol.find(symbol_id.value());
  if (found == macro_by_symbol.end()) {
    return "<macro-symbol:" + std::to_string(symbol_id.value()) + ">";
  }
  const MacroDefRow& macro = *found->second;
  return macro.display_label.empty()
             ? "M" + std::to_string(macro.id.value())
             : macro.display_label;
}

std::uint32_t anchor_idx_at(
    const std::vector<StructuralProjectionToken>& tokens,
    std::size_t token_index) {
  if (token_index >= tokens.size() || !tokens[token_index].anchor_id.valid()) {
    return 0;
  }
  return tokens[token_index].anchor_id.value() + 1;
}

}  // namespace

NativeCompactGrammarProjection summarize_compact_grammar(
    const NativeIr& ir,
    const std::vector<StructuralProjectionToken>& tokens,
    const GlobalGrammarState& state,
    const GrammarEngineResult& result,
    std::uint32_t device_id) {
  NativeCompactGrammarProjection summary;
  summary.device_id = device_id;
  summary.available = result.ok() && state.stage == GrammarStage::kDone;
  summary.stop_reason = grammar_engine_stop_reason_name(result.stop_reason);
  summary.engine_step_count = result.steps.size();
  summary.source_token_count = tokens.size();
  if (!summary.available) {
    return summary;
  }

  const GrammarSnapshot snapshot = freeze_grammar_snapshot(state);
  std::unordered_map<SymbolId::value_type, const MacroDefRow*>
      macro_by_symbol;
  macro_by_symbol.reserve(snapshot.macro_defs.size());
  for (const MacroDefRow& macro : snapshot.macro_defs) {
    macro_by_symbol.emplace(macro.symbol_id.value(), &macro);
  }

  summary.live_nodes.reserve(snapshot.nodes.size());
  for (const GrammarSnapshotNode& node : snapshot.nodes) {
    if (node.source_begin_token_index >=
            node.source_end_token_index_exclusive ||
        node.source_end_token_index_exclusive > tokens.size()) {
      throw std::invalid_argument(
          "compact grammar live node has an invalid source span");
    }
    NativeGrammarLiveNodeSummary row;
    row.grammar_node_id = node.node_id.value();
    row.symbol_id = node.symbol_id.value();
    row.has_macro_def_id = node.macro_def_id.valid();
    if (node.macro_def_id.valid()) {
      row.macro_def_id = node.macro_def_id.value();
    }
    row.label = grammar_symbol_label(ir, macro_by_symbol, node.symbol_id);
    row.source_begin_token_index = node.source_begin_token_index;
    row.source_end_token_index_exclusive =
        node.source_end_token_index_exclusive;
    row.first_anchor_idx =
        anchor_idx_at(tokens, node.source_begin_token_index);
    row.last_anchor_idx = anchor_idx_at(
        tokens, node.source_end_token_index_exclusive - 1);
    row.span_us = static_cast<double>(node.end_ns - node.start_ns) / 1000.0;
    summary.live_nodes.push_back(std::move(row));
  }

  summary.macro_defs.reserve(snapshot.macro_defs.size());
  for (const MacroDefRow& macro : snapshot.macro_defs) {
    NativeGrammarMacroSummary row;
    row.macro_def_id = macro.id.value();
    row.symbol_id = macro.symbol_id.value();
    row.level = macro_level_label(macro.level);
    row.label = grammar_symbol_label(ir, macro_by_symbol, macro.symbol_id);
    row.definition_len = macro.definition_len;
    row.replace_count = macro.replace_count;
    row.gain = macro.gain;
    row.first_pos = macro.first_pos;
    row.rhs_labels.reserve(macro.rhs_symbols.size());
    for (SymbolId rhs_symbol : macro.rhs_symbols) {
      row.rhs_labels.push_back(
          grammar_symbol_label(ir, macro_by_symbol, rhs_symbol));
    }
    summary.macro_defs.push_back(std::move(row));
  }

  for (const GrammarNode& node : state.nodes) {
    if (!node.macro_def_id.valid() ||
        node.macro_def_id.value() >= summary.macro_defs.size()) {
      continue;
    }
    if (node.source_begin_token_index >=
            node.source_end_token_index_exclusive ||
        node.source_end_token_index_exclusive > tokens.size()) {
      throw std::invalid_argument(
          "compact grammar macro occurrence has an invalid source span");
    }
    NativeGrammarMacroSummary& row =
        summary.macro_defs[node.macro_def_id.value()];
    ++row.occurrence_count;
    const std::uint32_t first_anchor =
        anchor_idx_at(tokens, node.source_begin_token_index);
    const std::uint32_t last_anchor = anchor_idx_at(
        tokens, node.source_end_token_index_exclusive - 1);
    if (row.first_anchor_idx == 0 ||
        (first_anchor != 0 && first_anchor < row.first_anchor_idx)) {
      row.first_anchor_idx = first_anchor;
    }
    row.last_anchor_idx = std::max(row.last_anchor_idx, last_anchor);
    row.inclusive_span_us +=
        static_cast<double>(node.end_ns - node.start_ns) / 1000.0;
  }
  return summary;
}

}  // namespace traceloom::compat::detail
