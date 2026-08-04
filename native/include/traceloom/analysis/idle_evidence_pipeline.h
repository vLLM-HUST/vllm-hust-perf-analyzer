#pragma once

#include "traceloom/analysis/idle_explanation.h"
#include "traceloom/analysis/idle_evidence_semantic_rules.h"
#include "traceloom/analysis/productive_timeline.h"
#include "traceloom/analysis/semantic_task_classifier.h"
#include "traceloom/analysis/stream_state_timeline.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom {

// One authoritative E1 -> E4 composition used by production reports, audit
// tooling, and future sidecar materialization. Keeping the intermediate
// results makes every promoted explanation auditable back through its
// classification, productive gap, and per-stream state evidence.
struct IdleEvidencePipelineOptions {
  ProductiveTimelineOptions productive_timeline;
  IdleExplanationOptions idle_explanation;
};

struct IdleEvidencePipelineResult {
  struct Timing {
    double classify_ms = 0.0;
    double productive_timeline_ms = 0.0;
    double stream_states_ms = 0.0;
    double idle_explanations_ms = 0.0;
  } timing;
  SemanticTaskClassificationResult classification;
  ProductiveTimelineRunResult productive_timeline;
  StreamStateRunResult stream_states;
  IdleExplanationRunResult idle_explanations;
};

IdleEvidencePipelineResult run_idle_evidence_pipeline(
    const NativeIr& ir,
    const SemanticTaskRuleset& ruleset,
    const IdleEvidencePipelineOptions& options = {});

}  // namespace traceloom
