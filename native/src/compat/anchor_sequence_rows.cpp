#include "traceloom/compat/anchor_sequence_rows.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "traceloom/compat/timeline_rows.h"
#include "sqlite_support.h"

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

void write_anchor_structural_order(const std::string& sqlite_path,
                                   const NativeIr& ir, bool requested) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path, {structural_order_table_schema()});
  SqliteDb db(sqlite_path);
  db.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_structural_order_position "
          "ON traceloom_structural_order(structural_idx)");
  db.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_structural_order_anchor "
          "ON traceloom_structural_order(anchor_id)");
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_structural_order");
    SqliteStmt stmt(db.get(), "INSERT INTO traceloom_structural_order VALUES(?,?,?,?,?,?,?)");
    for (const auto& token : ir.tokens.rows()) {
      bind_int64(stmt, 1, token.sequence_index + 1);
      bind_text(stmt, 2, anchor_compat_id(token.anchor_id));
      bind_text(stmt, 3, token.order_runtime_call_id.valid() ? "host_launch_single_thread"
                           : requested ? "device_fallback" : "device");
      if (token.order_runtime_call_id.valid()) {
        const auto& call = ir.runtime_calls.row(token.order_runtime_call_id);
        const auto& source = ir.source_refs.row(call.source_ref_id);
        bind_text(stmt, 4, source.source_path);
        bind_text(stmt, 5, source.table_name);
        bind_int64(stmt, 6, call.source_row_id);
        bind_int64(stmt, 7, call.start_ns);
      }
      if (sqlite3_step(stmt.get()) != SQLITE_DONE)
        throw std::runtime_error(sqlite3_errmsg(db.get()));
      sqlite3_reset(stmt.get());
      sqlite3_clear_bindings(stmt.get());
    }
    db.exec("COMMIT");
  } catch (...) { db.exec("ROLLBACK"); throw; }
#else
  (void)sqlite_path; (void)ir; (void)requested;
#endif
}

}  // namespace traceloom::compat
