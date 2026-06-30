#pragma once

#include <cstddef>
#include <vector>

#include "traceloom/pattern/grammar_apply.h"
#include "traceloom/pattern/grammar_commit_plan.h"
#include "traceloom/pattern/grammar_round.h"
#include "traceloom/pattern/grammar_snapshot.h"
#include "traceloom/pattern/grammar_state.h"

namespace traceloom {

enum class GrammarEngineStopReason {
  kDone,
  kSequenceTooLargeForFullPairDiscovery,
  kRoundLimitExceeded,
  kCommitPlanRejected,
  kApplyRejected,
};

struct GrammarEngineStep {
  GrammarStage stage = GrammarStage::kInit;
  GrammarProducerId producer_id = GrammarProducerId::kAdjacentRun;
  GrammarRoundStatus round_status = GrammarRoundStatus::kStop;
  std::uint64_t before_generation = 0;
  std::uint64_t after_generation = 0;
  std::size_t before_live_node_count = 0;
  std::size_t after_live_node_count = 0;
  GrammarActionKind action_kind = GrammarActionKind::kReplaceExactRuns;
  std::size_t gain = 0;
  std::size_t replace_count = 0;
};

struct GrammarEngineConfig {
  std::size_t full_discovery_cap = 50000;
  std::size_t max_rounds = 10000;
};

struct GrammarEngineResult {
  GrammarEngineStopReason stop_reason = GrammarEngineStopReason::kDone;
  std::vector<GrammarEngineStep> steps;
  std::vector<GrammarCommitDiagnostic> commit_diagnostics;
  std::vector<GrammarApplyDiagnostic> apply_diagnostics;

  bool ok() const noexcept {
    return stop_reason == GrammarEngineStopReason::kDone ||
           stop_reason ==
               GrammarEngineStopReason::kSequenceTooLargeForFullPairDiscovery;
  }
};

const char* grammar_engine_stop_reason_name(
    GrammarEngineStopReason reason);

GrammarEngineResult run_grammar_state_machine(
    GlobalGrammarState& state,
    const GrammarEngineConfig& config = GrammarEngineConfig{});

}  // namespace traceloom
