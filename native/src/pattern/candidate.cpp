#include "traceloom/pattern/candidate.h"

namespace traceloom {

const char* candidate_diagnostic_code_name(CandidateDiagnosticCode code) {
  switch (code) {
    case CandidateDiagnosticCode::kCrossesNoCrossBoundary:
      return "CandidateCrossesHardBoundary";
    case CandidateDiagnosticCode::kEnclosesNoCrossInterval:
      return "CandidateEnclosesForbiddenInterval";
    case CandidateDiagnosticCode::kAmbiguousIntervalBlocksCandidate:
      return "AmbiguousIntervalBlocksCandidate";
    case CandidateDiagnosticCode::kCrossesSequenceDomain:
      return "CandidateCrossesSequenceDomain";
  }
  return "UnknownCandidateDiagnostic";
}

}  // namespace traceloom
