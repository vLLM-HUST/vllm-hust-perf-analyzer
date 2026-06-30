#include "traceloom/pattern/pattern_candidate_summary.h"

namespace traceloom {

PatternCandidateSummaryTable build_pattern_candidate_summary(
    const std::vector<CandidateSummaryRow>& reduced_candidates) {
  PatternCandidateSummaryTable table;
  table.rows.reserve(reduced_candidates.size());
  for (std::size_t index = 0; index < reduced_candidates.size(); ++index) {
    const CandidateSummaryRow& row = reduced_candidates[index];
    table.rows.push_back(PatternCandidateSummaryRow{
        checked_next_id<PatternId>(index), row.key, row.occurrence_count,
        row.first_begin});
  }
  return table;
}

}  // namespace traceloom
