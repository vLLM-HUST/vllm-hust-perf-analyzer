#include "traceloom/pattern/grammar_modes.h"

namespace traceloom {

const char* grammar_algorithm_mode_name(GrammarAlgorithmMode mode) {
  switch (mode) {
    case GrammarAlgorithmMode::kAnalysisQualityV1:
      return "analysis_quality_v1";
    case GrammarAlgorithmMode::kPythonCompatWithoutRepeatedBlock:
      return "python_compat_without_repeated_block";
    case GrammarAlgorithmMode::kPythonCompatFull:
      return "python_compat_full";
  }
  return "unknown";
}

GrammarAlgorithmMetadata default_grammar_metadata(
    GrammarAlgorithmMode mode) {
  GrammarAlgorithmMetadata metadata;
  metadata.mode = mode;
  metadata.full_discovery_cap = 50000;
  switch (mode) {
    case GrammarAlgorithmMode::kAnalysisQualityV1:
      // Execution order of the native engine: the exact repeated-block
      // producer runs first (before any adjacent-run or pair compression so
      // raw exact tilings are recognized), then the run fold, then the pair
      // grammar with interleaved macro-run folds. The repeated-block producer
      // additionally re-runs at the pair fixpoint to catch macro-level exact
      // tilings created by compression.
      metadata.producer_sequence = {
          "ExactRepeatedBlockProducer",
          "AdjacentRunProducer",
          "PairGrammarProducer",
          "NativeMacroRunProducer",
      };
      break;
    case GrammarAlgorithmMode::kPythonCompatWithoutRepeatedBlock:
      metadata.producer_sequence = {
          "ExactSymbolRunProducer",
          "PairGrammarProducer",
          "ExactMacroLoopProducer",
      };
      metadata.known_deltas = {
          "generic_repeated_block_skipped_native_v1",
      };
      break;
    case GrammarAlgorithmMode::kPythonCompatFull:
      metadata.producer_sequence = {
          "ExactSymbolRunProducer",
          "ExactRepeatedBlockProducer",
          "PairGrammarProducer",
          "ExactMacroLoopProducer",
      };
      break;
  }
  return metadata;
}

bool grammar_mode_enables_exact_repeated_block(GrammarAlgorithmMode mode) {
  return mode != GrammarAlgorithmMode::kPythonCompatWithoutRepeatedBlock;
}

}  // namespace traceloom
