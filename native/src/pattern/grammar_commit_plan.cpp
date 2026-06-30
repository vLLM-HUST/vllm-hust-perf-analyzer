#include "traceloom/pattern/grammar_commit_plan.h"

#include <algorithm>

namespace traceloom {
namespace {

BoundaryViolation first_span_violation(
    const std::vector<ProtectedIntervalSpan>& intervals,
    std::size_t begin,
    std::size_t end) {
  if (begin >= end) {
    return BoundaryViolation{};
  }
  const std::size_t last = end - 1;
  for (const ProtectedIntervalSpan& interval : intervals) {
    const bool overlaps = begin <= interval.last_token_index &&
                          last >= interval.first_token_index;
    if (!overlaps) {
      continue;
    }

    if (interval.boundary_policy == BoundaryPolicy::kBlockAnyOverlap) {
      return BoundaryViolation{
          BoundaryViolationKind::kAmbiguousIntervalBlocksCandidate,
          interval.id};
    }

    const bool exact_cover = begin == interval.first_token_index &&
                             last == interval.last_token_index;
    const bool inside_interval = begin >= interval.first_token_index &&
                                 last <= interval.last_token_index;
    const bool strictly_encloses = begin < interval.first_token_index &&
                                   last > interval.last_token_index;

    if (inside_interval || exact_cover) {
      continue;
    }
    if (strictly_encloses &&
        interval.boundary_policy == BoundaryPolicy::kNoCross) {
      return BoundaryViolation{
          BoundaryViolationKind::kEnclosesNoCrossInterval, interval.id};
    }
    if (!strictly_encloses &&
        (interval.boundary_policy == BoundaryPolicy::kNoCross ||
         interval.boundary_policy == BoundaryPolicy::kAllowEnclosing)) {
      return BoundaryViolation{
          BoundaryViolationKind::kCrossesNoCrossBoundary, interval.id};
    }
  }
  return BoundaryViolation{};
}

bool span_matches_action(const GrammarSnapshot& snapshot,
                         const GrammarGlobalAction& action,
                         const GrammarCandidateOccurrence& occurrence) {
  if (occurrence.end_dense_index_exclusive > snapshot.nodes.size()) {
    return false;
  }
  if (occurrence.end_dense_index_exclusive <= occurrence.begin_dense_index) {
    return false;
  }
  if (occurrence.end_dense_index_exclusive - occurrence.begin_dense_index !=
      action.key.run_len) {
    return false;
  }
  if (snapshot.nodes[occurrence.begin_dense_index].node_id !=
      occurrence.begin_node_id) {
    return false;
  }
  if (snapshot.nodes[occurrence.end_dense_index_exclusive - 1].node_id !=
      occurrence.last_node_id) {
    return false;
  }
  for (std::size_t dense_index = occurrence.begin_dense_index;
       dense_index < occurrence.end_dense_index_exclusive; ++dense_index) {
    if (snapshot.nodes[dense_index].symbol_id != action.key.symbol_id) {
      return false;
    }
  }
  return true;
}

GrammarReplacementSpan make_replacement_span(
    const GrammarSnapshot& snapshot,
    const GrammarCandidateOccurrence& occurrence) {
  const GrammarSnapshotNode& first =
      snapshot.nodes[occurrence.begin_dense_index];
  const GrammarSnapshotNode& last =
      snapshot.nodes[occurrence.end_dense_index_exclusive - 1];
  return GrammarReplacementSpan{
      occurrence.begin_node_id,
      occurrence.last_node_id,
      occurrence.begin_dense_index,
      occurrence.end_dense_index_exclusive,
      first.source_begin_token_index,
      last.source_end_token_index_exclusive,
      first.start_ns,
      last.end_ns,
      occurrence.owner_chunk_id,
      occurrence.owner_worker_id};
}

void reject(GrammarCommitPlan& plan,
            GrammarCommitDiagnostic diagnostic) {
  plan.status = GrammarCommitPlanStatus::kRejected;
  plan.diagnostics.push_back(diagnostic);
}

}  // namespace

const char* grammar_commit_diagnostic_code_name(
    GrammarCommitDiagnosticCode code) {
  switch (code) {
    case GrammarCommitDiagnosticCode::kStaleSnapshotGeneration:
      return "stale_snapshot_generation";
    case GrammarCommitDiagnosticCode::kReplacementSpanMismatch:
      return "replacement_span_mismatch";
    case GrammarCommitDiagnosticCode::kOverlappingReplacementSpan:
      return "overlapping_replacement_span";
    case GrammarCommitDiagnosticCode::kProtectedIntervalViolation:
      return "protected_interval_violation";
  }
  return "unknown";
}

GrammarCommitPlan build_adjacent_run_commit_plan(
    const GrammarSnapshot& snapshot,
    const GrammarGlobalAction& action) {
  GrammarCommitPlan plan;
  plan.snapshot_generation = snapshot.generation;
  plan.action = action;

  if (action.snapshot_generation != snapshot.generation) {
    reject(plan,
           GrammarCommitDiagnostic{
               GrammarCommitDiagnosticCode::kStaleSnapshotGeneration, 0,
               ProtectedIntervalId::invalid(), BoundaryViolationKind::kNone});
    return plan;
  }

  std::vector<GrammarCandidateOccurrence> occurrences = action.occurrences;
  std::sort(occurrences.begin(), occurrences.end(),
            [](const GrammarCandidateOccurrence& lhs,
               const GrammarCandidateOccurrence& rhs) {
              if (lhs.begin_dense_index != rhs.begin_dense_index) {
                return lhs.begin_dense_index < rhs.begin_dense_index;
              }
              return lhs.end_dense_index_exclusive <
                     rhs.end_dense_index_exclusive;
            });

  std::size_t previous_end = 0;
  bool has_previous = false;
  for (std::size_t index = 0; index < occurrences.size(); ++index) {
    const GrammarCandidateOccurrence& occurrence = occurrences[index];
    if (!span_matches_action(snapshot, action, occurrence)) {
      reject(plan, GrammarCommitDiagnostic{
                       GrammarCommitDiagnosticCode::kReplacementSpanMismatch,
                       index, ProtectedIntervalId::invalid(),
                       BoundaryViolationKind::kNone});
      continue;
    }
    if (has_previous && occurrence.begin_dense_index < previous_end) {
      reject(plan, GrammarCommitDiagnostic{
                       GrammarCommitDiagnosticCode::kOverlappingReplacementSpan,
                       index, ProtectedIntervalId::invalid(),
                       BoundaryViolationKind::kNone});
      continue;
    }

    const GrammarReplacementSpan span =
        make_replacement_span(snapshot, occurrence);
    const BoundaryViolation violation = first_span_violation(
        snapshot.protected_intervals, span.source_begin_token_index,
        span.source_end_token_index_exclusive);
    if (violation.valid()) {
      reject(plan,
             GrammarCommitDiagnostic{
                 GrammarCommitDiagnosticCode::kProtectedIntervalViolation,
                 index, violation.protected_interval_id, violation.kind});
      continue;
    }

    plan.replacement_spans.push_back(span);
    previous_end = occurrence.end_dense_index_exclusive;
    has_previous = true;
  }

  if (!plan.diagnostics.empty()) {
    plan.replacement_spans.clear();
  }
  return plan;
}

}  // namespace traceloom
