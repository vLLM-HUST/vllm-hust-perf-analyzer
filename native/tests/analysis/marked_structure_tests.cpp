#include "traceloom/analysis/marked_structure.h"
#include "traceloom/analysis/structural_position_model.h"
#include "traceloom/testing/test_util.h"
#include <map>
using namespace traceloom;
using traceloom::testing::require;
std::vector<StructuralProjectionToken> tokens(std::initializer_list<const char*> names) {
  std::vector<StructuralProjectionToken> out;std::map<std::string,SymbolId> ids;
  for(auto name:names) {
    auto [it,inserted]=ids.emplace(name,SymbolId(static_cast<std::uint32_t>(ids.size())));
    StructuralProjectionToken t;t.ordinal=out.size();t.anchor_id=AnchorId(t.ordinal);
    t.symbol_id=it->second;t.display_op=name;t.start_ns=t.ordinal*10;t.end_ns=t.start_ns+20;
    out.push_back(t);
  }
  return out;
}
int count(const StructuralOccurrenceGraph& g,const std::string& label) {
  int n=0;for(const auto& o:g.occurrences) if(g.node_defs[o.node_def_id.value()].display_op==label)++n;return n;
}
int main() {
  MarkedStructureRules rules{"hc","P","Q",{{"attention",{"A"}},{"moe",{"M"}}},{{"layer",{"attention","moe"}}}};
  auto t=tokens({"x","P","A","Q","P","M","Q","P","A","Q","P","M","Q","z"});
  auto g=build_marked_structural_graph(t,rules);
  require(count(g,"layer")==2 && count(g,"attention")==2 && count(g,"moe")==2);
  int templates=0;for(auto& d:g.node_defs) if(d.display_op=="layer") ++templates;
  require(templates==1); // true identical signatures share a Position
  (void)build_structural_position_model(g,t.size());
  auto variants=tokens({"P","A","Q","P","M","Q","P","A","x","Q","P","M","Q"});
  auto v=build_marked_structural_graph(variants,rules);templates=0;
  for(auto& d:v.node_defs) if(d.display_op=="layer") ++templates;
  require(templates==2);(void)build_structural_position_model(v,variants.size());
  auto test=[&](std::initializer_list<const char*> names,int layers,int ambiguous,int unclassified) {
    auto t=tokens(names);auto g=build_marked_structural_graph(t,rules);
    require(count(g,"layer")==layers && count(g,"ambiguous")==ambiguous && count(g,"unclassified")==unclassified);
    int leaves=0;for(const auto& o:g.occurrences) if(g.node_defs[o.node_def_id.value()].kind==StructuralNodeKind::kAtom) ++leaves;
    require(leaves==static_cast<int>(t.size()));(void)build_structural_position_model(g,t.size());
  };
  test({"P","A","M","Q"},0,1,0);
  test({"P","x","Q"},0,0,1);
  test({"P","A","Q","x","P","M","Q"},0,0,0); // gap is a barrier
  test({"P","M","Q","P","A","Q"},0,0,0); // wrong type order
  test({"Q","P","P","A","Q","P"},0,0,0); // malformed stays raw
  test({"x","y"},0,0,0);
  MarkedStructureRules residual{"residual","Norm","End",
      {{"attention",{"A"}},{"mlp",{"M"}}},{{"layer",{"attention","mlp"}}}};
  residual.mode="end_delimited";residual.end_predecessor="Add";
  auto check=[&](std::initializer_list<const char*> names,const MarkedStructureRules& r,
                 const std::string& label,int expected) {
    auto ts=tokens(names);auto graph=build_marked_structural_graph(ts,r);
    require(count(graph,label)==expected);
    std::size_t leaves=0;
    for(const auto& o:graph.occurrences)
      if(graph.node_defs[o.node_def_id.value()].kind==StructuralNodeKind::kAtom)++leaves;
    require(leaves==ts.size());
    (void)build_structural_position_model(graph,ts.size());
  };
  check({"prep","Norm","A","Add","End","M","Add","End",
         "A","x","Add","End","M","Add","End","tail"},residual,"layer",2);
  check({"Norm","A","Add","M","Add","End"},residual,"layer",0); // missing end
  check({"Norm","A","Add","End","M","End"},residual,"layer",0); // wrong predecessor
  check({"A","Add","End","M","Add","End"},residual,"layer",0); // clipped first unit
  check({"Norm","M","Add","End","A","Add","End"},residual,"layer",0);
  check({"Norm","A","Add","End","M","Add","End","sample",
         "Norm","Norm","A","Add","End","M","Add","End"},residual,"layer",2);
  residual.boundary_label="residual_norm";
  residual.compositions={{"layer",{"attention","residual_norm","mlp","residual_norm"}}};
  const auto separated=tokens({"Norm","A","Add","End","M","Add","End","tail"});
  const auto separated_graph=build_marked_structural_graph(separated,residual);
  require(count(separated_graph,"layer")==1 && count(separated_graph,"residual_norm")==2);
  std::map<std::string,std::vector<std::pair<std::uint32_t,std::uint32_t>>> spans;
  for(const auto& o:separated_graph.occurrences)
    spans[separated_graph.node_defs[o.node_def_id.value()].display_op].push_back(
        {o.token_start_ordinal,o.token_end_ordinal});
  require(spans["attention"]==decltype(spans)::mapped_type{{1,2}});
  require(spans["mlp"]==decltype(spans)::mapped_type{{4,5}});
  require(spans["residual_norm"]==decltype(spans)::mapped_type{{2,4},{5,7}});
  require(spans["layer"]==decltype(spans)::mapped_type{{1,7}});
  (void)build_structural_position_model(separated_graph,separated.size());
  check({"Norm","A","Add","End","M","Add","End"},residual,"layer",1);
  check({"A","Add","End","M","Add","End"},residual,"residual_norm",2);
  check({"Norm","A","End","M","Add","End"},residual,"layer",0);
  check({"Norm","A","M","Add","End"},residual,"layer",0);
  check({"Norm","A","Add","End","sample","Norm","M","Add","End"},residual,"layer",0);
  // Matching-only CANN preprocessing must not merge exact kernel variants.
  residual.name_normalization="ascend_decorated_kernel";
  residual.end_predecessor.clear();
  residual.end_predecessor_any={"Add", "aclnnAdds_AddAiCore_Add"};
  auto decorated=tokens({"Norm_high_performance_0", "A_high_performance_1", "Add_high_performance_1", "End_high_performance_1",
      "M_high_precision_1", "Add_high_performance_1", "End_high_performance_1",
      "A_high_performance_2", "Add_high_performance_1", "End_high_performance_1",
      "M_high_precision_1", "Add_high_performance_1", "End_high_performance_1"});
  auto normalized=build_marked_structural_graph(decorated,residual);
  require(count(normalized,"layer")==2);
  templates=0;
  for (const auto& def:normalized.node_defs) if(def.display_op=="layer") ++templates;
  require(templates==2); // same family, different exact ordered identities
  require(count(normalized,"A_high_performance_1")==1);
  require(count(normalized,"A_high_performance_2")==1);
  (void)build_structural_position_model(normalized,decorated.size());
  check({"Norm_custom", "A", "Add", "End", "M", "Add", "End"},residual,"layer",0);
  check({"Norm", "A_custom", "Add", "End", "M", "Add", "End"},residual,"layer",0);
  check({"Norm", "A", "Sub_high_performance_1", "End", "M", "Add", "End"},residual,"layer",0);
  residual.name_normalization="exact";
  require(count(build_marked_structural_graph(decorated,residual),"layer")==0);
  MarkedStructureRules cycle;cycle.id="cycle";cycle.mode="cycle_end";
  cycle.end_sequence={"S","tail"};cycle.cycle_label="cycle_candidate";
  check({"prefix","S","tail","A","S","tail","B","S","tail","partial"},
        cycle,"cycle_candidate",2);
  check({"S","tail","A","S","wrong","B","S","tail"},cycle,"cycle_candidate",0);
  check({"S","tail","A","S"},cycle,"cycle_candidate",0);
}
