#pragma once

#include <cstddef>
#include <vector>

#include "traceloom/pattern/candidate.h"
#include "traceloom/sequence/boundary_index.h"
#include "traceloom/sequence/partition_plan.h"
#include "traceloom/sequence/protected_sequence.h"

namespace traceloom {

struct CandidateScanConfig {
  std::size_t min_length = 2;
  std::size_t max_length = 4;
};

struct CandidateAggregateResult {
  std::size_t occurrence_count = 0;
  std::vector<CandidateSummaryRow> summaries;
  std::vector<CandidateDiagnostic> diagnostics;
};

CandidateScanResult scan_candidates_with_diagnostics(
    const ProtectedSequence& sequence,
    const BoundaryIndex& boundaries,
    const Partition& partition,
    CandidateScanConfig config);

std::vector<CandidateOccurrence> scan_candidates(
    const ProtectedSequence& sequence,
    const BoundaryIndex& boundaries,
    const Partition& partition,
    CandidateScanConfig config);

CandidateScanResult scan_candidate_partitions_with_diagnostics(
    const ProtectedSequence& sequence,
    const BoundaryIndex& boundaries,
    const PartitionPlan& plan,
    CandidateScanConfig config,
    std::size_t thread_count);

CandidateAggregateResult scan_and_reduce_candidate_partitions(
    const ProtectedSequence& sequence,
    const BoundaryIndex& boundaries,
    const PartitionPlan& plan,
    CandidateScanConfig config,
    std::size_t thread_count);

std::vector<CandidateOccurrence> scan_candidate_partitions(
    const ProtectedSequence& sequence,
    const BoundaryIndex& boundaries,
    const PartitionPlan& plan,
    CandidateScanConfig config,
    std::size_t thread_count);

}  // namespace traceloom
