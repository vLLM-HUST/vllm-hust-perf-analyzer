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
  if (snapshot.nodes[occurrence.begin_dense_index].node_id !=
      occurrence.begin_node_id) {
    return false;
  }
  if (snapshot.nodes[occurrence.end_dense_index_exclusive - 1].node_id !=
      occurrence.last_node_id) {
    return false;
  }
  if (action.kind == GrammarActionKind::kReplacePair) {
    if (occurrence.end_dense_index_exclusive - occurrence.begin_dense_index !=
        action.key.run_len) {
      return false;
    }
    if (action.key.run_len != 2 ||
        occurrence.end_dense_index_exclusive !=
            occurrence.begin_dense_index + 2) {
      return false;
    }
    return snapshot.nodes[occurrence.begin_dense_index].symbol_id ==
               action.key.symbol_id &&
           snapshot.nodes[occurrence.begin_dense_index + 1].symbol_id ==
               action.key.second_symbol_id;
  }
  if (action.kind == GrammarActionKind::kCompressMaximalRuns) {
    if (occurrence.end_dense_index_exclusive - occurrence.begin_dense_index <
        2) {
      return false;
    }
    if (occurrence.begin_dense_index > 0 &&
        snapshot.nodes[occurrence.begin_dense_index - 1].symbol_id ==
            action.key.symbol_id) {
      return false;
    }
    if (occurrence.end_dense_index_exclusive < snapshot.nodes.size() &&
        snapshot.nodes[occurrence.end_dense_index_exclusive].symbol_id ==
            action.key.symbol_id) {
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
  if (action.kind == GrammarActionKind::kReplaceRepeatedBlock) {
    if (occurrence.end_dense_index_exclusive - occurrence.begin_dense_index !=
        action.key.run_len) {
      return false;
    }
    if (action.key.run_len != action.block_rhs_symbols.size()) {
      return false;
    }
    if (action.repeat_count < 2 ||
        action.repeat_count != action.occurrences.size()) {
      return false;
    }
    for (std::size_t dense_index = occurrence.begin_dense_index;
         dense_index < occurrence.end_dense_index_exclusive; ++dense_index) {
      if (snapshot.nodes[dense_index].symbol_id !=
          action.block_rhs_symbols[dense_index -
                                   occurrence.begin_dense_index]) {
        return false;
      }
    }
    return true;
  }
  if (occurrence.end_dense_index_exclusive - occurrence.begin_dense_index !=
      action.key.run_len) {
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

GrammarCommitPlan rejected_plan_for_action(const GrammarSnapshot& snapshot,
                                           const GrammarGlobalAction& action) {
  GrammarCommitPlan plan;
  plan.status = GrammarCommitPlanStatus::kRejected;
  plan.snapshot_generation = snapshot.generation;
  plan.action = action;
  plan.diagnostics.push_back(GrammarCommitDiagnostic{
      GrammarCommitDiagnosticCode::kReplacementSpanMismatch, 0,
      ProtectedIntervalId::invalid(), BoundaryViolationKind::kNone});
  return plan;
}

GrammarCommitPlan build_commit_plan_for_action(
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
  const bool repeated_block =
      action.kind == GrammarActionKind::kReplaceRepeatedBlock;
  for (std::size_t index = 0; index < occurrences.size(); ++index) {
    const GrammarCandidateOccurrence& occurrence = occurrences[index];
    if (!span_matches_action(snapshot, action, occurrence)) {
      reject(plan, GrammarCommitDiagnostic{
                       GrammarCommitDiagnosticCode::kReplacementSpanMismatch,
                       index, ProtectedIntervalId::invalid(),
                       BoundaryViolationKind::kNone});
      continue;
    }
    if (repeated_block && index == 0 &&
        occurrence.begin_dense_index != 0) {
      reject(plan, GrammarCommitDiagnostic{
                       GrammarCommitDiagnosticCode::kReplacementSpanMismatch,
                       index, ProtectedIntervalId::invalid(),
                       BoundaryViolationKind::kNone});
      continue;
    }
    if (repeated_block && index + 1 == occurrences.size() &&
        occurrence.end_dense_index_exclusive != snapshot.nodes.size()) {
      reject(plan, GrammarCommitDiagnostic{
                       GrammarCommitDiagnosticCode::kReplacementSpanMismatch,
                       index, ProtectedIntervalId::invalid(),
                       BoundaryViolationKind::kNone});
      continue;
    }
    if (repeated_block && has_previous &&
        occurrence.begin_dense_index != previous_end) {
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
  } else if (repeated_block && !plan.replacement_spans.empty()) {
    // The repeated-block action folds all spans into one outer macro whose
    // union span is [first, last). Validate that union span against protected
    // intervals as well: per-block checks alone would allow an outer macro to
    // enclose or straddle a kNoCross interval that each individual block span
    // exactly covers or misses.
    const GrammarReplacementSpan& first_span = plan.replacement_spans.front();
    const GrammarReplacementSpan& last_span = plan.replacement_spans.back();
    const BoundaryViolation outer_violation = first_span_violation(
        snapshot.protected_intervals, first_span.source_begin_token_index,
        last_span.source_end_token_index_exclusive);
    if (outer_violation.valid()) {
      reject(plan,
             GrammarCommitDiagnostic{
                 GrammarCommitDiagnosticCode::kProtectedIntervalViolation,
                 plan.replacement_spans.size() - 1,
                 outer_violation.protected_interval_id, outer_violation.kind});
      plan.replacement_spans.clear();
    }
  }
  return plan;
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
  if (action.kind != GrammarActionKind::kReplaceExactRuns ||
      action.key.producer_id != GrammarProducerId::kAdjacentRun) {
    return rejected_plan_for_action(snapshot, action);
  }
  return build_commit_plan_for_action(snapshot, action);
}

GrammarCommitPlan build_pair_grammar_commit_plan(
    const GrammarSnapshot& snapshot,
    const GrammarGlobalAction& action) {
  if (action.kind != GrammarActionKind::kReplacePair ||
      action.key.producer_id != GrammarProducerId::kPairGrammar) {
    return rejected_plan_for_action(snapshot, action);
  }
  return build_commit_plan_for_action(snapshot, action);
}

GrammarCommitPlan build_native_macro_run_commit_plan(
    const GrammarSnapshot& snapshot,
    const GrammarGlobalAction& action) {
  if (action.kind != GrammarActionKind::kCompressMaximalRuns ||
      action.key.producer_id != GrammarProducerId::kNativeMacroRun) {
    return rejected_plan_for_action(snapshot, action);
  }
  return build_commit_plan_for_action(snapshot, action);
}

GrammarCommitPlan build_exact_repeated_block_commit_plan(
    const GrammarSnapshot& snapshot,
    const GrammarGlobalAction& action) {
  if (action.kind != GrammarActionKind::kReplaceRepeatedBlock ||
      action.key.producer_id != GrammarProducerId::kExactRepeatedBlock) {
    return rejected_plan_for_action(snapshot, action);
  }
  if (action.key.run_len < 2 ||
      action.key.run_len != action.block_rhs_symbols.size() ||
      action.repeat_count < 2 ||
      action.repeat_count != action.occurrences.size()) {
    return rejected_plan_for_action(snapshot, action);
  }
  return build_commit_plan_for_action(snapshot, action);
}

}  // namespace traceloom
