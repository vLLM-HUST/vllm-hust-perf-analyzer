#include "traceloom/compat/anchor_sequence_rows.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "traceloom/compat/timeline_rows.h"

namespace traceloom::compat {
namespace {

double ns_to_us(std::int64_t ns) {
  return static_cast<double>(ns) / 1000.0;
}

std::string symbol_value_or_empty(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

const char* anchor_role_name(AnchorKind kind) {
  switch (kind) {
    case AnchorKind::kDeviceEvent:
      return "compute";
    case AnchorKind::kCommunication:
      return "comm";
    case AnchorKind::kSynchronization:
      return "sync";
    case AnchorKind::kGraphReplayUnit:
    case AnchorKind::kGraphH:
    case AnchorKind::kGraphL:
    case AnchorKind::kGraphT:
      return "graph";
    case AnchorKind::kUnknown:
      return "unknown";
  }
  return "unknown";
}

}  // namespace

std::string anchor_compat_id(AnchorId id) {
  if (!id.valid()) {
    throw std::invalid_argument("AnchorId is invalid");
  }
  return "anchor-" + std::to_string(id.value());
}

std::vector<AnchorSqlRow> build_anchor_sequence_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx) {
  std::vector<AnchorSqlRow> rows;
  rows.reserve(ir.anchors.size());

  for (const AnchorRow& anchor : ir.anchors.rows()) {
    if (anchor.trace_event_id.valid() &&
        anchor.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument("AnchorRow trace_event_id is out of range");
    }

    AnchorSqlRow row;
    row.anchor_id = anchor_compat_id(anchor.id);
    row.db_idx = db_idx;
    row.device_id = anchor.device_id;
    row.anchor_idx = anchor.id.value() + 1;
    row.event_id = anchor.trace_event_id.valid()
                       ? trace_event_compat_id(anchor.trace_event_id)
                       : std::string();
    row.step_idx = anchor.trace_event_id.valid() ? anchor.trace_event_id.value()
                                                 : anchor.id.value();
    row.symbol = symbol_value_or_empty(ir, anchor.symbol_id);
    row.role = anchor_role_name(anchor.kind);
    row.label = row.symbol;
    row.family = row.role;
    row.start_ns = anchor.start_ns;
    row.end_ns = anchor.end_ns;
    row.dur_us = ns_to_us(anchor.end_ns - anchor.start_ns);
    rows.push_back(std::move(row));
  }

  return rows;
}

}  // namespace traceloom::compat
