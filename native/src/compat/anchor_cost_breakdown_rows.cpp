#include "traceloom/compat/anchor_cost_breakdown_rows.h"

#include <map>
#include <stdexcept>
#include <utility>

#include "traceloom/compat/aux_attribution_rows.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

namespace {

double ns_to_us(std::int64_t ns) {
  return static_cast<double>(ns) / 1000.0;
}

std::string symbol_value_or_empty(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

ReportAnchorKind report_anchor_kind_for_anchor_kind(AnchorKind kind) {
  switch (kind) {
    case AnchorKind::kDeviceEvent:
      return ReportAnchorKind::kExec;
    case AnchorKind::kCommunication:
      return ReportAnchorKind::kCollective;
    case AnchorKind::kGraphH:
      return ReportAnchorKind::kGraphH;
    case AnchorKind::kGraphL:
      return ReportAnchorKind::kGraphL;
    case AnchorKind::kGraphT:
      return ReportAnchorKind::kGraphT;
    case AnchorKind::kGraphReplayUnit:
      return ReportAnchorKind::kGraphTemplate;
    case AnchorKind::kSynchronization:
    case AnchorKind::kUnknown:
      return ReportAnchorKind::kUnknown;
  }
  return ReportAnchorKind::kUnknown;
}

std::map<std::uint32_t, double> aux_us_by_anchor_idx(
    const AuxAttributionSqlRows& aux_rows) {
  std::map<std::uint32_t, double> out;
  for (const AnchorAuxSlotSqlRow& slot : aux_rows.aux_slots) {
    out[slot.anchor_idx] += slot.aux_dur_us;
  }
  return out;
}

}  // namespace

std::vector<AnchorCostBreakdownSqlRow> build_anchor_cost_breakdown_sql_rows(
    const AnchorInternalCostBreakdown& breakdown) {
  std::vector<AnchorCostBreakdownSqlRow> rows;
  rows.reserve(breakdown.rows.size());
  for (const AnchorInternalCostBreakdownRow& source : breakdown.rows) {
    AnchorCostBreakdownSqlRow row;
    row.anchor_idx = source.anchor_idx;
    row.symbol = source.symbol;
    row.anchor_kind = report_anchor_kind_name(source.anchor_kind);
    row.total_us = ns_to_us(source.total_ns);
    row.self_us = ns_to_us(source.self_ns);
    row.aux_us = ns_to_us(source.aux_ns);
    row.graph_child_us = ns_to_us(source.graph_child_ns);
    row.residual_us = ns_to_us(source.residual_ns);
    row.raw_child_task_count = source.raw_child_task_count;
    row.top_ops = source.top_ops;
    row.diagnostic_flags = source.diagnostic_flags;
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<AnchorCostBreakdownSqlRow>
build_native_anchor_cost_breakdown_sql_rows(
    const NativeIr& ir,
    const AuxAttributionSqlRows& aux_rows) {
  const std::map<std::uint32_t, double> aux_by_anchor =
      aux_us_by_anchor_idx(aux_rows);

  std::vector<AnchorCostBreakdownSqlRow> rows;
  rows.reserve(ir.anchors.size());
  for (const AnchorRow& anchor : ir.anchors.rows()) {
    if (anchor.end_ns < anchor.start_ns) {
      throw std::invalid_argument("anchor end_ns is before start_ns");
    }
    if (anchor.trace_event_id.valid() &&
        anchor.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument("AnchorRow trace_event_id is out of range");
    }

    AnchorCostBreakdownSqlRow row;
    row.anchor_idx = anchor.id.value() + 1;
    row.symbol = symbol_value_or_empty(ir, anchor.symbol_id);
    row.anchor_kind =
        report_anchor_kind_name(report_anchor_kind_for_anchor_kind(anchor.kind));
    row.self_us = ns_to_us(anchor.end_ns - anchor.start_ns);
    const auto aux_found = aux_by_anchor.find(row.anchor_idx);
    if (aux_found != aux_by_anchor.end()) {
      row.aux_us = aux_found->second;
    }
    row.total_us = row.self_us + row.aux_us;
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<AnchorCostBreakdownSqlRow>
build_native_anchor_cost_breakdown_sql_rows(const NativeIr& ir,
                                            std::uint32_t db_idx) {
  return build_native_anchor_cost_breakdown_sql_rows(
      ir, build_aux_attribution_sql_rows(ir, db_idx));
}

const CompatTableSchema& anchor_cost_breakdown_sql_row_schema() {
  return anchor_cost_breakdown_table_schema();
}

}  // namespace traceloom::compat
