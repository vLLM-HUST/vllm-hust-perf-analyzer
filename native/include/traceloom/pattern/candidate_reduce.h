#pragma once

#include <vector>

#include "traceloom/pattern/candidate.h"

namespace traceloom {

std::vector<CandidateSummaryRow> reduce_candidates(
    std::vector<CandidateOccurrence> occurrences);

}  // namespace traceloom
