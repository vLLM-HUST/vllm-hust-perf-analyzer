#pragma once

#include <cstddef>
#include <iosfwd>

#include "traceloom/core/string_table.h"
#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/pattern/grammar_state.h"

namespace traceloom {

struct GrammarDebugJsonOptions {
  bool include_final_sequence = true;
  std::size_t engine_max_rounds = 10000;
};

void write_grammar_debug_json(std::ostream& out,
                              const SymbolTable& symbols,
                              const GlobalGrammarState& state,
                              const GrammarEngineResult& result,
                              const GrammarDebugJsonOptions& options =
                                  GrammarDebugJsonOptions{});

}  // namespace traceloom
