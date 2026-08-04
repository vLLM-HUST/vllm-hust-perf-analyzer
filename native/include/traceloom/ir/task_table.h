#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

struct TaskRow {
  TaskId id;
  SourceRefId source_ref_id;
  TraceEventId trace_event_id;
  std::uint64_t raw_task_id = 0;
  std::int64_t raw_global_task_id = -1;
  std::int64_t raw_connection_id = -1;
  SymbolId task_type_symbol_id;
  SymbolId op_name_symbol_id;
  SymbolId op_type_symbol_id;
  SymbolId compute_task_type_symbol_id;
  SymbolId comm_name_symbol_id;
  std::int64_t raw_model_id = -1;
  SymbolId communication_task_type_symbol_id;
};

class TaskTable {
 public:
  TaskId append(SourceRefId source_ref_id,
                TraceEventId trace_event_id,
                std::uint64_t raw_task_id,
                std::int64_t raw_global_task_id,
                std::int64_t raw_connection_id,
                SymbolId task_type_symbol_id,
                SymbolId op_name_symbol_id,
                SymbolId op_type_symbol_id,
                SymbolId compute_task_type_symbol_id,
                SymbolId comm_name_symbol_id,
                std::int64_t raw_model_id = -1,
                SymbolId communication_task_type_symbol_id =
                    SymbolId::invalid());

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  void reserve(std::size_t count) { rows_.reserve(count); }
  const TaskRow& row(TaskId id) const;
  const std::vector<TaskRow>& rows() const noexcept { return rows_; }

 private:
  std::vector<TaskRow> rows_;
};

}  // namespace traceloom
