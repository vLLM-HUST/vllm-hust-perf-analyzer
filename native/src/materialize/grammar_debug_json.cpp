#include "traceloom/materialize/grammar_debug_json.h"

#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "traceloom/pattern/grammar_round.h"

namespace traceloom {
namespace {

void write_json_string(std::ostream& out, const std::string& value) {
  out << '"';
  for (const char ch : value) {
    switch (ch) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(static_cast<unsigned char>(ch)) << std::dec
              << std::setfill(' ');
        } else {
          out << ch;
        }
        break;
    }
  }
  out << '"';
}

const char* grammar_stage_name(GrammarStage stage) {
  switch (stage) {
    case GrammarStage::kInit:
      return "init";
    case GrammarStage::kRunFold:
      return "run_fold";
    case GrammarStage::kPairGrammar:
      return "pair_grammar";
    case GrammarStage::kMacroRunFold:
      return "macro_run_fold";
    case GrammarStage::kDone:
      return "done";
    case GrammarStage::kError:
      return "error";
  }
  return "unknown";
}

const char* grammar_round_status_name(GrammarRoundStatus status) {
  switch (status) {
    case GrammarRoundStatus::kStop:
      return "stop";
    case GrammarRoundStatus::kActionSelected:
      return "action_selected";
    case GrammarRoundStatus::kError:
      return "error";
  }
  return "unknown";
}

const char* grammar_action_kind_name(GrammarActionKind kind) {
  switch (kind) {
    case GrammarActionKind::kReplaceExactRuns:
      return "replace_exact_runs";
    case GrammarActionKind::kReplacePair:
      return "replace_pair";
    case GrammarActionKind::kCompressMaximalRuns:
      return "compress_maximal_runs";
    case GrammarActionKind::kReplaceRepeatedBlock:
      return "replace_repeated_block";
  }
  return "unknown";
}

const char* macro_level_name(MacroLevel level) {
  switch (level) {
    case MacroLevel::kRP:
      return "RP";
    case MacroLevel::kLP:
      return "LP";
    case MacroLevel::kSemantic:
      return "semantic";
  }
  return "unknown";
}

const char* boundary_violation_kind_name(BoundaryViolationKind kind) {
  switch (kind) {
    case BoundaryViolationKind::kNone:
      return "none";
    case BoundaryViolationKind::kCrossesNoCrossBoundary:
      return "crosses_no_cross_boundary";
    case BoundaryViolationKind::kEnclosesNoCrossInterval:
      return "encloses_no_cross_interval";
    case BoundaryViolationKind::kAmbiguousIntervalBlocksCandidate:
      return "ambiguous_interval_blocks_candidate";
  }
  return "unknown";
}

std::string symbol_name(const SymbolTable& symbols, SymbolId symbol_id) {
  if (!symbol_id.valid()) {
    return "<invalid>";
  }
  if (symbol_id.value() < symbols.size()) {
    return symbols.value(symbol_id);
  }
  std::ostringstream out;
  out << "<macro:" << symbol_id.value() << ">";
  return out.str();
}

