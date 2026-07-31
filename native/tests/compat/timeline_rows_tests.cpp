#include "traceloom/compat/timeline_rows.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>
#include <string>
#include <vector>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir;
  const SourceRefId task_source =
      ir.source_refs.append("fixture", "msprof.db", "TASK", 0);
  const SourceRefId comm_source =
      ir.source_refs.append("fixture", "msprof.db", "COMMUNICATION_OP", 0);

  const SymbolId raw_task = ir.symbols.intern("AI_CORE");
  const SymbolId matmul = ir.symbols.intern("MatMul");
  const SymbolId cube = ir.symbols.intern("Cube");
  const SymbolId all_reduce = ir.symbols.intern("HcclAllReduce");

  const TraceEventId task_event =
      ir.trace_events.append(task_source, 101, 0, 7, 1000, 2500, raw_task);
  ir.tasks.append(task_source, task_event, 55, 9001, -1, raw_task, matmul,
                  cube, raw_task, SymbolId::invalid());

  const TraceEventId comm_event =
      ir.trace_events.append(comm_source, 5, 1, 9, 3000, 4500, all_reduce);
  ir.communication_ops.append(comm_source, comm_event, 77, 12, 2, 1,
                              all_reduce);

  const compat::EventSqlRows rows =
      compat::build_timeline_sql_rows(ir, 3);
  require(rows.events.size() == 2);
  require(rows.event_sources.size() == 2);

  const std::vector<compat::EventSqlRow> timeline_rows =
      compat::split_timeline_event_sql_rows(rows);
  const std::vector<compat::EventSourceSqlRow> lineage_rows =
      compat::split_source_lineage_sql_rows(rows);
  require(timeline_rows.size() == rows.events.size());
  require(lineage_rows.size() == rows.event_sources.size());
  require(timeline_rows[0].event_id == rows.events[0].event_id);
  require(lineage_rows[0].event_id == rows.event_sources[0].event_id);

  require(rows.events[0].event_id == "event-0");
  require(rows.events[0].db_idx == 3);
  require(rows.events[0].device_id == 0);
  require(rows.events[0].step_idx == 0);
  require(rows.events[0].source_table == "TASK");
  require(rows.events[0].source_key == "101");
  require(rows.events[0].stream_id == 7);
  require(rows.events[0].start_ns == 1000);
  require(rows.events[0].end_ns == 2500);
  require(rows.events[0].dur_us == 1.5);
  require(rows.events[0].symbol == "Cube");
  require(rows.events[0].label == "Cube");
  require(rows.events[0].raw_label == "AI_CORE");
  require(rows.events[0].op_type == "Cube");
  require(rows.events[0].compute_task_type == "AI_CORE");
  require(rows.events[0].task_type == "AI_CORE");
  require(rows.events[0].role == "compute");
  require(rows.events[0].category == "compute");
  require(rows.events[0].family == "compute");

  require(rows.event_sources[0].event_id == rows.events[0].event_id);
  require(rows.event_sources[0].source_table == "TASK");
  require(rows.event_sources[0].source_key == "101");
  require(rows.event_sources[0].source_role == "primary");

  require(rows.events[1].event_id == "event-1");
  require(rows.events[1].device_id == 1);
  require(rows.events[1].source_table == "COMMUNICATION_OP");
  require(rows.events[1].source_key == "5");
  require(rows.events[1].symbol == "HcclAllReduce");
  require(rows.events[1].role == "comm");
  require(rows.events[1].category == "comm");
  require(rows.event_sources[1].source_table == "COMMUNICATION_OP");

  const SourceRefId aux_source = ir.source_refs.append(
      "cuda_nsys_sqlite", "report.sqlite", "CUPTI_ACTIVITY_KIND_RUNTIME", 0);
  const SymbolId runtime_name = ir.symbols.intern("cudaLaunchKernel");
  const SymbolId runtime_task = ir.symbols.intern("CUDA_RUNTIME_AUX");
  const TraceEventId aux_event = ir.trace_events.append(
      aux_source, 6, 0, 7, 4600, 4700, runtime_name);
  ir.tasks.append(aux_source, aux_event, 6, -1, -1, runtime_task,
                  runtime_name, SymbolId::invalid(), runtime_task,
                  SymbolId::invalid());
  const compat::EventSqlRows rows_with_aux =
      compat::build_timeline_sql_rows(ir, 3);
  require(rows_with_aux.events.size() == 3, "aux event missing");
  require(rows_with_aux.events[2].task_type == "CUDA_RUNTIME_AUX",
          "aux task type mismatch");
  require(rows_with_aux.events[2].role == "aux", "aux role mismatch");
  require(rows_with_aux.events[2].category == "aux",
          "aux category mismatch");
  require(rows_with_aux.events[2].family == "aux", "aux family mismatch");

  require(compat::trace_event_compat_id(task_event) == "event-0");

  NativeIr bad_ir;
  const SourceRefId bad_source =
      bad_ir.source_refs.append("fixture", "bad", "TASK", 0);
  bad_ir.tasks.append(bad_source, TraceEventId(99), 1, 2, -1, raw_task,
                      SymbolId::invalid(), SymbolId::invalid(),
                      SymbolId::invalid(), SymbolId::invalid());
  bool rejected_bad_task_ref = false;
  try {
    (void)compat::build_timeline_sql_rows(bad_ir);
  } catch (const std::invalid_argument&) {
    rejected_bad_task_ref = true;
  }
  require(rejected_bad_task_ref);

  return 0;
}
