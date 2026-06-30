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
      metadata.producer_sequence = {
          "AdjacentRunProducer",
          "PairGrammarProducer",
          "NativeMacroRunProducer",
      };
      metadata.known_deltas = {
          "generic_repeated_block_skipped_native_v1",
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

}  // namespace traceloom
