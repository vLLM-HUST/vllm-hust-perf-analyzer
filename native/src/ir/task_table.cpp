#include "traceloom/ir/task_table.h"

#include <stdexcept>

namespace traceloom {

TaskId TaskTable::append(SourceRefId source_ref_id,
                         TraceEventId trace_event_id,
                         std::uint64_t raw_task_id,
                         std::int64_t raw_global_task_id,
                         std::int64_t raw_connection_id,
                         SymbolId task_type_symbol_id,
                         SymbolId op_name_symbol_id,
                         SymbolId op_type_symbol_id,
                         SymbolId compute_task_type_symbol_id,
                         SymbolId comm_name_symbol_id,
                         std::int64_t raw_model_id,
                         SymbolId communication_task_type_symbol_id,
                         std::int64_t raw_context_id) {
  const auto id = checked_next_id<TaskId>(rows_.size());
  rows_.push_back(TaskRow{id, source_ref_id, trace_event_id, raw_task_id,
                          raw_global_task_id, raw_connection_id, raw_context_id,
                          task_type_symbol_id, op_name_symbol_id,
                          op_type_symbol_id, compute_task_type_symbol_id,
                          comm_name_symbol_id, raw_model_id,
                          communication_task_type_symbol_id});
  return id;
}

const TaskRow& TaskTable::row(TaskId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("TaskId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
