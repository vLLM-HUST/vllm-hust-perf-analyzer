#include "traceloom/analysis/replay_body_pattern.h"

#include <algorithm>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "traceloom/analysis/structural_occurrence_builder.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/pattern/grammar_state.h"

namespace traceloom {
namespace {

using DomainTuple =
    std::tuple<GraphTemplateId::value_type, std::uint32_t, std::uint32_t,
               std::uint32_t, ReplayBodyTemplateId::value_type, std::uint32_t>;

DomainTuple domain_tuple(const ReplayAlignedCostAggregateRow& row) {
  return {row.graph_template_id.value(), row.device_id,
          static_cast<std::uint32_t>(row.slot_role),
          static_cast<std::uint32_t>(row.aggregation_scope),
          row.replay_body_template_id.value(), row.stream_id};
}

ReplayBodyPatternDomainKey domain_key(const ReplayAlignedCostAggregateRow& row) {
  ReplayBodyPatternDomainKey key;
  key.graph_template_id = row.graph_template_id;
  key.device_id = row.device_id;
  key.slot_role = row.slot_role;
  key.aggregation_scope = row.aggregation_scope;
  key.replay_body_template_id = row.replay_body_template_id;
  key.stream_id = row.stream_id;
  return key;
}

void add_issue(ReplayBodyPatternResult& result,
               std::size_t domain_index,
               std::string code,
               std::string detail) {
  result.issues.push_back(ReplayBodyPatternIssue{
      std::move(code), domain_index, std::move(detail)});
}

void reject_domain(ReplayBodyPatternResult& result,
                   std::size_t domain_index,
                   const std::string& reason,
                   const std::string& detail) {
  ReplayBodyPatternDomain& domain = result.domains.at(domain_index);
  domain.support_status = ReplayBodyPatternSupportStatus::kRejected;
  domain.reason_code = reason;
  domain.graph = StructuralOccurrenceGraph{};
  ++result.rejected_domain_count;
  add_issue(result, domain_index, reason, detail);
}

bool validate_domain_positions(const NativeIr& ir,
                               const ReplayInternalCostMapResult& replay_cost,
                               const ReplayBodyPatternDomain& domain,
                               std::string* reason,
                               std::string* detail) {
  if (domain.aggregate_indices.empty()) {
    *reason = "empty_position_sequence";
    *detail = "replay-body domain has no aligned Position rows";
    return false;
  }
  for (std::size_t ordinal = 0; ordinal < domain.aggregate_indices.size();
       ++ordinal) {
    const std::size_t aggregate_index = domain.aggregate_indices[ordinal];
    if (aggregate_index >= replay_cost.aggregates.size()) {
      *reason = "invalid_aggregate_index";
      *detail = "replay-body domain references an out-of-range aggregate";
      return false;
    }
    const ReplayAlignedCostAggregateRow& row =
        replay_cost.aggregates[aggregate_index];
    if (row.within_stream_position != ordinal) {
      *reason = "non_dense_position_sequence";
      *detail = "replay-body Position ordinals are not dense and zero-based";
      return false;
    }
    if (!row.graph_template_id.valid() ||
        !row.replay_body_template_id.valid() ||
        !row.identity_symbol_id.valid() ||
        row.identity_symbol_id.value() >= ir.symbols.size()) {
      *reason = "invalid_position_identity";
      *detail = "replay-body Position lacks a valid exact identity";
      return false;
    }
    if (!row.kind_consistent || !row.lane_consistent ||
        !row.distribution_supported) {
      *reason = "inconsistent_position_evidence";
      *detail = "replay-body Position has inconsistent kind, lane, or cost "
                "distribution evidence";
      return false;
    }
    if (domain_tuple(row) != domain_tuple(
            replay_cost.aggregates[domain.aggregate_indices.front()])) {
      *reason = "mixed_position_domain";
      *detail = "replay-body Position sequence mixes structural domains";
      return false;
    }
  }
  return true;
}

std::string exact_symbol(const NativeIr& ir,
                         const ReplayAlignedCostAggregateRow& row) {
  // The separator is deliberately outside the user-facing identity surface.
  // It merely prevents an identity observed with two member kinds from
  // aliasing inside the grammar alphabet.
  return ir.symbols.value(row.identity_symbol_id) + "\x1f" +
         replay_internal_cost_map_member_kind_name(row.kind);
}

void recover_domain(const NativeIr& ir,
                    const ReplayInternalCostMapResult& replay_cost,
                    const ReplayBodyPatternConfig& config,
                    ReplayBodyPatternDomain& domain) {
  NativeIr grammar_ir;
  std::vector<StructuralProjectionToken> tokens;
  tokens.reserve(domain.aggregate_indices.size());
  for (std::size_t ordinal = 0; ordinal < domain.aggregate_indices.size();
       ++ordinal) {
    const ReplayAlignedCostAggregateRow& row =
        replay_cost.aggregates[domain.aggregate_indices[ordinal]];
    const SymbolId symbol = grammar_ir.symbols.intern(exact_symbol(ir, row));
    grammar_ir.tokens.append(
        AnchorId::invalid(), symbol, row.device_id,
        static_cast<std::uint32_t>(ordinal),
        static_cast<std::int64_t>(ordinal),
        static_cast<std::int64_t>(ordinal + 1));

    StructuralProjectionToken token;
    token.ordinal = static_cast<std::uint32_t>(ordinal);
    token.device_id = row.device_id;
    token.symbol_id = symbol;
    token.display_op = ir.symbols.value(row.identity_symbol_id);
    token.display_category =
        replay_internal_cost_map_member_kind_name(row.kind);
    token.anchor_kind = StructuralAnchorKind::kExec;
    token.anchor_id = AnchorId::invalid();
    token.start_ns = static_cast<std::int64_t>(ordinal);
    token.end_ns = static_cast<std::int64_t>(ordinal + 1);
    tokens.push_back(std::move(token));
  }

  GrammarStateConfig state_config;
  state_config.mode = config.grammar_mode;
  state_config.target_nodes_per_chunk = config.target_nodes_per_chunk;
  state_config.worker_count = config.worker_count;
  state_config.full_discovery_cap = config.full_discovery_cap;
  GlobalGrammarState state =
      build_initial_grammar_state(grammar_ir, state_config);

  GrammarEngineConfig engine_config;
  engine_config.full_discovery_cap = config.full_discovery_cap;
  engine_config.max_rounds = config.max_rounds;
  const GrammarEngineResult engine =
      run_grammar_state_machine(state, engine_config);
  domain.grammar_mode = config.grammar_mode;
  domain.grammar_stop_reason = engine.stop_reason;
  domain.grammar_live_node_count = state.live_node_count;
  domain.grammar_macro_def_count = state.macro_defs.size();
  domain.grammar_step_count = engine.steps.size();

  if (engine.stop_reason != GrammarEngineStopReason::kDone ||
      state.stage != GrammarStage::kDone) {
    throw std::runtime_error(
        std::string("grammar_engine_") +
        grammar_engine_stop_reason_name(engine.stop_reason));
  }
  domain.graph = state.macro_defs.empty()
                     ? build_structural_occurrence_graph_from_tokens(tokens)
                     : build_structural_occurrence_graph_from_grammar_state(
                           tokens, state);
  if (domain.graph.node_defs.empty() || domain.graph.occurrences.empty()) {
    throw std::runtime_error("empty_structural_occurrence_graph");
  }
}

}  // namespace

const char* replay_body_pattern_support_status_name(
    ReplayBodyPatternSupportStatus status) noexcept {
  switch (status) {
    case ReplayBodyPatternSupportStatus::kSupported:
      return "supported";
    case ReplayBodyPatternSupportStatus::kRejected:
      return "rejected";
  }
  return "rejected";
}

ReplayBodyPatternResult build_replay_body_patterns(
    const NativeIr& ir,
    const ReplayInternalCostMapResult& replay_cost,
    const ReplayBodyPatternConfig& config) {
  if (config.target_nodes_per_chunk == 0) {
    throw std::invalid_argument("target_nodes_per_chunk must be nonzero");
  }
  if (config.worker_count == 0) {
    throw std::invalid_argument("worker_count must be nonzero");
  }
  if (config.full_discovery_cap == 0) {
    throw std::invalid_argument("full_discovery_cap must be nonzero");
  }
  if (config.max_rounds == 0) {
    throw std::invalid_argument("max_rounds must be nonzero");
  }

  ReplayBodyPatternResult result;
  std::map<DomainTuple, std::vector<std::size_t>> groups;
  for (std::size_t index = 0; index < replay_cost.aggregates.size(); ++index) {
    groups[domain_tuple(replay_cost.aggregates[index])].push_back(index);
  }

  for (auto& item : groups) {
    std::vector<std::size_t>& indices = item.second;
    std::stable_sort(indices.begin(), indices.end(),
                     [&](std::size_t lhs, std::size_t rhs) {
                       return replay_cost.aggregates[lhs]
                                  .within_stream_position <
                              replay_cost.aggregates[rhs]
                                  .within_stream_position;
                     });
    ReplayBodyPatternDomain domain;
    domain.key = domain_key(replay_cost.aggregates[indices.front()]);
    domain.grammar_mode = config.grammar_mode;
    domain.aggregate_indices = std::move(indices);
    result.domains.push_back(std::move(domain));
  }

  if (result.domains.empty()) {
    result.result_reason_codes = replay_cost.result_reason_codes;
    if (result.result_reason_codes.empty()) {
      result.result_reason_codes.push_back("no_replay_cost_aggregates");
    }
    return result;
  }

  for (std::size_t domain_index = 0; domain_index < result.domains.size();
       ++domain_index) {
    ReplayBodyPatternDomain& domain = result.domains[domain_index];
    std::string reason;
    std::string detail;
    if (!validate_domain_positions(ir, replay_cost, domain, &reason, &detail)) {
      reject_domain(result, domain_index, reason, detail);
      continue;
    }

    try {
      recover_domain(ir, replay_cost, config, domain);
      domain.support_status = ReplayBodyPatternSupportStatus::kSupported;
      domain.reason_code.clear();
      ++result.supported_domain_count;
    } catch (const std::exception& ex) {
      reject_domain(result, domain_index, "grammar_recovery_rejected",
                    ex.what());
    }
  }
  if (result.supported_domain_count == 0) {
    result.result_reason_codes.push_back("no_supported_replay_body_domains");
  }
  return result;
}

}  // namespace traceloom
