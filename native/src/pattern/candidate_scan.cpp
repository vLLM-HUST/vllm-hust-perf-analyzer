#include "traceloom/pattern/candidate_scan.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "traceloom/runtime/thread_pool.h"

namespace traceloom {

CandidateScanResult scan_candidates_with_diagnostics(
    const ProtectedSequence& sequence,
    const BoundaryIndex& boundaries,
    const Partition& partition,
    CandidateScanConfig config) {
  if (config.min_length == 0 || config.max_length < config.min_length) {
    throw std::invalid_argument("invalid candidate scan length config");
  }
  if (partition.owned_begin > partition.owned_end ||
      partition.read_begin > partition.owned_begin ||
      partition.owned_end > partition.read_end ||
      partition.read_end > sequence.size()) {
    throw std::invalid_argument("invalid partition range");
  }

  CandidateScanResult result;
  for (std::size_t begin = partition.owned_begin; begin < partition.owned_end;
       ++begin) {
    const std::size_t max_end =
        std::min(partition.read_end, begin + config.max_length);
    for (std::size_t end = begin + config.min_length; end <= max_end; ++end) {
      const ProtectedIntervalId blocked_interval_id =
          boundaries.first_no_cross_violation(begin, end);
      if (blocked_interval_id.valid()) {
        result.diagnostics.push_back(CandidateDiagnostic{
            CandidateDiagnosticCode::kPartialNoCrossInterval, begin, end,
            partition.id, blocked_interval_id});
        continue;
      }

      CandidateKey key;
      key.symbols.reserve(end - begin);
      for (std::size_t index = begin; index < end; ++index) {
        key.symbols.push_back(sequence.token_at(index).symbol_id);
      }
      result.occurrences.push_back(
          CandidateOccurrence{std::move(key), begin, end, partition.id});
    }
  }
  return result;
}

std::vector<CandidateOccurrence> scan_candidates(
    const ProtectedSequence& sequence,
    const BoundaryIndex& boundaries,
    const Partition& partition,
    CandidateScanConfig config) {
  return scan_candidates_with_diagnostics(sequence, boundaries, partition,
                                          config)
      .occurrences;
}

CandidateScanResult scan_candidate_partitions_with_diagnostics(
    const ProtectedSequence& sequence,
    const BoundaryIndex& boundaries,
    const PartitionPlan& plan,
    CandidateScanConfig config,
    std::size_t thread_count) {
  std::vector<CandidateScanResult> local_results(plan.size());
  ThreadPool pool(thread_count);
  pool.parallel_for(plan.size(), [&](std::size_t partition_index) {
    local_results[partition_index] =
        scan_candidates_with_diagnostics(
            sequence, boundaries, plan.partition_at(partition_index), config);
  });

  CandidateScanResult result;
  std::size_t occurrence_count = 0;
  std::size_t diagnostic_count = 0;
  for (const CandidateScanResult& local : local_results) {
    occurrence_count += local.occurrences.size();
    diagnostic_count += local.diagnostics.size();
  }
  result.occurrences.reserve(occurrence_count);
  result.diagnostics.reserve(diagnostic_count);
  for (CandidateScanResult& local : local_results) {
    result.occurrences.insert(
        result.occurrences.end(),
        std::make_move_iterator(local.occurrences.begin()),
        std::make_move_iterator(local.occurrences.end()));
    result.diagnostics.insert(
        result.diagnostics.end(),
        std::make_move_iterator(local.diagnostics.begin()),
        std::make_move_iterator(local.diagnostics.end()));
  }
  return result;
}

std::vector<CandidateOccurrence> scan_candidate_partitions(
    const ProtectedSequence& sequence,
    const BoundaryIndex& boundaries,
    const PartitionPlan& plan,
    CandidateScanConfig config,
    std::size_t thread_count) {
  return scan_candidate_partitions_with_diagnostics(
             sequence, boundaries, plan, config, thread_count)
      .occurrences;
}

}  // namespace traceloom
