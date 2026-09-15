#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/pattern/grammar_commit_plan.h"
#include "traceloom/pattern/grammar_snapshot.h"
#include "traceloom/testing/test_util.h"
#include <filesystem>
#include <fstream>
using namespace traceloom;
using traceloom::testing::require;
NativeIr input(std::initializer_list<const char*> names) {
  NativeIr ir;
  for(auto name:names) {auto i=ir.tokens.size();ir.tokens.append(AnchorId(i),ir.symbols.intern(name),0,i,i*10,i*10+10);}
  return ir;
}
int main() {
  GrammarStateConfig config;
  config.match_rules.ordered_markers.push_back({"hc","HcPre","HcPost"});
  for(auto names:{std::initializer_list<const char*>{"HcPre","A"}, {"A","HcPost"}, {"HcPre","HcPost"}, {"A","B"}}) {
    auto ir=input(names);auto state=build_initial_grammar_state(ir,config);
    require(macro_match_allowed(freeze_grammar_snapshot(state),0,2));
  }
  auto ir=input({"HcPost","HcPre","HcPost","HcPre","HcPost","HcPre","HcPost","HcPre"});
  auto state=build_initial_grammar_state(ir,config);
  require(!macro_match_allowed(freeze_grammar_snapshot(state),0,2));
  // Candidate filtering must precede frequency selection, not abort the engine.
  auto plain=build_initial_grammar_state(ir);
  auto round=run_pair_grammar_readonly_round(plain);
  auto forged=build_pair_grammar_commit_plan(freeze_grammar_snapshot(state),round.action);
  require(!forged.valid());
  require(forged.diagnostics.front().code==GrammarCommitDiagnosticCode::kMatchRuleViolation);
  auto result=run_grammar_state_machine(state);require(result.ok());
  auto nested=build_initial_grammar_state(ir,config);
  const auto post=ir.tokens.row(TokenId(0)).symbol_id,pre=ir.tokens.row(TokenId(1)).symbol_id;
  nested.macro_defs.push_back({MacroDefId(0),SymbolId(1000),MacroLevel::kRP,{pre,SymbolId(999)},2,1,0,0,""});
  nested.nodes[1].symbol_id=SymbolId(1000);nested.nodes[1].macro_def_id=MacroDefId(0);
  require(!macro_match_allowed(freeze_grammar_snapshot(nested),0,2));
  // A complete pair is sealed; higher macros may combine it with other units.
  nested.macro_defs[0].rhs_symbols={pre,post};
  require(macro_match_allowed(freeze_grammar_snapshot(nested),0,2));
  nested.nodes[0].symbol_id=SymbolId(1000);nested.nodes[0].macro_def_id=MacroDefId(0);
  require(macro_match_allowed(freeze_grammar_snapshot(nested),0,2));

  GrammarStateConfig suffix;
  suffix.match_rules.suffix_markers.push_back({"end", "S"});
  for (auto names : {std::initializer_list<const char*>{"A","B"}, {"A","S"}, {"S","S"}, {"A","S","S"}}) {
    auto state=build_initial_grammar_state(input(names),suffix);
    require(macro_match_allowed(freeze_grammar_snapshot(state),0,names.size()));
  }
  for (auto names : {std::initializer_list<const char*>{"S","A"}, {"A","S","B"}}) {
    auto state=build_initial_grammar_state(input(names),suffix);
    require(!macro_match_allowed(freeze_grammar_snapshot(state),0,names.size()));
  }
  auto suffix_ir=input({"A","S","A","S"});
  auto suffix_state=build_initial_grammar_state(suffix_ir,suffix);
  const auto a=suffix_ir.tokens.row(TokenId(0)).symbol_id;
  const auto end=suffix_ir.tokens.row(TokenId(1)).symbol_id;
  suffix_state.macro_defs.push_back({MacroDefId(0),SymbolId(1000),MacroLevel::kRP,{a,end},2,2,0,0,""});
  suffix_state.nodes[0].symbol_id=SymbolId(1000);
  suffix_state.nodes[1].symbol_id=SymbolId(1000);
  require(!macro_match_allowed(freeze_grammar_snapshot(suffix_state),0,2));
  // Repeating marker-bearing mixed macros must be filtered by both run producers.
  suffix_state.nodes[0].macro_def_id=MacroDefId(0);
  suffix_state.nodes[1].macro_def_id=MacroDefId(0);
  require(run_adjacent_run_readonly_round(suffix_state).status==GrammarRoundStatus::kStop);
  require(run_native_macro_run_readonly_round(suffix_state).status==GrammarRoundStatus::kStop);
  auto plain_suffix=build_initial_grammar_state(input({"S","A","S","A"}));
  auto pair=run_pair_grammar_readonly_round(plain_suffix);
  auto guarded=build_initial_grammar_state(input({"S","A","S","A"}),suffix);
  require(!build_pair_grammar_commit_plan(freeze_grammar_snapshot(guarded),pair.action).valid());
  // Two occurrences now suffice; a single occurrence is never promoted.
  auto twice=build_initial_grammar_state(input({"A","B","A","B"}));
  auto two=run_pair_grammar_readonly_round(twice);
  require(two.status==GrammarRoundStatus::kActionSelected && two.action.replace_count==2);
  require(two.action.gain==0); // retained legacy estimate, not an acceptance gate
  require(run_grammar_state_machine(twice).ok());
  require(twice.live_node_count==2);
  require(run_pair_grammar_readonly_round(build_initial_grammar_state(input({"A","B"}))).status==GrammarRoundStatus::kStop);

  const auto path=std::filesystem::temp_directory_path()/"traceloom-match-rules-test.yaml";
  const std::string good="schema: traceloom-match-rules-v1\nordered_markers:\n - id: hc\n   before: 'HcPre' # comment\n   after: HcPost\n";
  {std::ofstream out(path);out<<good;}
  auto loaded=load_macro_match_rules(path.string());
  require(loaded.ordered_markers.size()==1 && loaded.ordered_markers[0].before=="HcPre");
  require(loaded.source_yaml==good);
  for(const auto& bad:std::vector<std::string>{good+"typo: true\n",good+"schema: again\n",good+"---\nschema: x\n", "schema: unknown\nordered_markers: []\n", "schema: traceloom-match-rules-v1\nordered_markers: [{id: x, before: A, after: A}]\n"}) {
    {std::ofstream out(path);out<<bad;}
    bool failed=false;try{(void)load_macro_match_rules(path.string());}catch(const std::invalid_argument&){failed=true;}require(failed);
  }
  {std::ofstream out(path);out<<"schema: traceloom-match-rules-v1\nsuffix_markers: [{id: end, marker: S}]\n";}
  require(load_macro_match_rules(path.string()).suffix_markers.size()==1);
  std::filesystem::remove(path);
}