void write_string_array(std::ostream& out,
                        const std::vector<std::string>& values,
                        const char* item_indent,
                        const char* close_indent) {
  out << "[\n";
  for (std::size_t index = 0; index < values.size(); ++index) {
    out << item_indent;
    write_json_string(out, values[index]);
    if (index + 1 < values.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << close_indent << "]";
}

void write_symbol_ref(std::ostream& out,
                      const SymbolTable& symbols,
                      SymbolId symbol_id) {
  out << "{\"id\": ";
  if (symbol_id.valid()) {
    out << symbol_id.value();
  } else {
    out << "null";
  }
  out << ", \"name\": ";
  write_json_string(out, symbol_name(symbols, symbol_id));
  out << "}";
}

void write_macro_defs(std::ostream& out,
                      const SymbolTable& symbols,
                      const std::vector<MacroDefRow>& macro_defs) {
  out << "  \"macro_defs\": [\n";
  for (std::size_t index = 0; index < macro_defs.size(); ++index) {
    const MacroDefRow& row = macro_defs[index];
    out << "    {\n";
    out << "      \"macro_def_id\": " << row.id.value() << ",\n";
    out << "      \"symbol\": ";
    write_symbol_ref(out, symbols, row.symbol_id);
    out << ",\n";
    out << "      \"level\": ";
    write_json_string(out, macro_level_name(row.level));
    out << ",\n";
    out << "      \"definition_len\": " << row.definition_len << ",\n";
    out << "      \"replace_count\": " << row.replace_count << ",\n";
    out << "      \"gain\": " << row.gain << ",\n";
    out << "      \"first_pos\": " << row.first_pos << ",\n";
    out << "      \"rhs\": [";
    for (std::size_t rhs_index = 0; rhs_index < row.rhs_symbols.size();
         ++rhs_index) {
      if (rhs_index != 0) {
        out << ", ";
      }
      write_symbol_ref(out, symbols, row.rhs_symbols[rhs_index]);
    }
    out << "]\n";
    out << "    }";
    if (index + 1 < macro_defs.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ]";
}

void write_steps(std::ostream& out,
                 const std::vector<GrammarEngineStep>& steps) {
  out << "  \"steps\": [\n";
  for (std::size_t index = 0; index < steps.size(); ++index) {
    const GrammarEngineStep& step = steps[index];
    out << "    {\n";
    out << "      \"index\": " << index << ",\n";
    out << "      \"stage\": ";
    write_json_string(out, grammar_stage_name(step.stage));
    out << ",\n";
    out << "      \"producer\": ";
    write_json_string(out, grammar_producer_id_name(step.producer_id));
    out << ",\n";
    out << "      \"status\": ";
    write_json_string(out, grammar_round_status_name(step.round_status));
    out << ",\n";
    out << "      \"action\": ";
    write_json_string(out, grammar_action_kind_name(step.action_kind));
    out << ",\n";
    out << "      \"before_generation\": " << step.before_generation << ",\n";
    out << "      \"after_generation\": " << step.after_generation << ",\n";
    out << "      \"before_live_node_count\": "
        << step.before_live_node_count << ",\n";
    out << "      \"after_live_node_count\": "
        << step.after_live_node_count << ",\n";
    out << "      \"gain\": " << step.gain << ",\n";
    out << "      \"replace_count\": " << step.replace_count << "\n";
    out << "    }";
    if (index + 1 < steps.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ]";
}

void write_commit_diagnostics(
    std::ostream& out,
    const std::vector<GrammarCommitDiagnostic>& diagnostics) {
  out << "  \"commit_diagnostics\": [\n";
  for (std::size_t index = 0; index < diagnostics.size(); ++index) {
    const GrammarCommitDiagnostic& diagnostic = diagnostics[index];
    out << "    {\"index\": " << index << ", \"code\": ";
    write_json_string(out,
                      grammar_commit_diagnostic_code_name(diagnostic.code));
    out << ", \"occurrence_index\": " << diagnostic.occurrence_index
        << ", \"protected_interval_id\": ";
    if (diagnostic.protected_interval_id.valid()) {
      out << diagnostic.protected_interval_id.value();
    } else {
      out << "null";
    }
    out << ", \"boundary_violation\": ";
    write_json_string(out, boundary_violation_kind_name(
                               diagnostic.boundary_violation_kind));
    out << "}";
    if (index + 1 < diagnostics.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ]";
}

void write_apply_diagnostics(
    std::ostream& out,
    const std::vector<GrammarApplyDiagnostic>& diagnostics) {
  out << "  \"apply_diagnostics\": [\n";
  for (std::size_t index = 0; index < diagnostics.size(); ++index) {
    const GrammarApplyDiagnostic& diagnostic = diagnostics[index];
    out << "    {\"index\": " << index << ", \"code\": ";
    write_json_string(out, grammar_apply_diagnostic_code_name(diagnostic.code));
    out << "}";
    if (index + 1 < diagnostics.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ]";
}

void write_final_sequence(std::ostream& out,
                          const SymbolTable& symbols,
                          const std::vector<GrammarNode>& nodes) {
  out << "  \"final_sequence\": [\n";
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const GrammarNode& node = nodes[index];
    out << "    {\"index\": " << index << ", \"node_id\": "
        << node.id.value() << ", \"symbol\": ";
    write_symbol_ref(out, symbols, node.symbol_id);
    out << ", \"macro_def_id\": ";
    if (node.macro_def_id.valid()) {
      out << node.macro_def_id.value();
    } else {
      out << "null";
    }
    out << ", \"source_begin\": " << node.source_begin_token_index
        << ", \"source_end\": " << node.source_end_token_index_exclusive
        << "}";
    if (index + 1 < nodes.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ]";
}

}  // namespace

void write_grammar_debug_json(std::ostream& out,
                              const SymbolTable& symbols,
                              const GlobalGrammarState& state,
                              const GrammarEngineResult& result,
                              const GrammarDebugJsonOptions& options) {
  out << "{\n";
  out << "  \"schema_version\": \"native_grammar_debug_v1\",\n";
  out << "  \"algorithm\": {\n";
  out << "    \"mode\": ";
  write_json_string(out, grammar_algorithm_mode_name(state.metadata.mode));
  out << ",\n";
  out << "    \"full_discovery_cap\": "
      << state.metadata.full_discovery_cap << ",\n";
  out << "    \"producer_sequence\": ";
  write_string_array(out, state.metadata.producer_sequence, "      ", "    ");
  out << ",\n";
  out << "    \"known_deltas\": ";
  write_string_array(out, state.metadata.known_deltas, "      ", "    ");
  out << "\n";
  out << "  },\n";
  out << "  \"state\": {\n";
  out << "    \"stage\": ";
  write_json_string(out, grammar_stage_name(state.stage));
  out << ",\n";
  out << "    \"generation\": " << state.generation << ",\n";
  out << "    \"live_node_count\": " << state.live_node_count << ",\n";
  out << "    \"target_nodes_per_chunk\": "
      << state.target_nodes_per_chunk << ",\n";
  out << "    \"worker_count\": " << state.worker_count << ",\n";
  out << "    \"chunk_count\": " << state.chunks.size() << ",\n";
  out << "    \"macro_def_count\": " << state.macro_defs.size() << "\n";
  out << "  },\n";
  out << "  \"engine\": {\n";
  out << "    \"stop_reason\": ";
  write_json_string(out, grammar_engine_stop_reason_name(result.stop_reason));
  out << ",\n";
  out << "    \"ok\": " << (result.ok() ? "true" : "false") << ",\n";
  out << "    \"max_rounds\": " << options.engine_max_rounds << ",\n";
  out << "    \"step_count\": " << result.steps.size() << ",\n";
  out << "    \"commit_diagnostic_count\": "
      << result.commit_diagnostics.size() << ",\n";
  out << "    \"apply_diagnostic_count\": "
      << result.apply_diagnostics.size() << "\n";
  out << "  },\n";
  write_steps(out, result.steps);
  out << ",\n";
  write_commit_diagnostics(out, result.commit_diagnostics);
  out << ",\n";
  write_apply_diagnostics(out, result.apply_diagnostics);
  out << ",\n";
  write_macro_defs(out, symbols, state.macro_defs);
  if (options.include_final_sequence) {
    out << ",\n";
    write_final_sequence(out, symbols, state.nodes);
  }
  out << "\n";
  out << "}\n";
}

}  // namespace traceloom
