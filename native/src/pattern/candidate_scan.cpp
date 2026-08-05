#include "traceloom/pattern/candidate_scan.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

#include "traceloom/pattern/candidate_reduce.h"
#include "traceloom/runtime/thread_pool.h"

namespace traceloom {
namespace {

CandidateDiagnosticCode diagnostic_code_from_violation(
    BoundaryViolationKind kind) {
  switch (kind) {
    case BoundaryViolationKind::kCrossesNoCrossBoundary:
      return CandidateDiagnosticCode::kCrossesNoCrossBoundary;
    case BoundaryViolationKind::kEnclosesNoCrossInterval:
      return CandidateDiagnosticCode::kEnclosesNoCrossInterval;
    case BoundaryViolationKind::kAmbiguousIntervalBlocksCandidate:
      return CandidateDiagnosticCode::kAmbiguousIntervalBlocksCandidate;
    case BoundaryViolationKind::kNone:
      break;
  }
  throw std::invalid_argument("invalid boundary violation kind");
}

CandidateKey build_candidate_key(const ProtectedSequence& sequence,
                                 std::size_t begin,
                                 std::size_t end) {
  CandidateKey key;
  key.symbols.reserve(end - begin);
  for (std::size_t index = begin; index < end; ++index) {
    key.symbols.push_back(sequence.token_at(index).symbol_id);
  }
  return key;
}

bool stays_in_one_device_sequence(const ProtectedSequence& sequence,
                                  std::size_t begin,
                                  std::size_t end) {
  const std::uint32_t device_id = sequence.token_at(begin).device_id;
  for (std::size_t index = begin + 1; index < end; ++index) {
    if (sequence.token_at(index).device_id != device_id) {
      return false;
    }
  }
  return true;
}

void remove_ambiguous_key_occurrences(CandidateScanResult& result) {
  std::vector<CandidateKey> blocked_keys;
  for (const CandidateDiagnostic& diagnostic : result.diagnostics) {
    if (diagnostic.code ==
        CandidateDiagnosticCode::kAmbiguousIntervalBlocksCandidate) {
      blocked_keys.push_back(diagnostic.key);
    }
  }
  if (blocked_keys.empty()) {
    return;
  }

  std::sort(blocked_keys.begin(), blocked_keys.end());
  blocked_keys.erase(std::unique(blocked_keys.begin(), blocked_keys.end()),
                     blocked_keys.end());
  result.occurrences.erase(
      std::remove_if(result.occurrences.begin(), result.occurrences.end(),
                     [&](const CandidateOccurrence& occurrence) {
                       return std::binary_search(blocked_keys.begin(),
                                                 blocked_keys.end(),
                                                 occurrence.key);
                     }),
      result.occurrences.end());
}

std::vector<CandidateKey> ambiguous_keys(
    const std::vector<CandidateDiagnostic>& diagnostics) {
  std::vector<CandidateKey> blocked_keys;
  for (const CandidateDiagnostic& diagnostic : diagnostics) {
    if (diagnostic.code ==
        CandidateDiagnosticCode::kAmbiguousIntervalBlocksCandidate) {
      blocked_keys.push_back(diagnostic.key);
    }
  }
  std::sort(blocked_keys.begin(), blocked_keys.end());
  blocked_keys.erase(std::unique(blocked_keys.begin(), blocked_keys.end()),
                     blocked_keys.end());
  return blocked_keys;
}

bool summary_less(const CandidateSummaryRow& lhs,
                  const CandidateSummaryRow& rhs) {
  if (lhs.key < rhs.key) {
    return true;
  }
  if (rhs.key < lhs.key) {
    return false;
  }
  return lhs.first_begin < rhs.first_begin;
}

struct LocalCandidateAggregate {
  std::vector<CandidateSummaryRow> summaries;
  std::vector<CandidateDiagnostic> diagnostics;
};

}  // namespace

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
      // Tokens from different devices are independent execution sequences.
      // Never create a pattern merely from their adjacency in the flattened
      // storage order.
      if (!stays_in_one_device_sequence(sequence, begin, end)) {
        continue;
      }
      CandidateKey key = build_candidate_key(sequence, begin, end);
      const BoundaryViolation violation = boundaries.first_violation(begin, end);
      if (violation.valid()) {
        result.diagnostics.push_back(CandidateDiagnostic{
            diagnostic_code_from_violation(violation.kind), std::move(key),
            begin, end, partition.id, violation.protected_interval_id});
        continue;
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
  remove_ambiguous_key_occurrences(result);
  return result;
}

CandidateAggregateResult scan_and_reduce_candidate_partitions(
    const ProtectedSequence& sequence,
    const BoundaryIndex& boundaries,
    const PartitionPlan& plan,
    CandidateScanConfig config,
    std::size_t thread_count) {
  std::vector<LocalCandidateAggregate> local_results(plan.size());
  ThreadPool pool(thread_count);
  pool.parallel_for(plan.size(), [&](std::size_t partition_index) {
    CandidateScanResult scan = scan_candidates_with_diagnostics(
        sequence, boundaries, plan.partition_at(partition_index), config);
    local_results[partition_index].summaries =
        reduce_candidates(std::move(scan.occurrences));
    local_results[partition_index].diagnostics = std::move(scan.diagnostics);
  });

  std::size_t summary_count = 0;
  std::size_t diagnostic_count = 0;
  for (const LocalCandidateAggregate& local : local_results) {
    summary_count += local.summaries.size();
    diagnostic_count += local.diagnostics.size();
  }

  CandidateAggregateResult result;
  result.diagnostics.reserve(diagnostic_count);
  std::vector<CandidateSummaryRow> mapped_summaries;
  mapped_summaries.reserve(summary_count);
  for (LocalCandidateAggregate& local : local_results) {
    mapped_summaries.insert(
        mapped_summaries.end(),
        std::make_move_iterator(local.summaries.begin()),
        std::make_move_iterator(local.summaries.end()));
    result.diagnostics.insert(
        result.diagnostics.end(),
        std::make_move_iterator(local.diagnostics.begin()),
        std::make_move_iterator(local.diagnostics.end()));
  }

  const std::vector<CandidateKey> blocked_keys =
      ambiguous_keys(result.diagnostics);
  std::sort(mapped_summaries.begin(), mapped_summaries.end(), summary_less);
  result.summaries.reserve(mapped_summaries.size());
  for (CandidateSummaryRow& row : mapped_summaries) {
    if (std::binary_search(blocked_keys.begin(), blocked_keys.end(), row.key)) {
      continue;
    }
    result.occurrence_count += row.occurrence_count;
    if (result.summaries.empty() ||
        !(result.summaries.back().key == row.key)) {
      result.summaries.push_back(std::move(row));
      continue;
    }
    CandidateSummaryRow& existing = result.summaries.back();
    existing.occurrence_count += row.occurrence_count;
    existing.first_begin = std::min(existing.first_begin, row.first_begin);
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
