#include "traceloom/ir/protected_interval_table.h"
#include "traceloom/ir/token_table.h"
#include "traceloom/pattern/candidate_reduce.h"
#include "traceloom/pattern/candidate_scan.h"
#include "traceloom/sequence/boundary_index.h"
#include "traceloom/sequence/partition_plan.h"
#include "traceloom/sequence/protected_sequence.h"
#include "traceloom/testing/test_util.h"

#include <vector>

namespace {

bool has_key_count(const std::vector<traceloom::CandidateSummaryRow>& summary,
                   std::vector<traceloom::SymbolId> symbols,
                   std::size_t count) {
  for (const traceloom::CandidateSummaryRow& row : summary) {
    if (row.key.symbols == symbols && row.occurrence_count == count) {
      return true;
    }
  }
  return false;
}

bool summaries_equal(
    const std::vector<traceloom::CandidateSummaryRow>& lhs,
    const std::vector<traceloom::CandidateSummaryRow>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (!(lhs[index].key == rhs[index].key)) {
      return false;
    }
    if (lhs[index].occurrence_count != rhs[index].occurrence_count) {
      return false;
    }
    if (lhs[index].first_begin != rhs[index].first_begin) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  TokenTable tokens;
  tokens.append(AnchorId(0), SymbolId(1), 0, 0, 0, 10);
  tokens.append(AnchorId(1), SymbolId(2), 0, 1, 10, 20);
  tokens.append(AnchorId(2), SymbolId(3), 0, 2, 20, 30);
  tokens.append(AnchorId(3), SymbolId(1), 0, 3, 30, 40);
  tokens.append(AnchorId(4), SymbolId(2), 0, 4, 40, 50);

  const ProtectedSequence sequence = ProtectedSequence::from_token_table(tokens);

  ProtectedIntervalTable intervals;
  intervals.append(ProtectedIntervalKind::kGraphReplayUnit,
                   BoundaryPolicy::kNoCross, TokenId(1), TokenId(3),
                   AnchorId(1), AnchorId(3), SourceRefId(0));
  const BoundaryIndex boundaries = BoundaryIndex::build(sequence, intervals);

  const PartitionPlan plan =
      PartitionPlan::build(sequence.size(), PartitionPlanConfig{3, 2});

  const CandidateScanResult scan_result =
      scan_candidate_partitions_with_diagnostics(
          sequence, boundaries, plan, CandidateScanConfig{2, 5}, 1);
  std::vector<CandidateOccurrence> all_occurrences =
      scan_result.occurrences;

  const std::vector<CandidateSummaryRow> summary =
      reduce_candidates(all_occurrences);
  const std::vector<CandidateSummaryRow> summary_threads3 =
      reduce_candidates(scan_candidate_partitions(
          sequence, boundaries, plan, CandidateScanConfig{2, 5}, 3));
  const std::vector<CandidateSummaryRow> summary_threads8 =
      reduce_candidates(scan_candidate_partitions(
          sequence, boundaries, plan, CandidateScanConfig{2, 5}, 8));

  require(summaries_equal(summary, summary_threads3));
  require(summaries_equal(summary, summary_threads8));

  require(has_key_count(summary, {SymbolId(2), SymbolId(3)}, 1));
  require(has_key_count(summary, {SymbolId(3), SymbolId(1)}, 1));
  require(has_key_count(summary, {SymbolId(2), SymbolId(3), SymbolId(1)}, 1));

  for (const CandidateSummaryRow& row : summary) {
    require(row.key.symbols != std::vector<SymbolId>({SymbolId(1),
                                                      SymbolId(2)}));
    require(row.key.symbols != std::vector<SymbolId>({SymbolId(1), SymbolId(2),
                                                      SymbolId(3)}));
    require(row.key.symbols != std::vector<SymbolId>({SymbolId(3), SymbolId(1),
                                                      SymbolId(2)}));
  }

  require(!scan_result.diagnostics.empty());
  bool saw_left_partial_cross = false;
  bool saw_right_partial_cross = false;
  bool saw_strict_enclosing = false;
  for (const CandidateDiagnostic& diagnostic : scan_result.diagnostics) {
    require(diagnostic.code ==
            CandidateDiagnosticCode::kPartialNoCrossInterval);
    require(diagnostic.partition_id.valid());
    require(diagnostic.protected_interval_id == ProtectedIntervalId(0));
    if (diagnostic.begin == 0 && diagnostic.end == 2) {
      saw_left_partial_cross = true;
    }
    if (diagnostic.begin == 2 && diagnostic.end == 5) {
      saw_right_partial_cross = true;
    }
    if (diagnostic.begin == 0 && diagnostic.end == 5) {
      saw_strict_enclosing = true;
    }
  }
  require(saw_left_partial_cross);
  require(saw_right_partial_cross);
  require(saw_strict_enclosing);

  const CandidateScanResult scan_result_threads3 =
      scan_candidate_partitions_with_diagnostics(
          sequence, boundaries, plan, CandidateScanConfig{2, 5}, 3);
  require(scan_result.diagnostics.size() ==
          scan_result_threads3.diagnostics.size());
  for (std::size_t index = 0; index < scan_result.diagnostics.size();
       ++index) {
    require(scan_result.diagnostics[index].begin ==
            scan_result_threads3.diagnostics[index].begin);
    require(scan_result.diagnostics[index].end ==
            scan_result_threads3.diagnostics[index].end);
    require(scan_result.diagnostics[index].partition_id ==
            scan_result_threads3.diagnostics[index].partition_id);
    require(scan_result.diagnostics[index].protected_interval_id ==
            scan_result_threads3.diagnostics[index].protected_interval_id);
  }

  std::vector<CandidateOccurrence> duplicated = all_occurrences;
  duplicated.insert(duplicated.end(), all_occurrences.begin(),
                    all_occurrences.end());
  const std::vector<CandidateSummaryRow> deduped =
      reduce_candidates(duplicated);
  require(has_key_count(deduped, {SymbolId(2), SymbolId(3)}, 1));

  return 0;
}
