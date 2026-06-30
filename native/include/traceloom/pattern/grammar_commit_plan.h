#pragma once

#include <cstddef>
#include <vector>

#include "traceloom/pattern/grammar_round.h"
#include "traceloom/pattern/grammar_snapshot.h"
#include "traceloom/sequence/boundary_index.h"

namespace traceloom {

enum class GrammarCommitPlanStatus {
  kValid,
  kRejected,
};

enum class GrammarCommitDiagnosticCode {
  kStaleSnapshotGeneration,
  kReplacementSpanMismatch,
  kOverlappingReplacementSpan,
  kProtectedIntervalViolation,
};

struct GrammarCommitDiagnostic {
  GrammarCommitDiagnosticCode code =
      GrammarCommitDiagnosticCode::kReplacementSpanMismatch;
  std::size_t occurrence_index = 0;
  ProtectedIntervalId protected_interval_id = ProtectedIntervalId::invalid();
  BoundaryViolationKind boundary_violation_kind = BoundaryViolationKind::kNone;
};

struct GrammarReplacementSpan {
  GrammarNodeId begin_node_id;
  GrammarNodeId last_node_id;
  std::size_t begin_dense_index = 0;
  std::size_t end_dense_index_exclusive = 0;
  std::size_t source_begin_token_index = 0;
  std::size_t source_end_token_index_exclusive = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  GrammarChunkId owner_chunk_id;
  std::size_t owner_worker_id = 0;
};

struct GrammarCommitPlan {
  GrammarCommitPlanStatus status = GrammarCommitPlanStatus::kValid;
  std::uint64_t snapshot_generation = 0;
  GrammarGlobalAction action;
  std::vector<GrammarReplacementSpan> replacement_spans;
  std::vector<GrammarCommitDiagnostic> diagnostics;

  bool valid() const noexcept {
    return status == GrammarCommitPlanStatus::kValid && diagnostics.empty();
  }
};

const char* grammar_commit_diagnostic_code_name(
    GrammarCommitDiagnosticCode code);

GrammarCommitPlan build_adjacent_run_commit_plan(
    const GrammarSnapshot& snapshot,
    const GrammarGlobalAction& action);

GrammarCommitPlan build_pair_grammar_commit_plan(
    const GrammarSnapshot& snapshot,
    const GrammarGlobalAction& action);

GrammarCommitPlan build_native_macro_run_commit_plan(
    const GrammarSnapshot& snapshot,
    const GrammarGlobalAction& action);

}  // namespace traceloom
