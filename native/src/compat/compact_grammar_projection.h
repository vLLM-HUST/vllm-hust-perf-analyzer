#pragma once

#include <cstdint>
#include <vector>

#include "traceloom/analysis/structural_occurrence_graph.h"
#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/pattern/grammar_state.h"

namespace traceloom::compat::detail {

NativeCompactGrammarProjection summarize_compact_grammar(
    const NativeIr& ir,
    const std::vector<StructuralProjectionToken>& tokens,
    const GlobalGrammarState& state,
    const GrammarEngineResult& result,
    std::uint32_t device_id);

}  // namespace traceloom::compat::detail
