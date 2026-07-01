#include "traceloom/compat/timeline_rows.h"

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace traceloom::compat {
namespace {

double ns_to_us(std::int64_t ns) {
  return static_cast<double>(ns) / 1000.0;
}

std::string symbol_value_or_empty(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

std::string source_key_for_event(const TraceEventRow& event) {
  return std::to_string(event.source_row_id);
}

SymbolId choose_task_symbol(const TaskRow& task,
                            const TraceEventRow& event) {
  if (task.op_type_symbol_id.valid()) {
    return task.op_type_symbol_id;
  }
  if (task.op_name_symbol_id.valid()) {
    return task.op_name_symbol_id;
  }
  if (task.comm_name_symbol_id.valid()) {
    return task.comm_name_symbol_id;
  }
  if (task.task_type_symbol_id.valid()) {
    return task.task_type_symbol_id;
  }
  return event.raw_name_symbol_id;
}

std::unordered_map<TraceEventId::value_type, const TaskRow*> tasks_by_event(
    const NativeIr& ir) {
  std::unordered_map<TraceEventId::value_type, const TaskRow*> out;
  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.trace_event_id.valid() ||
        task.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument("TaskRow trace_event_id is out of range");
    }
    out.emplace(task.trace_event_id.value(), &task);
  }
  return out;
}

std::unordered_map<TraceEventId::value_type, const CommunicationOpRow*>
communication_ops_by_event(const NativeIr& ir) {
  std::unordered_map<TraceEventId::value_type, const CommunicationOpRow*> out;
  for (const CommunicationOpRow& comm : ir.communication_ops.rows()) {
    if (!comm.trace_event_id.valid() ||
        comm.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument(
          "CommunicationOpRow trace_event_id is out of range");
    }
    out.emplace(comm.trace_event_id.value(), &comm);
  }
  return out;
}

}  // namespace

std::string trace_event_compat_id(TraceEventId id) {
  if (!id.valid()) {
    throw std::invalid_argument("TraceEventId is invalid");
  }
  return "event-" + std::to_string(id.value());
}

EventSqlRows build_timeline_sql_rows(const NativeIr& ir, std::uint32_t db_idx) {
  const auto task_index = tasks_by_event(ir);
  const auto comm_index = communication_ops_by_event(ir);

  EventSqlRows rows;
  rows.events.reserve(ir.trace_events.size());
  rows.event_sources.reserve(ir.trace_events.size());

  for (const TraceEventRow& event : ir.trace_events.rows()) {
    const SourceRefRow& source = ir.source_refs.row(event.source_ref_id);
    const auto task_found = task_index.find(event.id.value());
    const TaskRow* task =
        task_found == task_index.end() ? nullptr : task_found->second;
    const auto comm_found = comm_index.find(event.id.value());
    const CommunicationOpRow* comm =
        comm_found == comm_index.end() ? nullptr : comm_found->second;

    EventSqlRow row;
    row.event_id = trace_event_compat_id(event.id);
    row.db_idx = db_idx;
    row.device_id = event.device_id;
    row.step_idx = event.id.value();
    row.source_table = source.table_name;
    row.source_key = source_key_for_event(event);
    row.stream_id = event.stream_id;
    row.start_ns = event.start_ns;
    row.end_ns = event.end_ns;
    row.dur_us = ns_to_us(event.end_ns - event.start_ns);
    row.raw_label = symbol_value_or_empty(ir, event.raw_name_symbol_id);

    if (task != nullptr) {
      row.symbol = symbol_value_or_empty(ir, choose_task_symbol(*task, event));
      row.label = row.symbol;
      row.op_type = symbol_value_or_empty(ir, task->op_type_symbol_id);
      row.compute_task_type =
          symbol_value_or_empty(ir, task->compute_task_type_symbol_id);
      row.task_type = symbol_value_or_empty(ir, task->task_type_symbol_id);
      const bool is_comm_task = task->comm_name_symbol_id.valid();
      row.role = is_comm_task ? "comm" : "compute";
      row.category = row.role;
      row.family = row.role;
    } else if (comm != nullptr) {
      row.symbol = symbol_value_or_empty(ir, comm->op_name_symbol_id);
      if (row.symbol.empty()) {
        row.symbol = row.raw_label;
      }
      row.label = row.symbol;
      row.role = "comm";
      row.category = "comm";
      row.family = "comm";
    } else {
      row.symbol = row.raw_label;
      row.label = row.symbol;
      row.role = "event";
      row.category = "event";
      row.family = "event";
    }
    rows.events.push_back(std::move(row));

    EventSourceSqlRow source_row;
    source_row.event_id = rows.events.back().event_id;
    source_row.source_ordinal = 0;
    source_row.db_idx = db_idx;
    source_row.device_id = event.device_id;
    source_row.source_table = source.table_name;
    source_row.source_key = source_key_for_event(event);
    source_row.source_role = "primary";
    rows.event_sources.push_back(std::move(source_row));
  }

  return rows;
}

std::vector<EventSqlRow> split_timeline_event_sql_rows(
    const EventSqlRows& rows) {
  return rows.events;
}

std::vector<EventSourceSqlRow> split_source_lineage_sql_rows(
    const EventSqlRows& rows) {
  return rows.event_sources;
}

}  // namespace traceloom::compat
