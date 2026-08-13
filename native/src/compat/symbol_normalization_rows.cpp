#include "traceloom/compat/symbol_normalization_rows.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "traceloom/analysis/structural_symbol_normalization.h"
#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/timeline_rows.h"

namespace traceloom::compat {
namespace {

std::string symbol_value_or_empty(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

}  // namespace

SymbolNormalizationSqlRows build_symbol_normalization_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx) {
  SymbolNormalizationSqlRows rows;
  const StructuralSymbolPolicySnapshot fallback_policy =
      ir.structural_symbol_policy.empty()
          ? load_default_structural_symbol_ruleset().snapshot()
          : StructuralSymbolPolicySnapshot{};
  const StructuralSymbolPolicySnapshot& policy =
      ir.structural_symbol_policy.empty() ? fallback_policy
                                          : ir.structural_symbol_policy;
  rows.policies.push_back(
      {policy.policy_id,
       policy.policy_version,
       policy.policy_kind,
       policy.source_manifest,
       policy.manifest_sha256,
       "Versioned structural comparison symbols; concrete provider identity "
       "and source provenance remain available."});

  for (const StructuralSymbolRuleSnapshot& rule : policy.rules) {
    rows.rules.push_back(
        {policy.policy_id,
         policy.policy_version,
         rule.rule_id,
         rule.priority,
         rule.provider_scope,
         rule.source_domain,
         rule.match_mode,
         rule.pattern,
         rule.structural_symbol,
         rule.required_fields,
         rule.rule_origin,
         rule.rule_origin_sha256,
         rule.source_line,
         rule.note});
  }
  rows.rules.push_back(
      {policy.policy_id, policy.policy_version,
       "analysis.replay-composition-slot", -100, "provider-neutral",
       "analysis", "typed_synthesis", "committed replay composition slot",
       "<role-derived>", "recognized replay composition slot",
       "analysis", "", 0,
       "Name an analyzer-supported replay composition position."});
  rows.rules.push_back(
      {policy.policy_id, policy.policy_version, "fallback.identity-preserve",
       -1000, "provider-neutral", "selected observed symbol",
       "identity_fallback", "no explicit rule matched", "<observed-symbol>",
       "one observed symbol",
       "analysis", "", 0,
       "Preserve unfamiliar symbols rather than guessing equivalence."});
  rows.rules.push_back(
      {policy.policy_id, policy.policy_version,
       "fallback.missing-observed-symbol", -1010, "provider-neutral",
       "selected observed symbol", "typed_unsupported",
       "no observed symbol available", "", "none",
       "analysis", "", 0,
       "Record unsupported structural identity without silent guessing."});
  rows.rules.push_back(
      {policy.policy_id, policy.policy_version, "fallback.rule-conflict",
       -1020, "provider-neutral", "selected observed symbol",
       "typed_conflict", "multiple equal-precedence rules matched",
       "<observed-symbol>", "one observed symbol and multiple rule matches",
       "analysis", "", 0,
       "Preserve the observed symbol when policy rules are ambiguous."});

  rows.decisions.reserve(ir.anchors.size());
  for (const AnchorRow& anchor : ir.anchors.rows()) {
    if (!anchor.source_ref_id.valid() ||
        anchor.source_ref_id.value() >= ir.source_refs.size()) {
      throw std::invalid_argument(
          "AnchorRow source_ref_id is out of range for symbol lineage");
    }
    const SourceRefRow& source = ir.source_refs.row(anchor.source_ref_id);
    std::uint64_t source_row_id = source.row_id;
    std::string event_id;
    if (anchor.trace_event_id.valid()) {
      if (anchor.trace_event_id.value() >= ir.trace_events.size()) {
        throw std::invalid_argument(
            "AnchorRow trace_event_id is out of range for symbol lineage");
      }
      const TraceEventRow& event = ir.trace_events.row(anchor.trace_event_id);
      source_row_id = event.source_row_id;
      event_id = trace_event_compat_id(anchor.trace_event_id);
    }

    AnchorSymbolNormalizationSqlRow row;
    row.anchor_id = anchor_compat_id(anchor.id);
    row.db_idx = db_idx;
    row.device_id = anchor.device_id;
    row.anchor_idx = anchor.id.value() + 1;
    row.event_id = std::move(event_id);
    row.source_path = source.source_path;
    row.source_table = source.table_name;
    row.source_key = std::to_string(source_row_id);
    row.observed_symbol =
        symbol_value_or_empty(ir, anchor.symbol_decision.observed_symbol_id);
    row.observed_symbol_source =
        structural_symbol_source_name(anchor.symbol_decision.observed_source);
    row.structural_symbol = symbol_value_or_empty(ir, anchor.symbol_id);
    row.policy_id = policy.policy_id;
    row.policy_version = policy.policy_version;
    row.rule_id = anchor.symbol_decision.rule_id;
    row.candidate_rule_ids = anchor.symbol_decision.candidate_rule_ids;
    row.outcome =
        structural_symbol_outcome_name(anchor.symbol_decision.outcome);
    row.reason_code =
        structural_symbol_reason_code(anchor.symbol_decision.outcome);
    rows.decisions.push_back(std::move(row));
  }
  return rows;
}

}  // namespace traceloom::compat
