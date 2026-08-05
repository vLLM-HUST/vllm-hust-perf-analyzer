#include "traceloom/analysis/idle_evidence_pipeline.h"

#include <chrono>

namespace traceloom {
namespace {

class Stopwatch {
 public:
  Stopwatch() : start_(Clock::now()) {}

  double elapsed_ms() const {
    return std::chrono::duration<double, std::milli>(Clock::now() - start_)
        .count();
  }

 private:
  using Clock = std::chrono::steady_clock;
  Clock::time_point start_;
};

}  // namespace

IdleEvidencePipelineResult run_idle_evidence_pipeline(
    const NativeIr& ir,
    const SemanticTaskRuleset& ruleset,
  const IdleEvidencePipelineOptions& options) {
  IdleEvidencePipelineResult result;
  Stopwatch stage;
  result.classification = classify_semantic_tasks(ir, ruleset);
  result.timing.classify_ms = stage.elapsed_ms();
  stage = Stopwatch();
  result.productive_timeline = build_productive_timelines(
      ir, result.classification, options.productive_timeline);
  result.timing.productive_timeline_ms = stage.elapsed_ms();
  stage = Stopwatch();
  result.stream_states = build_stream_state_timelines(
      ir, result.classification, result.productive_timeline);
  result.timing.stream_states_ms = stage.elapsed_ms();
  stage = Stopwatch();
  result.idle_explanations = build_idle_explanations(
      result.productive_timeline, result.stream_states,
      options.idle_explanation);
  result.timing.idle_explanations_ms = stage.elapsed_ms();
  return result;
}

}  // namespace traceloom
