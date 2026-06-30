#pragma once

#include <cstddef>
#include <vector>

#include "traceloom/core/ids.h"
#include "traceloom/pattern/candidate.h"

namespace traceloom {

struct PatternCandidateSummaryRow {
  PatternId id;
  CandidateKey key;
  std::size_t occurrence_count = 0;
  std::size_t first_begin = 0;
};

struct PatternCandidateSummaryTable {
  std::vector<PatternCandidateSummaryRow> rows;
};

PatternCandidateSummaryTable build_pattern_candidate_summary(
    const std::vector<CandidateSummaryRow>& reduced_candidates);

}  // namespace traceloom
