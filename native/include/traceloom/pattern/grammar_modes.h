#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace traceloom {

enum class GrammarAlgorithmMode {
  kAnalysisQualityV1,
  kPythonCompatWithoutRepeatedBlock,
  kPythonCompatFull,
};

struct GrammarAlgorithmMetadata {
  GrammarAlgorithmMode mode = GrammarAlgorithmMode::kAnalysisQualityV1;
  std::size_t full_discovery_cap = 50000;
  std::vector<std::string> producer_sequence;
  std::vector<std::string> known_deltas;
};

const char* grammar_algorithm_mode_name(GrammarAlgorithmMode mode);

GrammarAlgorithmMetadata default_grammar_metadata(
    GrammarAlgorithmMode mode = GrammarAlgorithmMode::kAnalysisQualityV1);

// Whether the native engine executes the exact repeated-block producer for
// the given algorithm mode. kPythonCompatWithoutRepeatedBlock is the only
// mode that must not execute it (and keeps its declared known delta).
bool grammar_mode_enables_exact_repeated_block(GrammarAlgorithmMode mode);

}  // namespace traceloom
