#include "traceloom/pattern/candidate_reduce.h"

#include <algorithm>

namespace traceloom {

namespace {

bool occurrence_less(const CandidateOccurrence& lhs,
                     const CandidateOccurrence& rhs) {
  if (lhs.key < rhs.key) {
    return true;
  }
  if (rhs.key < lhs.key) {
    return false;
  }
  if (lhs.begin != rhs.begin) {
    return lhs.begin < rhs.begin;
  }
  if (lhs.end != rhs.end) {
    return lhs.end < rhs.end;
  }
  return lhs.partition_id < rhs.partition_id;
}

bool same_occurrence_identity(const CandidateOccurrence& lhs,
                              const CandidateOccurrence& rhs) {
  return lhs.key == rhs.key && lhs.begin == rhs.begin && lhs.end == rhs.end;
}

}  // namespace

std::vector<CandidateSummaryRow> reduce_candidates(
    std::vector<CandidateOccurrence> occurrences) {
  std::sort(occurrences.begin(), occurrences.end(), occurrence_less);
  occurrences.erase(std::unique(occurrences.begin(), occurrences.end(),
                                same_occurrence_identity),
                    occurrences.end());

  std::vector<CandidateSummaryRow> summary;
  for (const CandidateOccurrence& occurrence : occurrences) {
    if (summary.empty() || !(summary.back().key == occurrence.key)) {
      summary.push_back(
          CandidateSummaryRow{occurrence.key, 1, occurrence.begin});
      continue;
    }
    summary.back().occurrence_count += 1;
  }
  return summary;
}

}  // namespace traceloom
