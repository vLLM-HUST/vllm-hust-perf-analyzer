#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/analysis/structural_occurrence_builder.h"
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
std::vector<StructuralProjectionToken> projection(const NativeIr& ir) {
  std::vector<StructuralProjectionToken> out;
  for (const auto& token : ir.tokens.rows()) {
    StructuralProjectionToken p;
    p.ordinal = out.size();
    p.symbol_id = token.symbol_id;
    p.display_op = ir.symbols.value(token.symbol_id);
    p.anchor_id = token.anchor_id;
    p.anchor_kind = StructuralAnchorKind::kExec;
    p.start_ns = token.start_ns;
    p.end_ns = token.end_ns;
    out.push_back(p);
  }
  return out;
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


  // Partition identities gate occurrences, never the shared definition key.
  GrammarStateConfig partition;
  partition.match_rules.partition_by = "scheduler_step";
  partition.token_partitions = {"s0","s0","s1","s1"};
  auto partitioned = build_initial_grammar_state(input({"A","B","A","B"}), partition);
  require(!macro_match_allowed(freeze_grammar_snapshot(partitioned),1,3));
  require(run_grammar_state_machine(partitioned).ok());
  require(partitioned.live_node_count==2);
  auto frozen = freeze_grammar_snapshot(partitioned);
  require(frozen.nodes[0].symbol_id==frozen.nodes[1].symbol_id);
  require(!macro_match_allowed(frozen,0,2)); // nested macro cannot cross either
  partition.token_partitions = {"s0","s0","s0","s1","s1","s1"};
  auto runs = build_initial_grammar_state(input({"A","A","A","A","A","A"}), partition);
  require(run_grammar_state_machine(runs).ok());
  require(runs.live_node_count==2); // retain both legal local runs
  auto unguarded = build_initial_grammar_state(input({"A","A","A","A","A","A"}));
  auto illegal = run_adjacent_run_readonly_round(unguarded);
  auto guarded_runs = build_initial_grammar_state(input({"A","A","A","A","A","A"}), partition);
  require(!build_adjacent_run_commit_plan(freeze_grammar_snapshot(guarded_runs),illegal.action).valid());
  partition.token_partitions = {"s0","s0","","s0","s0"};
  auto unknown = build_initial_grammar_state(input({"A","A","A","A","A"}), partition);
  require(run_grammar_state_machine(unknown).ok());
  require(unknown.live_node_count==3); // unknown cannot bridge same step on both sides
  for(const auto& node : freeze_grammar_snapshot(unknown).nodes)
    require(node.source_end_token_index_exclusive-node.source_begin_token_index<=2);


  // Rendering/lowering must not re-fold legal instances across the partition,
  // even if their symbols match. Empty-grammar fallback is also bounded.
  auto repeated_ir = input({"A","B","A","B"});
  partition.token_partitions = {"s0","s0","s1","s1"};
  auto repeated_state = build_initial_grammar_state(repeated_ir, partition);
  require(run_grammar_state_machine(repeated_state).ok());
  auto tree = build_structural_occurrence_graph_from_grammar_state(projection(repeated_ir), repeated_state);
  for (const auto& o : tree.occurrences)
    if (o.parent_occurrence_id.valid())
      require(o.token_end_ordinal-o.token_start_ordinal<=2);
  auto singleton_ir = input({"A","A","A"});
  partition.token_partitions = {"s0","","s1"};
  auto singleton_state = build_initial_grammar_state(singleton_ir, partition);
  require(run_grammar_state_machine(singleton_state).ok());
  tree = build_structural_occurrence_graph_from_grammar_state(projection(singleton_ir), singleton_state);
  for (const auto& o : tree.occurrences)
    if (o.parent_occurrence_id.valid())
      require(o.token_end_ordinal-o.token_start_ordinal==1);

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
  {std::ofstream out(path);out<<"schema: traceloom-match-rules-v1\npartition_by: scheduler_step\n";}
  require(load_macro_match_rules(path.string()).partition_by=="scheduler_step");
  {std::ofstream out(path);out<<"schema: traceloom-match-rules-v1\npartition_by: timestamp\n";}
  bool invalid_partition=false;
  try {(void)load_macro_match_rules(path.string());}
  catch (const std::invalid_argument&) { invalid_partition=true; }
  require(invalid_partition);
  std::filesystem::remove(path);
}
