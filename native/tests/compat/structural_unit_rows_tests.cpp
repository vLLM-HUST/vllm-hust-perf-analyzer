#include "traceloom/compat/structural_unit_rows.h"
#include "traceloom/pattern/grammar_state.h"
#include "traceloom/report/report_tree_builder.h"
#include "traceloom/testing/test_util.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

traceloom::ReportToken token(std::uint32_t ordinal,
                             traceloom::SymbolId symbol,
                             const char* op) {
  traceloom::ReportToken out;
  out.ordinal = ordinal;
  out.device_id = 0;
  out.symbol_id = symbol;
  out.display_op = op;
  out.display_category = "exec";
  out.anchor_kind = traceloom::ReportAnchorKind::kExec;
  out.anchor_id = traceloom::AnchorId(ordinal);
  out.start_ns = static_cast<std::int64_t>(ordinal) * 10;
  out.end_ns = out.start_ns + 5;
  return out;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  const SymbolId prefix(1);
  const SymbolId graph_body(2);
  const SymbolId x(3);
  const SymbolId y(4);
  const SymbolId suffix(5);
  const SymbolId graph_macro(100);
  const SymbolId second_graph_macro(101);

  const std::vector<ReportToken> tokens{
      token(0, prefix, "Prefix"), token(1, graph_body, "GraphBody"),
      token(2, graph_body, "GraphBody"), token(3, x, "X"),
      token(4, x, "X"), token(5, graph_body, "GraphBody"),
      token(6, y, "Y"), token(7, graph_body, "GraphBody"),
      token(8, suffix, "Suffix"),
  };

  GlobalGrammarState state;
  state.stage = GrammarStage::kDone;
  state.macro_defs = {
      MacroDefRow{MacroDefId(0), graph_macro, MacroLevel::kSemantic,
                  {graph_body}, 1, 3, 0, 1, "ReplayUnit T1"},
      MacroDefRow{MacroDefId(1), second_graph_macro, MacroLevel::kSemantic,
                  {graph_body}, 1, 1, 0, 1, "ReplayUnit T2"}};
  state.nodes.reserve(tokens.size());
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    const bool graph = index == 1 || index == 2 || index == 5 || index == 7;
    const bool second_graph = index == 7;
    state.nodes.push_back(GrammarNode{
        GrammarNodeId(static_cast<GrammarNodeId::value_type>(index)),
        graph ? (second_graph ? second_graph_macro : graph_macro)
              : tokens[index].symbol_id,
        graph ? (second_graph ? MacroDefId(1) : MacroDefId(0))
              : MacroDefId::invalid(),
        index, index + 1,
        tokens[index].start_ns, tokens[index].end_ns, GrammarChunkId(0),
        index == 0
            ? GrammarNodeId::invalid()
            : GrammarNodeId(
                  static_cast<GrammarNodeId::value_type>(index - 1)),
        index + 1 == tokens.size()
            ? GrammarNodeId::invalid()
            : GrammarNodeId(
                  static_cast<GrammarNodeId::value_type>(index + 1)),
        true});
  }
  state.chunks = {GrammarChunk{
      GrammarChunkId(0), 0, 0, GrammarNodeId(0),
      GrammarNodeId(static_cast<GrammarNodeId::value_type>(tokens.size() - 1)),
      tokens.size(), 0}};
  state.live_node_count = tokens.size();

  const ReportTree tree = build_report_tree_from_grammar_state(tokens, state);
  const compat::StructuralUnitSqlRows rows =
      compat::build_structural_unit_sql_rows(tree, tokens, 7);

  require(rows.units.size() == 7, "ordered structural row count");
  require(rows.unit_anchors.size() == tokens.size(),
          "every productive anchor has one unit membership");

  require(rows.units[0].kind == "unrecognized");
  require(rows.units[0].evidence_status == "unrecognized_open_prefix");
  require(rows.units[0].boundary_policy == "open_trace_prefix");
  require(rows.units[0].token_start_ordinal == 0 &&
              rows.units[0].token_end_ordinal == 1,
          "open prefix span");

  require(rows.units[1].kind == "graph_unit");
  require(rows.units[1].evidence_status == "exact");
  require(rows.units[1].run_count == 2, "adjacent exact graph run");
  require(rows.units[1].token_start_ordinal == 1 &&
              rows.units[1].token_end_ordinal == 3,
          "folded graph span");

  require(rows.units[2].kind == "structural_unit");
  require(rows.units[2].evidence_status == "complete");
  require(rows.units[2].boundary_policy ==
              "bounded_by_adjacent_exact_graph_units",
          "bounded productive unit policy");
  require(rows.units[2].anchor_count == 2);
  require(rows.units[2].token_start_ordinal == 3 &&
              rows.units[2].token_end_ordinal == 5,
          "first productive interstitial span");

  require(rows.units[4].kind == "structural_unit");
  require(rows.units[4].anchor_count == 1);
  require(rows.units[6].kind == "unrecognized");
  require(rows.units[6].evidence_status == "unrecognized_open_suffix");
  require(rows.units[6].boundary_policy == "open_trace_suffix");

  require(rows.units[1].family_id == rows.units[3].family_id,
          "same exact graph identity has one family");
  require(rows.units[3].family_id != rows.units[5].family_id,
          "different exact graph identities remain distinct");
  require(rows.units[1].body_fingerprint ==
              rows.units[3].body_fingerprint,
          "same graph body has stable fingerprint");
  require(rows.units[3].body_fingerprint !=
              rows.units[5].body_fingerprint,
          "graph fingerprint includes structural identity");
  require(!rows.units[2].expansion_nodes.empty(),
          "structural unit links to Loop Tree expansion");

  for (const compat::StructuralUnitSqlRow& row : rows.units) {
    require(row.db_idx == 7);
    require(row.unit_id.find("decode") == std::string::npos);
    require(row.unit_id.find("prefill") == std::string::npos);
    require(row.family_id.find("decode") == std::string::npos);
    require(row.family_id.find("prefill") == std::string::npos);
  }

  std::vector<std::uint32_t> membership(tokens.size(), 0);
  for (const compat::StructuralUnitAnchorSqlRow& member : rows.unit_anchors) {
    require(member.db_idx == 7);
    require(member.anchor_order < tokens.size());
    const std::size_t dash = member.anchor_id.find('-');
    require(dash != std::string::npos);
    const std::uint32_t ordinal =
        static_cast<std::uint32_t>(std::stoul(member.anchor_id.substr(dash + 1)) - 1);
    ++membership[ordinal];
  }
  for (std::uint32_t count : membership) {
    require(count == 1, "anchor belongs to exactly one structural unit");
  }

  return 0;
}
