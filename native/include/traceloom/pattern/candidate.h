#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

struct CandidateKey {
  std::vector<SymbolId> symbols;
  std::uint32_t device_id = 0;
};

inline bool operator==(const CandidateKey& lhs,
                       const CandidateKey& rhs) noexcept {
  return lhs.device_id == rhs.device_id && lhs.symbols == rhs.symbols;
}

inline bool operator<(const CandidateKey& lhs,
                      const CandidateKey& rhs) noexcept {
  if (lhs.device_id != rhs.device_id) {
    return lhs.device_id < rhs.device_id;
  }
  return lhs.symbols < rhs.symbols;
}

struct CandidateOccurrence {
  CandidateKey key;
  std::size_t begin = 0;
  std::size_t end = 0;
  PartitionId partition_id;
};

enum class CandidateDiagnosticCode {
  kCrossesNoCrossBoundary,
  kEnclosesNoCrossInterval,
  kAmbiguousIntervalBlocksCandidate,
  kCrossesSequenceDomain,
  kPartialNoCrossInterval = kCrossesNoCrossBoundary,
};

struct CandidateDiagnostic {
  CandidateDiagnosticCode code =
      CandidateDiagnosticCode::kCrossesNoCrossBoundary;
  CandidateKey key;
  std::size_t begin = 0;
  std::size_t end = 0;
  PartitionId partition_id;
  ProtectedIntervalId protected_interval_id;
};

struct CandidateScanResult {
  std::vector<CandidateOccurrence> occurrences;
  std::vector<CandidateDiagnostic> diagnostics;
};

struct CandidateSummaryRow {
  CandidateKey key;
  std::size_t occurrence_count = 0;
  std::size_t first_begin = 0;
};

const char* candidate_diagnostic_code_name(CandidateDiagnosticCode code);

}  // namespace traceloom
