#include "traceloom/pattern/pattern_candidate_table.h"

#include <utility>

namespace traceloom {

PatternCandidateTable build_pattern_candidate_table(
    std::vector<CandidateOccurrence> occurrences) {
  PatternCandidateTable table;
  table.rows = std::move(occurrences);
  return table;
}

PatternMiningDiagnostics build_pattern_mining_diagnostics(
    std::vector<CandidateDiagnostic> rows) {
  PatternMiningDiagnostics diagnostics;
  diagnostics.rows = std::move(rows);
  return diagnostics;
}

}  // namespace traceloom
