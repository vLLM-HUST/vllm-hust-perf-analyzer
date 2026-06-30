#pragma once

#include <vector>

#include "traceloom/pattern/grammar_commit_plan.h"
#include "traceloom/pattern/grammar_state.h"

namespace traceloom {

enum class GrammarApplyStatus {
  kApplied,
  kRejected,
};

enum class GrammarApplyDiagnosticCode {
  kInvalidCommitPlan,
  kStaleStateGeneration,
  kCommitPlanRevalidationFailed,
  kProtectedIntervalMappingFailed,
  kNoProgress,
};

struct GrammarApplyDiagnostic {
  GrammarApplyDiagnosticCode code = GrammarApplyDiagnosticCode::kInvalidCommitPlan;
};

struct GrammarApplyResult {
  GrammarApplyStatus status = GrammarApplyStatus::kRejected;
  std::vector<GrammarApplyDiagnostic> diagnostics;

  bool applied() const noexcept {
    return status == GrammarApplyStatus::kApplied && diagnostics.empty();
  }
};

const char* grammar_apply_diagnostic_code_name(
    GrammarApplyDiagnosticCode code);

GrammarApplyResult apply_adjacent_run_commit_plan(
    GlobalGrammarState& state,
    const GrammarCommitPlan& plan);

GrammarApplyResult apply_pair_grammar_commit_plan(
    GlobalGrammarState& state,
    const GrammarCommitPlan& plan);

GrammarApplyResult apply_native_macro_run_commit_plan(
    GlobalGrammarState& state,
    const GrammarCommitPlan& plan);

}  // namespace traceloom
