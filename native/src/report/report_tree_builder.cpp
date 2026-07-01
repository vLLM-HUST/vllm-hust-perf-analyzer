#include "traceloom/report/report_tree_builder.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

#include "traceloom/pattern/grammar_snapshot.h"

namespace traceloom {
namespace {

std::string local_node_id(std::size_t index) {
  const std::uint32_t n = static_cast<std::uint32_t>(index + 1);
  std::string digits = std::to_string(n);
  if (digits.size() < 3) {
    digits.insert(digits.begin(), 3 - digits.size(), '0');
  }
  return "N" + digits;
}

bool same_visible_token(const ReportToken& lhs, const ReportToken& rhs) {
  return lhs.symbol_id == rhs.symbol_id && lhs.display_op == rhs.display_op &&
         lhs.display_category == rhs.display_category &&
         lhs.anchor_kind == rhs.anchor_kind;
}

void validate_tokens_for_report_anchors(const std::vector<ReportToken>& tokens) {
  for (const ReportToken& token : tokens) {
    if (token.anchor_kind == ReportAnchorKind::kGraphLaunchActivity) {
      throw std::invalid_argument(
          "graph launch/activity metadata cannot become a report anchor");
    }
  }
}

std::string graph_tiling_diagnostic_code(const ReportGraphReplayEvidence& graph) {
  if (!graph.diagnostic_code.empty()) {
    return graph.diagnostic_code;
  }
  switch (graph.tiling_status) {
    case ReportGraphTilingStatus::kGap:
      return "graph_replay_tiling_gap";
    case ReportGraphTilingStatus::kOverlap:
      return "graph_replay_tiling_overlap";
    case ReportGraphTilingStatus::kAmbiguous:
      return "graph_replay_tiling_ambiguous";
    case ReportGraphTilingStatus::kNone:
    case ReportGraphTilingStatus::kExact:
      break;
  }
  return "";
}

bool graph_tiling_blocks_materialization(
    const ReportGraphReplayEvidence& graph) {
  return graph.tiling_status == ReportGraphTilingStatus::kGap ||
         graph.tiling_status == ReportGraphTilingStatus::kOverlap ||
         graph.tiling_status == ReportGraphTilingStatus::kAmbiguous;
}

ReportNodeDefId append_def(ReportTree& tree,
                           ReportNodeKind kind,
                           std::string display_op,
                           std::string display_category,
                           SymbolId symbol_id,
                           std::uint32_t repeat_count,
                           std::uint32_t display_depth,
                           std::uint32_t loop_depth,
                           std::string visibility_reason) {
  const ReportNodeDefId id =
      checked_next_id<ReportNodeDefId>(tree.node_defs.size());
  ReportNodeDef def;
  def.id = id;
  def.local_node_id = local_node_id(tree.node_defs.size());
  def.kind = kind;
  def.display_op = std::move(display_op);
  def.display_category = std::move(display_category);
  def.symbol_id = symbol_id;
  def.repeat_count = repeat_count;
  def.definition_order = id.value();
  def.display_depth = display_depth;
  def.loop_depth = loop_depth;
  def.visibility_reason = std::move(visibility_reason);
  tree.node_defs.push_back(std::move(def));
  return id;
}

ReportNodeOccurrenceId append_occurrence(ReportTree& tree,
                                         ReportNodeDefId def_id,
                                         ReportNodeOccurrenceId parent_id,
                                         std::uint32_t edge_order,
                                         std::uint32_t token_start,
                                         std::uint32_t token_end,
                                         std::uint32_t repeat_iteration) {
  const ReportNodeOccurrenceId id =
      checked_next_id<ReportNodeOccurrenceId>(tree.occurrences.size());
  std::uint32_t occurrence_index = 0;
  for (const ReportNodeOccurrence& existing : tree.occurrences) {
    if (existing.node_def_id == def_id) {
      ++occurrence_index;
    }
  }

  ReportNodeOccurrence occurrence;
  occurrence.id = id;
  occurrence.node_def_id = def_id;
  occurrence.parent_occurrence_id = parent_id;
  occurrence.edge_order = edge_order;
  occurrence.occurrence_index_for_def = occurrence_index;
  occurrence.token_start_ordinal = token_start;
  occurrence.token_end_ordinal = token_end;
  occurrence.repeat_iteration = repeat_iteration;
  tree.occurrences.push_back(occurrence);

  if (parent_id.valid()) {
    tree.edges.push_back(ReportTreeEdge{parent_id, id, edge_order});
  }
  return id;
}

void append_coverage(ReportTree& tree,
                     ReportNodeOccurrenceId occurrence_id,
                     std::uint32_t token_start,
                     std::uint32_t token_end,
                     ReportCoverageKind kind) {
  tree.coverage.push_back(
      ReportNodeCoverage{occurrence_id, token_start, token_end, kind});
}

void append_atom_occurrence(ReportTree& tree,
                            const ReportToken& token,
                            ReportNodeOccurrenceId parent_id,
                            std::uint32_t edge_order,
                            std::uint32_t token_start,
                            std::uint32_t repeat_iteration,
                            ReportNodeDefId atom_def_id) {
  const ReportNodeOccurrenceId occurrence_id = append_occurrence(
      tree, atom_def_id, parent_id, edge_order, token_start, token_start + 1,
      repeat_iteration);
  append_coverage(tree, occurrence_id, token_start, token_start + 1,
                  ReportCoverageKind::kAtomLeaf);
  (void)token;
}

const ReportToken& exemplar_for_symbol(const std::vector<ReportToken>& tokens,
                                       SymbolId symbol_id) {
  const auto found = std::find_if(
      tokens.begin(), tokens.end(), [symbol_id](const ReportToken& token) {
        return token.symbol_id == symbol_id;
      });
  if (found == tokens.end()) {
    throw std::invalid_argument("grammar references a missing token symbol");
  }
  return *found;
}

std::map<std::string, ReportMacroDefinition> macro_map(
    const ReportGrammarEvidence& grammar) {
  std::map<std::string, ReportMacroDefinition> out;
  for (const ReportMacroDefinition& macro : grammar.macros) {
    if (macro.macro_name.empty()) {
      throw std::invalid_argument("macro name is empty");
    }
    if (macro.body_symbols.empty()) {
      throw std::invalid_argument("macro body is empty");
    }
    const auto inserted = out.emplace(macro.macro_name, macro);
    if (!inserted.second) {
      throw std::invalid_argument("duplicate macro name");
    }
  }
  return out;
}

std::vector<SymbolId> expand_item_symbols(
    const ReportGrammarItem& item,
    const std::map<std::string, ReportMacroDefinition>& macros) {
  if (item.kind == ReportGrammarItemKind::kSymbol) {
    return {item.symbol_id};
  }
  const auto found = macros.find(item.macro_name);
  if (found == macros.end()) {
    throw std::invalid_argument("final sequence references unknown macro");
  }
  return found->second.body_symbols;
}

void ensure_symbols_match_tokens(const std::vector<ReportToken>& tokens,
                                 std::uint32_t cursor,
                                 const std::vector<SymbolId>& symbols) {
  if (cursor + symbols.size() > tokens.size()) {
    throw std::invalid_argument("grammar expansion exceeds token sequence");
  }
  for (std::size_t i = 0; i < symbols.size(); ++i) {
    if (tokens[cursor + i].symbol_id != symbols[i]) {
      throw std::invalid_argument("grammar expansion does not match tokens");
    }
  }
}

ReportNodeDefId append_atom_def_for_symbol(ReportTree& tree,
                                           const std::vector<ReportToken>& tokens,
                                           SymbolId symbol_id,
                                           std::uint32_t display_depth,
                                           std::uint32_t loop_depth,
                                           std::string reason) {
  const ReportToken& token = exemplar_for_symbol(tokens, symbol_id);
  return append_def(tree, ReportNodeKind::kAtom, token.display_op,
                    token.display_category, token.symbol_id, 0, display_depth,
                    loop_depth, std::move(reason));
}

struct GrammarReportLowering {
  const std::vector<ReportToken>& tokens;
  const GrammarSnapshot& snapshot;
  std::map<SymbolId::value_type, const MacroDefRow*> macro_by_symbol;
};

struct GrammarTemplateChild;

struct GrammarSubtreeTemplate {
  ReportNodeDefId def_id;
  SymbolId atom_symbol;
  std::uint32_t span_len = 0;
  bool transparent = false;
  std::vector<GrammarTemplateChild> children;
};

struct GrammarTemplateChild {
  GrammarSubtreeTemplate subtree;
  std::uint32_t span_len = 0;
};

std::uint32_t expanded_symbol_len(const GrammarReportLowering& lowering,
                                  SymbolId symbol,
                                  std::unordered_set<SymbolId::value_type>&
                                      visiting) {
  const auto found = lowering.macro_by_symbol.find(symbol.value());
  if (found == lowering.macro_by_symbol.end()) {
    return 1;
  }
  if (!visiting.insert(symbol.value()).second) {
    throw std::invalid_argument("grammar macro expansion cycle");
  }
  std::uint32_t out = 0;
  for (SymbolId rhs_symbol : found->second->rhs_symbols) {
    out += expanded_symbol_len(lowering, rhs_symbol, visiting);
  }
  visiting.erase(symbol.value());
  return out;
}

std::uint32_t expanded_symbol_len(const GrammarReportLowering& lowering,
                                  SymbolId symbol) {
  std::unordered_set<SymbolId::value_type> visiting;
  return expanded_symbol_len(lowering, symbol, visiting);
}

bool lp_macro_is_uniform(const MacroDefRow& macro) {
  if (macro.level != MacroLevel::kLP || macro.rhs_symbols.empty()) {
    return false;
  }
  for (SymbolId rhs_symbol : macro.rhs_symbols) {
    if (rhs_symbol != macro.rhs_symbols.front()) {
      return false;
    }
  }
  return true;
}

std::uint32_t append_grammar_symbol_tree(ReportTree& tree,
                                         const GrammarReportLowering& lowering,
                                         SymbolId symbol,
                                         ReportNodeOccurrenceId parent_id,
                                         std::uint32_t edge_order,
                                         std::uint32_t token_start,
                                         std::uint32_t token_end,
                                         std::uint32_t repeat_iteration,
                                         std::uint32_t display_depth,
                                         std::uint32_t loop_depth);

GrammarSubtreeTemplate build_grammar_template(ReportTree& tree,
                                              const GrammarReportLowering&
                                                  lowering,
                                              SymbolId symbol,
                                              std::uint32_t display_depth,
                                              std::uint32_t loop_depth);

std::uint32_t append_grammar_template_occurrence(
    ReportTree& tree,
    const GrammarReportLowering& lowering,
    const GrammarSubtreeTemplate& subtree,
    ReportNodeOccurrenceId parent_id,
    std::uint32_t edge_order,
    std::uint32_t token_start,
    std::uint32_t token_end,
    std::uint32_t repeat_iteration);

std::uint32_t append_grammar_atom(ReportTree& tree,
                                  const GrammarReportLowering& lowering,
                                  SymbolId symbol,
                                  ReportNodeOccurrenceId parent_id,
                                  std::uint32_t edge_order,
                                  std::uint32_t token_start,
                                  std::uint32_t repeat_iteration,
                                  std::uint32_t display_depth,
                                  std::uint32_t loop_depth) {
  if (token_start >= lowering.tokens.size()) {
    throw std::invalid_argument("grammar atom token span is out of range");
  }
  if (lowering.tokens[token_start].symbol_id != symbol) {
    throw std::invalid_argument("grammar atom does not match report token");
  }
  const ReportNodeDefId atom_def =
      append_atom_def_for_symbol(tree, lowering.tokens, symbol, display_depth,
                                 loop_depth, "grammar_state_atom");
  append_atom_occurrence(tree, lowering.tokens[token_start], parent_id,
                         edge_order, token_start, repeat_iteration, atom_def);
  return edge_order + 1;
}

std::uint32_t append_grammar_macro_inline(
    ReportTree& tree,
    const GrammarReportLowering& lowering,
    const MacroDefRow& macro,
    ReportNodeOccurrenceId parent_id,
    std::uint32_t edge_order,
    std::uint32_t token_start,
    std::uint32_t token_end,
    std::uint32_t repeat_iteration,
    std::uint32_t display_depth,
    std::uint32_t loop_depth) {
  std::uint32_t cursor = token_start;
  std::uint32_t child_order = edge_order;
  for (SymbolId rhs_symbol : macro.rhs_symbols) {
    const std::uint32_t span_len = expanded_symbol_len(lowering, rhs_symbol);
    child_order = append_grammar_symbol_tree(
        tree, lowering, rhs_symbol, parent_id, child_order, cursor,
        cursor + span_len, repeat_iteration, display_depth, loop_depth);
    cursor += span_len;
  }
  if (cursor != token_end) {
    throw std::invalid_argument("grammar macro inline did not consume its span");
  }
  return child_order;
}

std::uint32_t append_grammar_macro_repeat(ReportTree& tree,
                                          const GrammarReportLowering& lowering,
                                          const MacroDefRow& macro,
                                          ReportNodeOccurrenceId parent_id,
                                          std::uint32_t edge_order,
                                          std::uint32_t token_start,
                                          std::uint32_t token_end,
                                          std::uint32_t repeat_iteration,
                                          std::uint32_t display_depth,
                                          std::uint32_t loop_depth) {
  const SymbolId body_symbol = macro.rhs_symbols.front();
  const std::uint32_t body_len = expanded_symbol_len(lowering, body_symbol);
  const std::uint32_t repeat_count =
      static_cast<std::uint32_t>(macro.rhs_symbols.size());
  if (token_start + body_len * repeat_count != token_end) {
    throw std::invalid_argument("grammar repeat span length mismatch");
  }

  const ReportNodeDefId repeat_def = append_def(
      tree, ReportNodeKind::kRepeat, "Rep x" + std::to_string(repeat_count),
      "", SymbolId::invalid(), repeat_count, display_depth, loop_depth + 1,
      "grammar_state_macro_run");
  const ReportNodeOccurrenceId repeat_occurrence = append_occurrence(
      tree, repeat_def, parent_id, edge_order, token_start, token_end,
      repeat_iteration);
  append_coverage(tree, repeat_occurrence, token_start, token_end,
                  ReportCoverageKind::kDirectBody);

  const GrammarSubtreeTemplate body_template = build_grammar_template(
      tree, lowering, body_symbol, display_depth + 1, loop_depth + 1);
  std::uint32_t cursor = token_start;
  std::uint32_t child_order = 1;
  for (std::uint32_t index = 0; index < repeat_count; ++index) {
    child_order = append_grammar_template_occurrence(
        tree, lowering, body_template, repeat_occurrence, child_order, cursor,
        cursor + body_len, index + 1);
    cursor += body_len;
  }
  return edge_order + 1;
}

std::uint32_t append_grammar_symbol_run_repeat(
    ReportTree& tree,
    const GrammarReportLowering& lowering,
    SymbolId body_symbol,
    std::uint32_t repeat_count,
    ReportNodeOccurrenceId parent_id,
    std::uint32_t edge_order,
    std::uint32_t token_start,
    std::uint32_t token_end,
    std::uint32_t repeat_iteration,
    std::uint32_t display_depth,
    std::uint32_t loop_depth) {
  const std::uint32_t body_len = expanded_symbol_len(lowering, body_symbol);
  if (body_len == 0 ||
      token_start + body_len * repeat_count != token_end) {
    throw std::invalid_argument("grammar symbol run span length mismatch");
  }

  const ReportNodeDefId repeat_def = append_def(
      tree, ReportNodeKind::kRepeat, "Rep x" + std::to_string(repeat_count),
      "", SymbolId::invalid(), repeat_count, display_depth, loop_depth + 1,
      "grammar_state_adjacent_symbol_run");
  const ReportNodeOccurrenceId repeat_occurrence = append_occurrence(
      tree, repeat_def, parent_id, edge_order, token_start, token_end,
      repeat_iteration);
  append_coverage(tree, repeat_occurrence, token_start, token_end,
                  ReportCoverageKind::kDirectBody);

  const GrammarSubtreeTemplate body_template = build_grammar_template(
      tree, lowering, body_symbol, display_depth + 1, loop_depth + 1);
  std::uint32_t cursor = token_start;
  std::uint32_t child_order = 1;
  for (std::uint32_t index = 0; index < repeat_count; ++index) {
    child_order = append_grammar_template_occurrence(
        tree, lowering, body_template, repeat_occurrence, child_order, cursor,
        cursor + body_len, index + 1);
    cursor += body_len;
  }
  return edge_order + 1;
}

GrammarSubtreeTemplate build_grammar_template(ReportTree& tree,
                                              const GrammarReportLowering&
                                                  lowering,
                                              SymbolId symbol,
                                              std::uint32_t display_depth,
                                              std::uint32_t loop_depth) {
  const auto found = lowering.macro_by_symbol.find(symbol.value());
  if (found == lowering.macro_by_symbol.end()) {
    GrammarSubtreeTemplate subtree;
    subtree.def_id =
        append_atom_def_for_symbol(tree, lowering.tokens, symbol, display_depth,
                                   loop_depth, "grammar_state_repeat_body_atom");
    subtree.atom_symbol = symbol;
    subtree.span_len = 1;
    return subtree;
  }

  const MacroDefRow& macro = *found->second;
  GrammarSubtreeTemplate subtree;
  subtree.span_len = expanded_symbol_len(lowering, symbol);
  if (lp_macro_is_uniform(macro)) {
    const std::uint32_t repeat_count =
        static_cast<std::uint32_t>(macro.rhs_symbols.size());
    subtree.def_id = append_def(
        tree, ReportNodeKind::kRepeat, "Rep x" + std::to_string(repeat_count),
        "", SymbolId::invalid(), repeat_count, display_depth, loop_depth + 1,
        "grammar_state_repeat_body_macro_run");
    const SymbolId body_symbol = macro.rhs_symbols.front();
    GrammarTemplateChild child;
    child.span_len = expanded_symbol_len(lowering, body_symbol);
    child.subtree = build_grammar_template(tree, lowering, body_symbol,
                                           display_depth + 1, loop_depth + 1);
    subtree.children.push_back(std::move(child));
    return subtree;
  }

  subtree.transparent = true;
  for (SymbolId rhs_symbol : macro.rhs_symbols) {
    GrammarTemplateChild child;
    child.span_len = expanded_symbol_len(lowering, rhs_symbol);
    child.subtree = build_grammar_template(tree, lowering, rhs_symbol,
                                           display_depth, loop_depth);
    subtree.children.push_back(std::move(child));
  }
  return subtree;
}

std::uint32_t append_grammar_template_occurrence(
    ReportTree& tree,
    const GrammarReportLowering& lowering,
    const GrammarSubtreeTemplate& subtree,
    ReportNodeOccurrenceId parent_id,
    std::uint32_t edge_order,
    std::uint32_t token_start,
    std::uint32_t token_end,
    std::uint32_t repeat_iteration) {
  if (token_start + subtree.span_len != token_end) {
    throw std::invalid_argument("grammar template span length mismatch");
  }
  if (subtree.transparent) {
    std::uint32_t cursor = token_start;
    std::uint32_t child_order = edge_order;
    for (const GrammarTemplateChild& child : subtree.children) {
      child_order = append_grammar_template_occurrence(
          tree, lowering, child.subtree, parent_id, child_order, cursor,
          cursor + child.span_len, repeat_iteration);
      cursor += child.span_len;
    }
    if (cursor != token_end) {
      throw std::invalid_argument(
          "grammar transparent template did not consume span");
    }
    return child_order;
  }

  const ReportNodeDef& def = node_def(tree, subtree.def_id);
  if (def.kind == ReportNodeKind::kAtom) {
    if (token_start >= lowering.tokens.size()) {
      throw std::invalid_argument("grammar template atom span is out of range");
    }
    if (lowering.tokens[token_start].symbol_id != subtree.atom_symbol) {
      throw std::invalid_argument("grammar template atom does not match token");
    }
    append_atom_occurrence(tree, lowering.tokens[token_start], parent_id,
                           edge_order, token_start, repeat_iteration,
                           subtree.def_id);
    return edge_order + 1;
  }

  const ReportNodeOccurrenceId occurrence = append_occurrence(
      tree, subtree.def_id, parent_id, edge_order, token_start, token_end,
      repeat_iteration);
  append_coverage(tree, occurrence, token_start, token_end,
                  ReportCoverageKind::kDirectBody);

  if (def.kind == ReportNodeKind::kRepeat) {
    if (subtree.children.size() != 1) {
      throw std::invalid_argument("grammar repeat template must have one body");
    }
    const GrammarTemplateChild& body = subtree.children.front();
    if (body.span_len == 0 || def.repeat_count == 0 ||
        token_start + body.span_len * def.repeat_count != token_end) {
      throw std::invalid_argument("grammar repeat template length mismatch");
    }
    std::uint32_t cursor = token_start;
    std::uint32_t child_order = 1;
    for (std::uint32_t index = 0; index < def.repeat_count; ++index) {
      child_order = append_grammar_template_occurrence(
          tree, lowering, body.subtree, occurrence, child_order, cursor,
          cursor + body.span_len, index + 1);
      cursor += body.span_len;
    }
    return edge_order + 1;
  }

  std::uint32_t cursor = token_start;
  std::uint32_t child_order = 1;
  for (const GrammarTemplateChild& child : subtree.children) {
    child_order = append_grammar_template_occurrence(
        tree, lowering, child.subtree, occurrence, child_order, cursor,
        cursor + child.span_len, repeat_iteration);
    cursor += child.span_len;
  }
  if (cursor != token_end) {
    throw std::invalid_argument("grammar seq template did not consume span");
  }
  return edge_order + 1;
}

std::uint32_t append_grammar_symbol_tree(ReportTree& tree,
                                         const GrammarReportLowering& lowering,
                                         SymbolId symbol,
                                         ReportNodeOccurrenceId parent_id,
                                         std::uint32_t edge_order,
                                         std::uint32_t token_start,
                                         std::uint32_t token_end,
                                         std::uint32_t repeat_iteration,
                                         std::uint32_t display_depth,
                                         std::uint32_t loop_depth) {
  const auto found = lowering.macro_by_symbol.find(symbol.value());
  if (found == lowering.macro_by_symbol.end()) {
    if (token_end != token_start + 1) {
      throw std::invalid_argument("grammar atom span length mismatch");
    }
    return append_grammar_atom(tree, lowering, symbol, parent_id, edge_order,
                               token_start, repeat_iteration, display_depth,
                               loop_depth);
  }

  const MacroDefRow& macro = *found->second;
  if (lp_macro_is_uniform(macro)) {
    return append_grammar_macro_repeat(tree, lowering, macro, parent_id,
                                       edge_order, token_start, token_end,
                                       repeat_iteration, display_depth,
                                       loop_depth);
  }
  return append_grammar_macro_inline(tree, lowering, macro, parent_id,
                                     edge_order, token_start, token_end,
                                     repeat_iteration, display_depth,
                                     loop_depth);
}

}  // namespace

ReportGrammarItem ReportGrammarItem::symbol(SymbolId symbol_id) {
  ReportGrammarItem item;
  item.kind = ReportGrammarItemKind::kSymbol;
  item.symbol_id = symbol_id;
  return item;
}

ReportGrammarItem ReportGrammarItem::macro(std::string macro_name) {
  ReportGrammarItem item;
  item.kind = ReportGrammarItemKind::kMacro;
  item.macro_name = std::move(macro_name);
  return item;
}

ReportTree build_report_tree_from_tokens(const std::vector<ReportToken>& tokens,
                                         ReportTreeBuildConfig config) {
  if (config.min_run_length == 0) {
    throw std::invalid_argument("ReportTreeBuildConfig min_run_length is zero");
  }
  validate_tokens_for_report_anchors(tokens);

  ReportTree tree;
  const ReportNodeDefId root_def =
      append_def(tree, ReportNodeKind::kSeq, "Seq", "", SymbolId::invalid(), 0,
                 0, 0, "root");
  const ReportNodeOccurrenceId root_occurrence = append_occurrence(
      tree, root_def, ReportNodeOccurrenceId::invalid(), 0, 0,
      static_cast<std::uint32_t>(tokens.size()), 0);
  append_coverage(tree, root_occurrence, 0,
                  static_cast<std::uint32_t>(tokens.size()),
                  ReportCoverageKind::kDirectBody);

  std::uint32_t edge_order = 1;
  for (std::size_t i = 0; i < tokens.size();) {
    std::size_t run_end = i + 1;
    while (run_end < tokens.size() &&
           same_visible_token(tokens[i], tokens[run_end])) {
      ++run_end;
    }
    const std::uint32_t run_len = static_cast<std::uint32_t>(run_end - i);
    const std::uint32_t start = static_cast<std::uint32_t>(i);
    const std::uint32_t end = static_cast<std::uint32_t>(run_end);

    if (config.fold_adjacent_runs && run_len >= config.min_run_length) {
      const ReportNodeDefId repeat_def = append_def(
          tree, ReportNodeKind::kRepeat, "Rep x" + std::to_string(run_len), "",
          SymbolId::invalid(), run_len, 1, 1, "adjacent_same_symbol_run");
      const ReportNodeOccurrenceId repeat_occurrence = append_occurrence(
          tree, repeat_def, root_occurrence, edge_order++, start, end, 0);
      append_coverage(tree, repeat_occurrence, start, end,
                      ReportCoverageKind::kDirectBody);

      const ReportNodeDefId atom_def = append_def(
          tree, ReportNodeKind::kAtom, tokens[i].display_op,
          tokens[i].display_category, tokens[i].symbol_id, 0, 2, 1,
          "repeat_body_atom");
      for (std::uint32_t offset = 0; offset < run_len; ++offset) {
        append_atom_occurrence(tree, tokens[i + offset], repeat_occurrence,
                               offset + 1, start + offset, offset + 1,
                               atom_def);
      }
    } else {
      for (std::size_t j = i; j < run_end; ++j) {
        const ReportNodeDefId atom_def = append_def(
            tree, ReportNodeKind::kAtom, tokens[j].display_op,
            tokens[j].display_category, tokens[j].symbol_id, 0, 1, 0,
            "atom");
        append_atom_occurrence(tree, tokens[j], root_occurrence, edge_order++,
                               static_cast<std::uint32_t>(j), 0, atom_def);
      }
    }
    i = run_end;
  }

  validate_report_tree_or_throw(tree, static_cast<std::uint32_t>(tokens.size()));
  return tree;
}

ReportTree build_report_tree_from_grammar(const std::vector<ReportToken>& tokens,
                                          const ReportGrammarEvidence& grammar,
                                          ReportTreeBuildConfig config) {
  ReportGraphReplayEvidence graph;
  return build_report_tree_from_grammar(tokens, grammar, graph, config);
}

ReportTree build_report_tree_from_grammar(
    const std::vector<ReportToken>& tokens,
    const ReportGrammarEvidence& grammar,
    const ReportGraphReplayEvidence& graph,
    ReportTreeBuildConfig config) {
  if (graph_tiling_blocks_materialization(graph)) {
    ReportTree tree;
    tree.diagnostics.push_back(Diagnostic{
        DiagnosticSeverity::kError, graph_tiling_diagnostic_code(graph),
        "graph replay tiling is not exact; report tree was not materialized"});
    return tree;
  }
  validate_tokens_for_report_anchors(tokens);
  if (grammar.final_sequence.empty()) {
    return build_report_tree_from_tokens(tokens, config);
  }

  const std::map<std::string, ReportMacroDefinition> macros = macro_map(grammar);
  ReportTree tree;
  const ReportNodeDefId root_def =
      append_def(tree, ReportNodeKind::kSeq, "Seq", "", SymbolId::invalid(), 0,
                 0, 0, "root");
  const ReportNodeOccurrenceId root_occurrence = append_occurrence(
      tree, root_def, ReportNodeOccurrenceId::invalid(), 0, 0,
      static_cast<std::uint32_t>(tokens.size()), 0);
  append_coverage(tree, root_occurrence, 0,
                  static_cast<std::uint32_t>(tokens.size()),
                  ReportCoverageKind::kDirectBody);

  std::uint32_t cursor = 0;
  std::uint32_t edge_order = 1;
  for (std::size_t i = 0; i < grammar.final_sequence.size();) {
    const ReportGrammarItem& item = grammar.final_sequence[i];
    if (item.kind == ReportGrammarItemKind::kMacro) {
      const auto found = macros.find(item.macro_name);
      if (found == macros.end()) {
        throw std::invalid_argument("final sequence references unknown macro");
      }
      const ReportMacroDefinition& macro = found->second;
      std::size_t run_end = i + 1;
      while (run_end < grammar.final_sequence.size() &&
             grammar.final_sequence[run_end].kind ==
                 ReportGrammarItemKind::kMacro &&
             grammar.final_sequence[run_end].macro_name == item.macro_name) {
        ++run_end;
      }
      const std::uint32_t run_len = static_cast<std::uint32_t>(run_end - i);
      const std::uint32_t body_len =
          static_cast<std::uint32_t>(macro.body_symbols.size());
      const std::uint32_t start = cursor;
      const std::uint32_t end = cursor + run_len * body_len;
      for (std::uint32_t repeat_idx = 0; repeat_idx < run_len; ++repeat_idx) {
        ensure_symbols_match_tokens(tokens, cursor, macro.body_symbols);
        cursor += body_len;
      }

      if (macro.visibility == ReportMacroVisibility::kKeepRepeat &&
          run_len >= config.min_run_length) {
        const ReportNodeDefId repeat_def = append_def(
            tree, ReportNodeKind::kRepeat, "Rep x" + std::to_string(run_len),
            "", SymbolId::invalid(), run_len, 1, 1, "macro_run");
        const ReportNodeOccurrenceId repeat_occurrence = append_occurrence(
            tree, repeat_def, root_occurrence, edge_order++, start, end, 0);
        append_coverage(tree, repeat_occurrence, start, end,
                        ReportCoverageKind::kDirectBody);

        const ReportNodeDefId body_def =
            append_def(tree, ReportNodeKind::kSeq, "Seq", "",
                       SymbolId::invalid(), 0, 2, 1, "macro_body");
        std::vector<ReportNodeDefId> atom_defs;
        atom_defs.reserve(macro.body_symbols.size());
        for (SymbolId symbol_id : macro.body_symbols) {
          atom_defs.push_back(append_atom_def_for_symbol(
              tree, tokens, symbol_id, 3, 1, "macro_body_atom"));
        }

        std::uint32_t body_start = start;
        for (std::uint32_t repeat_idx = 0; repeat_idx < run_len; ++repeat_idx) {
          const ReportNodeOccurrenceId body_occurrence = append_occurrence(
              tree, body_def, repeat_occurrence, repeat_idx + 1, body_start,
              body_start + body_len, repeat_idx + 1);
          append_coverage(tree, body_occurrence, body_start,
                          body_start + body_len,
                          ReportCoverageKind::kDirectBody);
          for (std::uint32_t body_offset = 0; body_offset < body_len;
               ++body_offset) {
            append_atom_occurrence(tree, tokens[body_start + body_offset],
                                   body_occurrence, body_offset + 1,
                                   body_start + body_offset, repeat_idx + 1,
                                   atom_defs[body_offset]);
          }
          body_start += body_len;
        }
      } else {
        if (run_len == 1) {
          tree.diagnostics.push_back(Diagnostic{
              DiagnosticSeverity::kWarning,
              "macro_inlined_single_visible_reference",
              "macro was inlined because it has one visible reference"});
        }
        for (std::uint32_t repeat_idx = 0; repeat_idx < run_len; ++repeat_idx) {
          const std::uint32_t inline_start = start + repeat_idx * body_len;
          for (std::uint32_t body_offset = 0; body_offset < body_len;
               ++body_offset) {
            const ReportToken& token = tokens[inline_start + body_offset];
            const ReportNodeDefId atom_def = append_def(
                tree, ReportNodeKind::kAtom, token.display_op,
                token.display_category, token.symbol_id, 0, 1, 0,
                "inlined_macro_atom");
            append_atom_occurrence(tree, token, root_occurrence, edge_order++,
                                   inline_start + body_offset, 0, atom_def);
          }
        }
      }

      i = run_end;
      continue;
    }

    const std::vector<SymbolId> item_symbols = expand_item_symbols(item, macros);
    ensure_symbols_match_tokens(tokens, cursor, item_symbols);
    const ReportToken& token = tokens[cursor];
    const ReportNodeDefId atom_def = append_def(
        tree, ReportNodeKind::kAtom, token.display_op, token.display_category,
        token.symbol_id, 0, 1, 0, "grammar_symbol");
    append_atom_occurrence(tree, token, root_occurrence, edge_order++, cursor, 0,
                           atom_def);
    ++cursor;
    ++i;
  }

  if (cursor != tokens.size()) {
    throw std::invalid_argument("grammar expansion did not consume all tokens");
  }
  validate_report_tree_or_throw(tree, static_cast<std::uint32_t>(tokens.size()));
  return tree;
}

ReportTree build_report_tree_from_grammar_state(
    const std::vector<ReportToken>& tokens,
    const GlobalGrammarState& state,
    ReportTreeBuildConfig config) {
  if (config.min_run_length == 0) {
    throw std::invalid_argument("ReportTreeBuildConfig min_run_length is zero");
  }
  validate_tokens_for_report_anchors(tokens);
  if (state.stage != GrammarStage::kDone || state.live_node_count == 0) {
    return build_report_tree_from_tokens(tokens, config);
  }

  const GrammarSnapshot snapshot = freeze_grammar_snapshot(state);
  if (snapshot.empty() || snapshot.macro_defs.empty()) {
    return build_report_tree_from_tokens(tokens, config);
  }

  GrammarReportLowering lowering{tokens, snapshot, {}};
  for (const MacroDefRow& macro : snapshot.macro_defs) {
    if (!macro.symbol_id.valid()) {
      throw std::invalid_argument("grammar macro has invalid symbol");
    }
    const auto inserted =
        lowering.macro_by_symbol.emplace(macro.symbol_id.value(), &macro);
    if (!inserted.second) {
      throw std::invalid_argument("duplicate grammar macro symbol");
    }
  }

  ReportTree tree;
  const ReportNodeDefId root_def =
      append_def(tree, ReportNodeKind::kSeq, "Seq", "", SymbolId::invalid(), 0,
                 0, 0, "grammar_state_root");
  const ReportNodeOccurrenceId root_occurrence = append_occurrence(
      tree, root_def, ReportNodeOccurrenceId::invalid(), 0, 0,
      static_cast<std::uint32_t>(tokens.size()), 0);
  append_coverage(tree, root_occurrence, 0,
                  static_cast<std::uint32_t>(tokens.size()),
                  ReportCoverageKind::kDirectBody);

  std::uint32_t edge_order = 1;
  for (std::size_t index = 0; index < snapshot.nodes.size();) {
    const GrammarSnapshotNode& node = snapshot.nodes[index];
    if (node.source_begin_token_index > node.source_end_token_index_exclusive ||
        node.source_end_token_index_exclusive > tokens.size()) {
      throw std::invalid_argument("grammar node source span is out of range");
    }

    std::size_t run_end = index + 1;
    while (config.fold_adjacent_runs && run_end < snapshot.nodes.size()) {
      const GrammarSnapshotNode& next = snapshot.nodes[run_end];
      if (next.source_begin_token_index >
              next.source_end_token_index_exclusive ||
          next.source_end_token_index_exclusive > tokens.size()) {
        throw std::invalid_argument("grammar node source span is out of range");
      }
      const GrammarSnapshotNode& previous = snapshot.nodes[run_end - 1];
      if (next.symbol_id != node.symbol_id ||
          previous.source_end_token_index_exclusive !=
              next.source_begin_token_index) {
        break;
      }
      ++run_end;
    }

    const std::uint32_t run_len =
        static_cast<std::uint32_t>(run_end - index);
    if (config.fold_adjacent_runs && run_len >= config.min_run_length) {
      const GrammarSnapshotNode& last = snapshot.nodes[run_end - 1];
      edge_order = append_grammar_symbol_run_repeat(
          tree, lowering, node.symbol_id, run_len, root_occurrence,
          edge_order, static_cast<std::uint32_t>(node.source_begin_token_index),
          static_cast<std::uint32_t>(last.source_end_token_index_exclusive), 0,
          1, 0);
      index = run_end;
      continue;
    }

    edge_order = append_grammar_symbol_tree(
        tree, lowering, node.symbol_id, root_occurrence, edge_order,
        static_cast<std::uint32_t>(node.source_begin_token_index),
        static_cast<std::uint32_t>(node.source_end_token_index_exclusive), 0,
        1, 0);
    ++index;
  }

  validate_report_tree_or_throw(tree, static_cast<std::uint32_t>(tokens.size()));
  return tree;
}

void validate_report_tree_or_throw(const ReportTree& tree,
                                   std::uint32_t token_count) {
  if (tree.node_defs.empty()) {
    throw std::invalid_argument("report tree has no node defs");
  }
  if (tree.occurrences.empty()) {
    throw std::invalid_argument("report tree has no occurrences");
  }

  std::size_t root_count = 0;
  for (const ReportNodeOccurrence& occurrence : tree.occurrences) {
    if (!occurrence.id.valid() || occurrence.id.value() >= tree.occurrences.size()) {
      throw std::invalid_argument("occurrence id out of range");
    }
    if (!occurrence.node_def_id.valid() ||
        occurrence.node_def_id.value() >= tree.node_defs.size()) {
      throw std::invalid_argument("occurrence node_def_id out of range");
    }
    if (occurrence.token_start_ordinal > occurrence.token_end_ordinal ||
        occurrence.token_end_ordinal > token_count) {
      throw std::invalid_argument("occurrence span out of range");
    }
    if (!occurrence.parent_occurrence_id.valid()) {
      ++root_count;
      continue;
    }
    const ReportNodeOccurrence& parent =
        node_occurrence(tree, occurrence.parent_occurrence_id);
    if (occurrence.token_start_ordinal < parent.token_start_ordinal ||
        occurrence.token_end_ordinal > parent.token_end_ordinal) {
      throw std::invalid_argument("child span outside parent");
    }
  }
  if (root_count != 1) {
    throw std::invalid_argument("report tree must have exactly one root");
  }

  std::unordered_set<std::uint64_t> edge_keys;
  for (const ReportTreeEdge& edge : tree.edges) {
    const ReportNodeOccurrence& parent =
        node_occurrence(tree, edge.parent_occurrence_id);
    const ReportNodeOccurrence& child =
        node_occurrence(tree, edge.child_occurrence_id);
    if (child.parent_occurrence_id != parent.id) {
      throw std::invalid_argument("edge disagrees with child parent id");
    }
    const std::uint64_t key =
        (static_cast<std::uint64_t>(parent.id.value()) << 32u) |
        static_cast<std::uint64_t>(edge.edge_order);
    if (!edge_keys.insert(key).second) {
      throw std::invalid_argument("duplicate sibling edge order");
    }
  }

  for (const ReportNodeCoverage& row : tree.coverage) {
    if (!row.node_occurrence_id.valid() ||
        row.node_occurrence_id.value() >= tree.occurrences.size()) {
      throw std::invalid_argument("coverage occurrence id out of range");
    }
    if (row.token_start_ordinal > row.token_end_ordinal ||
        row.token_end_ordinal > token_count) {
      throw std::invalid_argument("coverage span out of range");
    }
  }
}

}  // namespace traceloom
