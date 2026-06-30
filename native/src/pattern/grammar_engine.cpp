#include "traceloom/pattern/grammar_engine.h"

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

  state.stage = GrammarStage::kRunFold;
  while (true) {
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

  if (state.live_node_count > config.full_discovery_cap) {
    state.stage = GrammarStage::kDone;
    result.stop_reason =
        GrammarEngineStopReason::kSequenceTooLargeForFullPairDiscovery;
    return result;
  }

  state.stage = GrammarStage::kPairGrammar;
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
      result.steps.push_back(pair_step);
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
