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
}
