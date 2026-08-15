#pragma once

#include "traceloom/analysis/structural_occurrence_graph.h"

namespace traceloom {

// Compatibility names for the former primary ReportTree product. Canonical
// analysis owns the structural occurrence graph; report and embedding clients
// may continue to compile against these aliases while migrating deliberately.
using ReportAnchorKind = StructuralAnchorKind;
using ReportNodeKind = StructuralNodeKind;
using ReportToken = StructuralProjectionToken;
using ReportNodeDef = StructuralNodeDef;
using ReportNodeOccurrence = StructuralNodeOccurrence;
using ReportTreeEdge = StructuralOccurrenceEdge;
using ReportCoverageKind = StructuralCoverageKind;
using ReportNodeCoverage = StructuralNodeCoverage;
using ReportTree = StructuralOccurrenceGraph;

const char* report_anchor_kind_name(ReportAnchorKind kind);

const ReportNodeDef& node_def(const ReportTree& tree, ReportNodeDefId id);
const ReportNodeOccurrence& node_occurrence(const ReportTree& tree,
                                            ReportNodeOccurrenceId id);

std::size_t occurrence_count_for_def(const ReportTree& tree,
                                     ReportNodeDefId id);

}  // namespace traceloom
