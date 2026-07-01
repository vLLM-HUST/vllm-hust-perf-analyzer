#include "traceloom/report/anchor_internal_cost_breakdown.h"

#include "traceloom/report/report_tree_cost_handoff.h"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <utility>

namespace traceloom {

namespace {

void add_error(AnchorInternalCostBreakdown& out,
               std::string code,
               std::string message) {
  out.diagnostics.push_back(Diagnostic{
      DiagnosticSeverity::kError,
      std::move(code),
      std::move(message),
  });
}

bool has_errors(const AnchorInternalCostBreakdown& out) {
  return std::any_of(out.diagnostics.begin(), out.diagnostics.end(),
                     [](const Diagnostic& diagnostic) {
                       return diagnostic.severity == DiagnosticSeverity::kError;
                     });
}

void append_field(std::string& target, const std::string& value) {
  if (value.empty()) {
    return;
  }
  if (!target.empty()) {
    target += ";";
  }
  target += value;
}

}  // namespace

AnchorInternalCostBreakdown build_anchor_internal_cost_breakdown(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    const std::vector<AnchorCostComponentLeaf>& component_leaves) {
  AnchorInternalCostBreakdown out;

  std::unordered_map<std::uint32_t, const ReportToken*> token_by_ordinal;
  token_by_ordinal.reserve(tokens.size());
  for (const ReportToken& token : tokens) {
    const auto inserted = token_by_ordinal.emplace(token.ordinal, &token);
    if (!inserted.second) {
      add_error(out, "anchor_cost_duplicate_token_ordinal",
                "report tokens must have unique ordinals");
    }
  }

  std::unordered_map<std::uint32_t, ReportNodeOccurrenceId> atom_by_token;
  std::unordered_map<std::uint32_t, ReportCostHandoffRow> handoff_by_token;
  for (const ReportCostHandoffRow& row : collect_report_cost_handoff_rows(tree)) {
    if (row.token_end_ordinal <= row.token_start_ordinal) {
      add_error(out, "anchor_cost_empty_atom_span",
                "atom cost handoff row has an empty token span");
      continue;
    }
    for (std::uint32_t token = row.token_start_ordinal;
         token < row.token_end_ordinal; ++token) {
      const auto inserted = atom_by_token.emplace(token, row.atom_occurrence_id);
      if (!inserted.second) {
        add_error(out, "anchor_cost_duplicate_atom_owner",
                  "more than one atom occurrence owns the same token ordinal");
      }
      handoff_by_token.emplace(token, row);
    }
  }

  std::unordered_map<std::uint32_t, AnchorInternalCostBreakdownRow*>
      row_by_token;
  out.rows.reserve(handoff_by_token.size());
  for (const auto& entry : handoff_by_token) {
    const std::uint32_t token_ordinal = entry.first;
    const ReportCostHandoffRow& handoff = entry.second;
    const auto token_it = token_by_ordinal.find(token_ordinal);
    if (token_it == token_by_ordinal.end()) {
      add_error(out, "anchor_cost_missing_report_token",
                "atom cost handoff row references a token absent from report "
                "tokens");
      continue;
    }

    const ReportToken& token = *token_it->second;
    AnchorInternalCostBreakdownRow row;
    row.anchor_occurrence_id = handoff.atom_occurrence_id;
    row.anchor_def_id = handoff.node_def_id;
    row.anchor_idx = token.ordinal + 1;
    row.symbol = token.display_op;
    row.anchor_kind = token.anchor_kind;
    out.rows.push_back(std::move(row));
  }

  std::sort(out.rows.begin(), out.rows.end(),
            [](const AnchorInternalCostBreakdownRow& lhs,
               const AnchorInternalCostBreakdownRow& rhs) {
              return lhs.anchor_idx < rhs.anchor_idx;
            });

  for (AnchorInternalCostBreakdownRow& row : out.rows) {
    row_by_token.emplace(row.anchor_idx - 1, &row);
  }

  for (std::size_t i = 0; i < component_leaves.size(); ++i) {
    const AnchorCostComponentLeaf& leaf = component_leaves[i];
    if (!leaf.id.valid() || leaf.id.value() != i) {
      add_error(out, "anchor_cost_invalid_component_leaf_id",
                "anchor cost component leaf ids must be dense and ordered");
      continue;
    }
    if (leaf.duration_ns < 0) {
      add_error(out, "anchor_cost_negative_component_duration",
                "anchor cost component duration cannot be negative");
      continue;
    }
    auto row_it = row_by_token.find(leaf.token_ordinal);
    if (row_it == row_by_token.end()) {
      add_error(out, "anchor_cost_orphan_component_leaf",
                "anchor cost component token ordinal is not covered by an atom "
                "occurrence");
      continue;
    }

    AnchorInternalCostBreakdownRow& row = *row_it->second;
    switch (leaf.kind) {
      case AnchorCostComponentKind::kSelf:
        row.self_ns += leaf.duration_ns;
        break;
      case AnchorCostComponentKind::kAux:
        row.aux_ns += leaf.duration_ns;
        break;
      case AnchorCostComponentKind::kGraphChild:
        row.graph_child_ns += leaf.duration_ns;
        break;
      case AnchorCostComponentKind::kResidual:
        row.residual_ns += leaf.duration_ns;
        break;
    }
    row.raw_child_task_count += leaf.raw_child_task_count;
    row.source_ref_count += leaf.source_ref_count;
    append_field(row.top_ops, leaf.top_ops);
    append_field(row.diagnostic_flags, leaf.diagnostic_flags);
  }

  if (has_errors(out)) {
    out.rows.clear();
    return out;
  }

  for (AnchorInternalCostBreakdownRow& row : out.rows) {
    row.total_ns =
        row.self_ns + row.aux_ns + row.graph_child_ns + row.residual_ns;
  }

  return out;
}

}  // namespace traceloom
