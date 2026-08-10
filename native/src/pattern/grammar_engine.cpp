#include "traceloom/pattern/grammar_engine.h"

#include <algorithm>

namespace traceloom {
namespace {

GrammarEngineStep make_step(const GlobalGrammarState& state,
                            GrammarStage stage,
                            const GrammarRoundResult& round) {
  GrammarEngineStep step;
  step.stage = stage;
  step.producer_id = round.producer_id;
  step.round_status = round.status;
  step.before_generation = state.generation;
  step.after_generation = state.generation;
  step.before_live_node_count = state.live_node_count;
  step.after_live_node_count = state.live_node_count;
  step.action_kind = round.action.kind;
  step.gain = round.action.gain;
  step.replace_count = round.action.replace_count;
  return step;
}

void finish_step_after_apply(GrammarEngineStep& step,
                             const GlobalGrammarState& state) {
  step.after_generation = state.generation;
  step.after_live_node_count = state.live_node_count;
}

GrammarEngineResult reject_commit(GrammarEngineResult result,
                                  const GrammarCommitPlan& plan) {
  result.stop_reason = GrammarEngineStopReason::kCommitPlanRejected;
  result.commit_diagnostics = plan.diagnostics;
  return result;
}

GrammarEngineResult reject_apply(GrammarEngineResult result,
                                 const GrammarApplyResult& apply) {
  result.stop_reason = GrammarEngineStopReason::kApplyRejected;
  result.apply_diagnostics = apply.diagnostics;
  return result;
}

bool only_protected_boundary_diagnostics(
    const std::vector<GrammarCommitDiagnostic>& diagnostics) {
  if (diagnostics.empty()) {
    return false;
  }
  return std::all_of(
      diagnostics.begin(), diagnostics.end(),
      [](const GrammarCommitDiagnostic& diagnostic) {
        return diagnostic.code ==
               GrammarCommitDiagnosticCode::kProtectedIntervalViolation;
      });
}

bool only_protected_boundary_apply_diagnostics(
    const std::vector<GrammarApplyDiagnostic>& diagnostics) {
  if (diagnostics.empty()) {
    return false;
  }
  return std::all_of(
      diagnostics.begin(), diagnostics.end(),
      [](const GrammarApplyDiagnostic& diagnostic) {
        return diagnostic.code ==
               GrammarApplyDiagnosticCode::kProtectedIntervalMappingFailed;
      });
}

enum class RepeatedBlockOutcome {
  kApplied,
  kStopped,
  kError,
};

// Commits and applies a selected exact repeated-block action. On success
// returns kApplied and records the step with post-apply counts. A
// protected-boundary rejection is a clean stop (kStopped): the sequence was
// already at a stable fixpoint and the state is left untouched, with the
// diagnostics recorded for honest reporting. Any other plan/apply failure is
// a hard engine error (kError). The block step is pushed in the stopped and
// error paths; callers push their own contextual step (for example the pair
// stop step) before invoking this helper.
RepeatedBlockOutcome commit_repeated_block_round(
    GlobalGrammarState& state,
    const GrammarRoundResult& block_round,
    GrammarEngineResult& result,
    GrammarEngineStep& block_step) {
  const GrammarSnapshot snapshot = freeze_grammar_snapshot(state);
  const GrammarCommitPlan plan =
      build_exact_repeated_block_commit_plan(snapshot, block_round.action);
  if (!plan.valid()) {
    result.steps.push_back(block_step);
    if (only_protected_boundary_diagnostics(plan.diagnostics)) {
      result.commit_diagnostics = plan.diagnostics;
      return RepeatedBlockOutcome::kStopped;
    }
    state.stage = GrammarStage::kError;
    result.stop_reason = GrammarEngineStopReason::kCommitPlanRejected;
    result.commit_diagnostics = plan.diagnostics;
    return RepeatedBlockOutcome::kError;
  }
  const GrammarApplyResult apply =
      apply_exact_repeated_block_commit_plan(state, plan);
  if (!apply.applied()) {
    result.steps.push_back(block_step);
    if (only_protected_boundary_apply_diagnostics(apply.diagnostics)) {
      result.apply_diagnostics = apply.diagnostics;
      return RepeatedBlockOutcome::kStopped;
    }
    state.stage = GrammarStage::kError;
    result.stop_reason = GrammarEngineStopReason::kApplyRejected;
    result.apply_diagnostics = apply.diagnostics;
    return RepeatedBlockOutcome::kError;
  }
  finish_step_after_apply(block_step, state);
  result.steps.push_back(block_step);
  return RepeatedBlockOutcome::kApplied;
}

}  // namespace

const char* grammar_engine_stop_reason_name(
    GrammarEngineStopReason reason) {
  switch (reason) {
    case GrammarEngineStopReason::kDone:
      return "done";
    case GrammarEngineStopReason::kSequenceTooLargeForFullPairDiscovery:
      return "sequence_too_large_for_full_pair_discovery";
    case GrammarEngineStopReason::kRoundLimitExceeded:
      return "round_limit_exceeded";
    case GrammarEngineStopReason::kCommitPlanRejected:
      return "commit_plan_rejected";
    case GrammarEngineStopReason::kApplyRejected:
      return "apply_rejected";
  }
  return "unknown";
}

GrammarEngineResult run_grammar_state_machine(
    GlobalGrammarState& state,
    const GrammarEngineConfig& config) {
  GrammarEngineResult result;
  std::size_t rounds = 0;

  // The engine config is the single authority for the full-discovery cap;
  // sync it into the state metadata so cap decisions, the discovery bound,
  // and the reported metadata cannot disagree.
  state.metadata.full_discovery_cap = config.full_discovery_cap;

  state.stage = GrammarStage::kRunFold;

  // First pass: exact whole-sequence repeated-block discovery runs before any
  // adjacent-run or pair compression (for modes that enable the producer) so
  // a raw exact tiling is recognized first and cannot be destroyed by a
  // cross-boundary pair action.
  if (grammar_mode_enables_exact_repeated_block(state.metadata.mode)) {
    if (++rounds > config.max_rounds) {
      result.stop_reason = GrammarEngineStopReason::kRoundLimitExceeded;
      state.stage = GrammarStage::kError;
      return result;
    }
    const GrammarRoundResult block_round =
        run_exact_repeated_block_readonly_round(state);
    GrammarEngineStep block_step =
        make_step(state, GrammarStage::kRunFold, block_round);
    if (block_round.status == GrammarRoundStatus::kActionSelected) {
      const RepeatedBlockOutcome outcome =
          commit_repeated_block_round(state, block_round, result, block_step);
      if (outcome == RepeatedBlockOutcome::kApplied) {
        state.stage = GrammarStage::kPairGrammar;
      } else if (outcome == RepeatedBlockOutcome::kStopped) {
        state.stage = GrammarStage::kDone;
        result.stop_reason = GrammarEngineStopReason::kDone;
        return result;
      } else {
        return result;
      }
    } else {
      result.steps.push_back(block_step);
    }
  }

  while (state.stage == GrammarStage::kRunFold) {
    if (++rounds > config.max_rounds) {
      result.stop_reason = GrammarEngineStopReason::kRoundLimitExceeded;
      state.stage = GrammarStage::kError;
      return result;
    }
    const GrammarRoundResult round =
        run_adjacent_run_readonly_round(state);
    GrammarEngineStep step = make_step(state, GrammarStage::kRunFold, round);
    if (round.status == GrammarRoundStatus::kStop) {
      result.steps.push_back(step);
      break;
    }

    const GrammarSnapshot snapshot = freeze_grammar_snapshot(state);
    const GrammarCommitPlan plan =
        build_adjacent_run_commit_plan(snapshot, round.action);
    if (!plan.valid()) {
      result.steps.push_back(step);
      state.stage = GrammarStage::kError;
      return reject_commit(result, plan);
    }
    const GrammarApplyResult apply =
        apply_adjacent_run_commit_plan(state, plan);
    if (!apply.applied()) {
      result.steps.push_back(step);
      state.stage = GrammarStage::kError;
      return reject_apply(result, apply);
    }
    finish_step_after_apply(step, state);
    result.steps.push_back(step);
  }

  if (state.stage == GrammarStage::kRunFold) {
    if (state.live_node_count > config.full_discovery_cap) {
      state.stage = GrammarStage::kDone;
      result.stop_reason =
          GrammarEngineStopReason::kSequenceTooLargeForFullPairDiscovery;
      return result;
    }
    state.stage = GrammarStage::kPairGrammar;
  }

  while (state.stage == GrammarStage::kPairGrammar) {
    if (++rounds > config.max_rounds) {
      result.stop_reason = GrammarEngineStopReason::kRoundLimitExceeded;
      state.stage = GrammarStage::kError;
      return result;
    }
    const GrammarRoundResult pair_round =
        run_pair_grammar_readonly_round(state);
    GrammarEngineStep pair_step =
        make_step(state, GrammarStage::kPairGrammar, pair_round);
    if (pair_round.status == GrammarRoundStatus::kStop) {
      // Later pass at the pair fixpoint. Note that a tiling of a compressed
      // sequence always implies a raw tiling (each macro expands to a fixed
      // word, so an exact macro-level tiling P^R expands to an exact raw
      // tiling), which the first pass would already have recognized. The one
      // reachable case for this pass is the cap boundary: when the raw
      // sequence exceeds full_discovery_cap the first pass is bounded off,
      // and compression can later bring the sequence within the cap, where
      // this pass re-applies the same exact whole-sequence check. It fires
      // only on exact tilings (block x R, R >= 2) with the same protected-
      // interval validation, so it cannot invent partial or inexact structure.
      if (grammar_mode_enables_exact_repeated_block(state.metadata.mode)) {
        if (++rounds > config.max_rounds) {
          result.stop_reason = GrammarEngineStopReason::kRoundLimitExceeded;
          state.stage = GrammarStage::kError;
          return result;
        }
        const GrammarRoundResult block_round =
            run_exact_repeated_block_readonly_round(state);
        GrammarEngineStep block_step =
            make_step(state, GrammarStage::kPairGrammar, block_round);
        if (block_round.status == GrammarRoundStatus::kActionSelected) {
          result.steps.push_back(pair_step);
          const RepeatedBlockOutcome outcome =
              commit_repeated_block_round(state, block_round, result,
                                          block_step);
          if (outcome == RepeatedBlockOutcome::kApplied) {
            state.stage = GrammarStage::kPairGrammar;
            continue;
          }
          if (outcome == RepeatedBlockOutcome::kStopped) {
            state.stage = GrammarStage::kDone;
            result.stop_reason = GrammarEngineStopReason::kDone;
            break;
          }
          return result;
        }
        result.steps.push_back(pair_step);
        result.steps.push_back(block_step);
      } else {
        result.steps.push_back(pair_step);
      }
      state.stage = GrammarStage::kDone;
      break;
    }

    const GrammarSnapshot pair_snapshot = freeze_grammar_snapshot(state);
    const GrammarCommitPlan pair_plan =
        build_pair_grammar_commit_plan(pair_snapshot, pair_round.action);
    if (!pair_plan.valid()) {
      result.steps.push_back(pair_step);
      state.stage = GrammarStage::kError;
      return reject_commit(result, pair_plan);
    }
    const GrammarApplyResult pair_apply =
        apply_pair_grammar_commit_plan(state, pair_plan);
    if (!pair_apply.applied()) {
      result.steps.push_back(pair_step);
      state.stage = GrammarStage::kError;
      return reject_apply(result, pair_apply);
    }
    finish_step_after_apply(pair_step, state);
    result.steps.push_back(pair_step);

    state.stage = GrammarStage::kMacroRunFold;
    while (state.stage == GrammarStage::kMacroRunFold) {
      if (++rounds > config.max_rounds) {
        result.stop_reason = GrammarEngineStopReason::kRoundLimitExceeded;
        state.stage = GrammarStage::kError;
        return result;
      }
      const GrammarRoundResult macro_round =
          run_native_macro_run_readonly_round(state);
      GrammarEngineStep macro_step =
          make_step(state, GrammarStage::kMacroRunFold, macro_round);
      if (macro_round.status == GrammarRoundStatus::kStop) {
        result.steps.push_back(macro_step);
        state.stage = GrammarStage::kPairGrammar;
        break;
      }

      const GrammarSnapshot macro_snapshot = freeze_grammar_snapshot(state);
      const GrammarCommitPlan macro_plan =
          build_native_macro_run_commit_plan(macro_snapshot,
                                             macro_round.action);
      if (!macro_plan.valid()) {
        result.steps.push_back(macro_step);
        state.stage = GrammarStage::kError;
        return reject_commit(result, macro_plan);
      }
      const GrammarApplyResult macro_apply =
          apply_native_macro_run_commit_plan(state, macro_plan);
      if (!macro_apply.applied()) {
        result.steps.push_back(macro_step);
        state.stage = GrammarStage::kError;
        return reject_apply(result, macro_apply);
      }
      finish_step_after_apply(macro_step, state);
      result.steps.push_back(macro_step);
    }
  }

  result.stop_reason = GrammarEngineStopReason::kDone;
  return result;
}

}  // namespace traceloom
