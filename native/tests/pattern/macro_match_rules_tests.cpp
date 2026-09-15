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
  std::filesystem::remove(path);
}
