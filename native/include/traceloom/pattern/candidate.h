#pragma once

#include <cstddef>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

struct CandidateKey {
  std::vector<SymbolId> symbols;
};

inline bool operator==(const CandidateKey& lhs,
                       const CandidateKey& rhs) noexcept {
  return lhs.symbols == rhs.symbols;
}

inline bool operator<(const CandidateKey& lhs,
                      const CandidateKey& rhs) noexcept {
  return lhs.symbols < rhs.symbols;
}

struct CandidateOccurrence {
  CandidateKey key;
  std::size_t begin = 0;
  std::size_t end = 0;
  PartitionId partition_id;
};

enum class CandidateDiagnosticCode {
  kPartialNoCrossInterval,
};

struct CandidateDiagnostic {
  CandidateDiagnosticCode code = CandidateDiagnosticCode::kPartialNoCrossInterval;
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

}  // namespace traceloom
