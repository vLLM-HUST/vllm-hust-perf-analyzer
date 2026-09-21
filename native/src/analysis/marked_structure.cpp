#include "traceloom/analysis/marked_structure.h"
#include "traceloom/analysis/structural_occurrence_builder.h"
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace traceloom {
namespace {
struct Region {
  std::size_t begin, end;
  std::string label, reason;
  std::vector<Region> children;
};
std::string classify(const std::vector<StructuralProjectionToken>& tokens,
                     std::size_t a, std::size_t b, const MarkedStructureRules& rules) {
  std::string label;
  for (const auto& rule : rules.labels) {
    const bool found = std::any_of(tokens.begin()+a, tokens.begin()+b, [&](const auto& token) {
      return std::find(rule.contains_any.begin(), rule.contains_any.end(), token.display_op)
          != rule.contains_any.end();
    });
    if (found) {
      if (!label.empty()) return "ambiguous";
      label = rule.label;
    }
  }
  return label.empty() ? "unclassified" : label;
}
}
StructuralOccurrenceGraph build_marked_structural_graph(
    const std::vector<StructuralProjectionToken>& tokens, const MarkedStructureRules& rules) {
  if (tokens.empty()) return build_structural_occurrence_graph_from_tokens(tokens);
  StructuralOccurrenceGraph graph;
  graph.diagnostics.push_back({DiagnosticSeverity::kInfo,"model_structure_explicit",rules.id});
  std::vector<Region> units;
  std::optional<std::size_t> start;
  bool nested = false;
  auto warning = [&](const std::string& code, std::size_t ordinal) {
    graph.diagnostics.push_back({DiagnosticSeverity::kWarning, code,
        "rule " + rules.id + ", structural ordinal " + std::to_string(ordinal)});
  };
  if (rules.mode == "cycle_end") {
    if (rules.end_sequence.empty()) throw std::invalid_argument("empty cycle end sequence");
    for (std::size_t i=0; i<tokens.size(); ++i) {
      if (tokens[i].display_op != rules.end_sequence.front()) continue;
      const auto end=i+rules.end_sequence.size();
      bool complete=end<=tokens.size();
      for (std::size_t j=0; complete && j<rules.end_sequence.size(); ++j)
        complete=tokens[i+j].display_op==rules.end_sequence[j];
      if (!complete) {
        warning("model_cycle_incomplete_end",i);
        start.reset(); // Never bridge an observed but unrecognized cycle tail.
        continue;
      }
      if (start) units.push_back({*start,end,rules.cycle_label,
          "model_cycle_candidate:"+rules.id,{}});
      start=end;
      i=end-1;
    }
  } else if (rules.mode == "end_delimited") {
    for (std::size_t i=0; i<tokens.size(); ++i) {
      // A fresh input norm starts/resets a segment. Preparation before the
      // last such seed stays raw; a previous graph/stream never seeds this one.
      if (tokens[i].display_op == rules.begin) start=i;
      if (tokens[i].display_op != rules.end) continue;
      const bool predecessor=rules.end_predecessor.empty() ||
          (i>0 && tokens[i-1].display_op==rules.end_predecessor);
      if (start && predecessor) {
        auto label=classify(tokens,*start,i+1,rules);
        units.push_back({*start,i+1,label,"model_rule:"+rules.id+":"+label,{}});
        if (label=="ambiguous" || label=="unclassified") warning("model_unit_"+label,*start);
      } else warning("model_unit_unsupported_end",i);
      // A valid observed end also seeds the next unit, including after a
      // clipped prefix. It does not invent ownership of the clipped prefix.
      start = predecessor ? std::optional<std::size_t>(i+1) : std::nullopt;
    }
  } else {
    for (std::size_t i=0; i<tokens.size(); ++i) {
      if (tokens[i].display_op == rules.begin) {
        if (start) { nested=true; warning("model_unit_nested_begin",i); }
        else { start=i; nested=false; }
      } else if (tokens[i].display_op == rules.end) {
        if (!start) warning("model_unit_unmatched_end",i);
        else if (!nested) {
          auto label=classify(tokens,*start,i+1,rules);
          units.push_back({*start,i+1,label,"model_rule:"+rules.id+":"+label,{}});
          if (label=="ambiguous" || label=="unclassified") warning("model_unit_"+label,*start);
        }
        start.reset(); nested=false;
      }
    }
    if (start) warning("model_unit_unmatched_begin",*start);
  }
  if (units.empty()) warning("model_unit_no_complete_match",0);
  // Composition is over adjacent COMPLETE units; any intervening event is a
  // barrier. No nesting inference, skipped unknown units, or guessed layer count.
  std::vector<Region> regions;
  for (std::size_t i=0; i<units.size();) {
    const UnitCompositionRule* chosen=nullptr;
    bool ambiguous=false;
    for (const auto& rule : rules.compositions) {
      bool matches=i+rule.sequence.size()<=units.size();
      for (std::size_t j=0; matches && j<rule.sequence.size(); ++j)
        matches=units[i+j].label==rule.sequence[j] &&
                (j==0 || units[i+j-1].end==units[i+j].begin);
      if (matches) { if (chosen) ambiguous=true; chosen=&rule; }
    }
    if (chosen && !ambiguous) {
      auto end=i+chosen->sequence.size();
      regions.push_back({units[i].begin,units[end-1].end,chosen->label,
                        "model_composition:"+rules.id+":"+chosen->label,
                        {units.begin()+i,units.begin()+end}});
      i=end;
    } else {
      if (ambiguous) warning("model_composition_ambiguous",units[i].begin);
      else if (!rules.compositions.empty()) warning("model_composition_unmatched",units[i].begin);
      regions.push_back(units[i++]);
    }
  }
  // Contextual definition sharing preserves one exact ordered child signature
  // per Position. A semantic label alone NEVER makes two variants equivalent.
  using Key=std::tuple<std::uint32_t,std::string,std::string>;
  std::map<Key,StructuralNodeDefId> definitions;
  auto append_def = [&](StructuralNodeKind kind, const std::string& label,
                        const std::string& category, SymbolId symbol,
                        std::uint32_t depth, const std::string& reason) {
    StructuralNodeDefId id(static_cast<std::uint32_t>(graph.node_defs.size()));
    StructuralNodeDef def;
    def.id=id;def.local_node_id="N"+std::to_string(id.value()+1);def.kind=kind;
    def.display_op=label;def.display_category=category;def.symbol_id=symbol;
    def.definition_order=id.value();def.display_depth=depth;def.visibility_reason=reason;
    graph.node_defs.push_back(std::move(def));graph.occurrence_counts_by_def.push_back(0);
    return id;
  };
  auto append_occ = [&](StructuralNodeDefId def, StructuralNodeOccurrenceId parent,
                        std::size_t order, std::size_t a, std::size_t b, bool atom) {
    StructuralNodeOccurrenceId id(static_cast<std::uint32_t>(graph.occurrences.size()));
    graph.occurrences.push_back({id,def,parent,static_cast<std::uint32_t>(order),
        graph.occurrence_counts_by_def[def.value()]++,static_cast<std::uint32_t>(a),
        static_cast<std::uint32_t>(b),0});
    if (parent.valid()) graph.edges.push_back({parent,id,static_cast<std::uint32_t>(order)});
    graph.coverage.push_back({id,static_cast<std::uint32_t>(a),static_cast<std::uint32_t>(b),
        atom?StructuralCoverageKind::kAtomLeaf:StructuralCoverageKind::kDirectBody});
    return id;
  };
  auto signature = [&](std::size_t a,std::size_t b) {
    std::ostringstream key;
    for (auto i=a;i<b;++i) {
      const auto& t=tokens[i];
      key<<t.symbol_id.value()<<':'<<static_cast<int>(t.anchor_kind)<<':'
         <<t.display_op.size()<<':'<<t.display_op<<t.display_category.size()<<':'<<t.display_category<<';';
    }
    return key.str();
  };
  std::function<void(const Region&,StructuralNodeDefId,StructuralNodeOccurrenceId,std::size_t,std::uint32_t)> emit;
  emit = [&](const Region& region,StructuralNodeDefId parent_def,
             StructuralNodeOccurrenceId parent,std::size_t order,std::uint32_t depth) {
    const bool atom=region.label.empty();
    const auto& token=tokens[region.begin];
    const auto label=atom?token.display_op:region.label;
    auto key=Key{parent_def.value(),atom?"atom":region.label,signature(region.begin,region.end)};
    auto found=definitions.find(key);
    StructuralNodeDefId def;
    if (found==definitions.end()) {
      def=append_def(atom?StructuralNodeKind::kAtom:StructuralNodeKind::kSeq,label,
                     atom?token.display_category:"model_rule",atom?token.symbol_id:SymbolId::invalid(),
                     depth,atom?"marked_structure_atom":region.reason);
      definitions.emplace(std::move(key),def);
    } else def=found->second;
    const auto occurrence=append_occ(def,parent,order,region.begin,region.end,atom);
    if (atom) return;
    if (region.children.empty()) {
      for (auto i=region.begin;i<region.end;++i)
        emit({i,i+1,"","",{}},def,occurrence,i-region.begin+1,depth+1);
    } else {
      for (std::size_t i=0;i<region.children.size();++i)
        emit(region.children[i],def,occurrence,i+1,depth+1);
    }
  };
  const auto root=append_def(StructuralNodeKind::kSeq,"model structure","model_rule",
                             SymbolId::invalid(),0,"model_rule:"+rules.id);
  const auto root_occ=append_occ(root,StructuralNodeOccurrenceId::invalid(),0,0,tokens.size(),false);
  std::size_t cursor=0,order=1;
  for (const auto& region : regions) {
    while(cursor<region.begin) {emit({cursor,cursor+1,"","",{}},root,root_occ,order++,1);++cursor;}
    emit(region,root,root_occ,order++,1);cursor=region.end;
  }
  while(cursor<tokens.size()) {emit({cursor,cursor+1,"","",{}},root,root_occ,order++,1);++cursor;}
  validate_structural_occurrence_graph_or_throw(graph,static_cast<std::uint32_t>(tokens.size()));
  return graph;
}
}  // namespace traceloom
