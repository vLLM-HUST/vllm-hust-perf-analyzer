#pragma once

#include <vector>

#include "traceloom/pattern/candidate.h"

namespace traceloom {

struct PatternCandidateTable {
  std::vector<CandidateOccurrence> rows;
};

struct PatternMiningDiagnostics {
  std::vector<CandidateDiagnostic> rows;
};

PatternCandidateTable build_pattern_candidate_table(
    std::vector<CandidateOccurrence> occurrences);

PatternMiningDiagnostics build_pattern_mining_diagnostics(
    std::vector<CandidateDiagnostic> diagnostics);

}  // namespace traceloom
