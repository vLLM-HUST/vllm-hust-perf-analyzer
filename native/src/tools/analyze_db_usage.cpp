#include "analyze_db_usage.h"

#include <iostream>

namespace traceloom::tools {

void print_usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " <profile.db-or-profile-dir> [--threads N]"
               " [--output ANALYSIS.db]"
               " [--perfetto-out TIMELINE.json[.gz]]"
               " [--loop-tree-out PATH|-]"
               " [--timings]\n\n"
            << "Writes a self-contained queryable database timeline "
               "under a neighboring traceloom/ directory by default.\n"
            << "Query its traceloom_projection_recipe catalog to select a "
               "scope and compose analytical projections.\n"
            << "Use traceloom_analysis_surface to discover the underlying "
               "hierarchy, cost, replay, and evidence relations.\n"
            << "Use --loop-tree-out only when a Markdown projection is "
               "needed for a human reader. It defaults to a compact grammar "
               "summary; select the exact expanded tree with "
               "--loop-tree-view expanded.\n"
            << "Export an existing queryable database timeline with '" << argv0
            << " export-perfetto analysis.db [--output timeline.json.gz]'.\n"
            << "Add one '--distributed-rank RANK=TIMELINE.db' per rank to align compressed "
               "TraceLoom event lanes at each rank's first event.\n"
            << "For collective comparison, explicitly opt into an auditable end-affine "
               "display with '--distributed-clock-model MODELS.jsonl'.\n"
            << "Use --help-advanced for compatibility and debug options.\n";
}

void print_advanced_usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " --source-db <profiler.sqlite-or-db> [--threads N]"
               " [--source-kind auto|ascend_sqlite_hot_path|"
               "ascend_sqlite_split|hygon_sqlite|cuda_nsys_sqlite]"
               " [--grammar-debug-out PATH|-]"
               " [--compat-db-out PATH]"
               " [--output PATH|--aug-db-out PATH|--no-aug-db]"
               " [--perfetto-out PATH]"
               " [--distributed-rank RANK=TIMELINE.db]"
               " [--distributed-clock-model MODELS.jsonl]"
               " [--loop-tree-out PATH|-]"
               " [--loop-tree-db-label LABEL]"
               " [--loop-tree-device-id N]"
               " [--loop-tree-view compact|expanded|both]"
               " [--loop-tree-grammar|--loop-tree-no-grammar]"
               " [--loop-tree-full-discovery-cap N]"
               " [--loop-tree-aux|--loop-tree-no-aux]"
               " [--classification-rules PATH]"
               " [--extend-classification-rules PATH]"
               " [--classification-rule-override RULE_ID.FIELD=VALUE]"
               " [--symbol-rules PATH]"
               " [--extend-symbol-rules PATH]"
               " [--event-reconciliation-rules PATH]"
               " [--extend-event-reconciliation-rules PATH]"
               " [--timings]\n";
}

}  // namespace traceloom::tools
