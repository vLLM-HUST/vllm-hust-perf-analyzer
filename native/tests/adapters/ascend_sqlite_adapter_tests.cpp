#include "traceloom/adapters/ascend_sqlite_adapter.h"
#include "support/ascend_sqlite_fixture.h"

#include "traceloom/analysis/native_pipeline.h"
#include "support/sqlite_fixture.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

void require_primitive_ir_equal(const traceloom::NativeIr& lhs,
                                const traceloom::NativeIr& rhs) {
  using namespace traceloom;
  require(lhs.source_refs.size() == rhs.source_refs.size(),
          "parallel TASK ingestion changed source domains");
  for (std::size_t index = 0; index < lhs.source_refs.size(); ++index) {
    const SourceRefRow& left = lhs.source_refs.row(SourceRefId(index));
    const SourceRefRow& right = rhs.source_refs.row(SourceRefId(index));
    require(left.id == right.id && left.source_kind == right.source_kind &&
                left.source_path == right.source_path &&
                left.table_name == right.table_name &&
                left.row_id == right.row_id,
            "parallel TASK ingestion changed source identity");
  }
  require(lhs.symbols.size() == rhs.symbols.size(),
          "parallel TASK ingestion changed symbol cardinality");
  for (std::size_t index = 0; index < lhs.symbols.size(); ++index) {
    require(lhs.symbols.value(SymbolId(index)) ==
                rhs.symbols.value(SymbolId(index)),
            "parallel TASK ingestion changed symbol identity");
  }
  require(lhs.streams.size() == rhs.streams.size(),
          "parallel TASK ingestion changed stream cardinality");
  for (std::size_t index = 0; index < lhs.streams.size(); ++index) {
    const StreamRow& left = lhs.streams.row(StreamId(index));
    const StreamRow& right = rhs.streams.row(StreamId(index));
    require(left.id == right.id && left.source_ref_id == right.source_ref_id &&
                left.device_id == right.device_id &&
                left.raw_stream_id == right.raw_stream_id,
            "parallel TASK ingestion changed stream identity");
  }
  require(lhs.trace_events.size() == rhs.trace_events.size(),
          "parallel TASK ingestion changed event cardinality");
  for (std::size_t index = 0; index < lhs.trace_events.size(); ++index) {
    const TraceEventRow& left = lhs.trace_events.row(TraceEventId(index));
    const TraceEventRow& right = rhs.trace_events.row(TraceEventId(index));
    require(left.id == right.id &&
                left.source_ref_id == right.source_ref_id &&
                left.source_row_id == right.source_row_id &&
                left.device_id == right.device_id &&
                left.stream_id == right.stream_id &&
                left.start_ns == right.start_ns && left.end_ns == right.end_ns &&
                left.raw_name_symbol_id == right.raw_name_symbol_id,
            "parallel TASK ingestion changed normalized event identity");
  }
  require(lhs.tasks.size() == rhs.tasks.size(),
          "parallel TASK ingestion changed task cardinality");
  for (std::size_t index = 0; index < lhs.tasks.size(); ++index) {
    const TaskRow& left = lhs.tasks.row(TaskId(index));
    const TaskRow& right = rhs.tasks.row(TaskId(index));
    require(left.id == right.id &&
                left.source_ref_id == right.source_ref_id &&
                left.trace_event_id == right.trace_event_id &&
                left.raw_task_id == right.raw_task_id &&
                left.raw_global_task_id == right.raw_global_task_id &&
                left.raw_connection_id == right.raw_connection_id &&
                left.raw_context_id == right.raw_context_id &&
                left.task_type_symbol_id == right.task_type_symbol_id &&
                left.op_name_symbol_id == right.op_name_symbol_id &&
                left.op_type_symbol_id == right.op_type_symbol_id &&
                left.compute_task_type_symbol_id ==
                    right.compute_task_type_symbol_id &&
                left.comm_name_symbol_id == right.comm_name_symbol_id &&
                left.raw_model_id == right.raw_model_id &&
                left.communication_task_type_symbol_id ==
                    right.communication_task_type_symbol_id,
            "parallel TASK ingestion changed normalized task identity");
  }
  require(lhs.communication_ops.size() == rhs.communication_ops.size(),
          "parallel TASK ingestion changed communication cardinality");
  for (std::size_t index = 0; index < lhs.communication_ops.size(); ++index) {
    const CommunicationOpRow& left =
        lhs.communication_ops.row(CommunicationOpId(index));
    const CommunicationOpRow& right =
        rhs.communication_ops.row(CommunicationOpId(index));
    require(left.id == right.id &&
                left.source_ref_id == right.source_ref_id &&
                left.trace_event_id == right.trace_event_id &&
                left.raw_connection_id == right.raw_connection_id &&
                left.raw_op_id == right.raw_op_id &&
                left.linked_task_count == right.linked_task_count &&
                left.linked_stream_count == right.linked_stream_count &&
                left.op_name_symbol_id == right.op_name_symbol_id &&
                left.op_type_symbol_id == right.op_type_symbol_id &&
                left.linked_task_name_symbol_id ==
                    right.linked_task_name_symbol_id &&
                left.linked_task_type_symbol_id ==
                    right.linked_task_type_symbol_id,
            "parallel TASK ingestion changed communication identity");
  }
}

}  // namespace

int main() {
  using namespace traceloom;
  using namespace traceloom::test;

  const std::string db_path = temp_ascend_db_path("_ok");
  materialize_ascend_minimal_fixture(db_path);

  const AscendSQLiteAdapter adapter(
      AscendSQLiteAdapterOptions{db_path, "ascend_smoke"});
  const NativeIr ir = adapter.load();
  AscendSQLiteAdapterOptions parallel_options{db_path, "ascend_smoke"};
  parallel_options.thread_count = 4;
  const NativeIr parallel_ir = AscendSQLiteAdapter(parallel_options).load();
  require_primitive_ir_equal(ir, parallel_ir);

  require(!ir.source_refs.empty(), "adapter did not emit SourceRef rows");
  require(ir.source_refs.row(SourceRefId(0)).source_kind == "ascend_smoke",
          "SourceRef source_kind mismatch");
  require(ir.source_refs.row(SourceRefId(0)).source_path == db_path,
          "SourceRef source_path mismatch");

  bool found_test_table = false;
  for (const SourceRefRow& row : ir.source_refs.rows()) {
    if (row.table_name == "COMPUTE_TASK_INFO") {
      found_test_table = true;
    }
  }
  require(found_test_table, "adapter inventory did not include test table");
  require(ir.strings.size() == 8, "adapter did not load STRING_IDS values");
  require(ir.streams.size() == 1, "adapter did not normalize streams");
  require(ir.trace_events.size() == 3, "adapter did not load TASK/COMM events");
  require(ir.tasks.size() == 2, "adapter did not load TASK facts");
  require(ir.communication_ops.size() == 1,
          "adapter did not load COMMUNICATION_OP facts");
  require(ir.trace_events.row(TraceEventId(0)).device_id == 0,
          "first TASK event device mismatch");
  require(ir.source_refs.row(ir.trace_events.row(TraceEventId(0)).source_ref_id)
              .table_name == "TASK",
          "first TASK event source table ref mismatch");
  require(ir.trace_events.row(TraceEventId(0)).source_row_id > 0,
          "first TASK event source row id missing");
  require(ir.trace_events.row(TraceEventId(0)).stream_id == 3,
          "first TASK event stream mismatch");
  require(ir.trace_events.row(TraceEventId(0)).start_ns == 100,
          "first TASK event start mismatch");
  require(ir.trace_events.row(TraceEventId(0)).end_ns == 160,
          "first TASK event end mismatch");
  require(ir.symbols.value(ir.trace_events.row(TraceEventId(0)).raw_name_symbol_id) ==
              "AI_CORE",
          "first TASK event task type decode mismatch");
  require(ir.tasks.row(TaskId(0)).raw_task_id == 99,
          "first TASK raw task id mismatch");
  require(ir.tasks.row(TaskId(0)).raw_global_task_id == 9001,
          "first TASK global task id mismatch");
  require(ir.tasks.row(TaskId(0)).raw_connection_id == 700,
          "first TASK connection id mismatch");
  require(ir.tasks.row(TaskId(0)).raw_context_id == 0,
          "first TASK context id mismatch");
  require(ir.tasks.row(TaskId(0)).raw_model_id == 2,
          "first TASK model id mismatch");
  require(ir.symbols.value(ir.tasks.row(TaskId(0)).op_name_symbol_id) ==
              "model.layers.0.mlp.gate_up_proj",
          "first TASK op name decode mismatch");
  require(ir.symbols.value(ir.tasks.row(TaskId(0)).op_type_symbol_id) ==
              "MatMul",
          "first TASK op type decode mismatch");
  require(ir.symbols.value(ir.tasks.row(TaskId(0)).compute_task_type_symbol_id) ==
              "MIX_AIC",
          "first TASK compute task type decode mismatch");
  require(!ir.tasks.row(TaskId(1)).op_name_symbol_id.valid(),
          "missing COMPUTE_TASK_INFO should leave op name invalid");
  require(ir.symbols.value(ir.tasks.row(TaskId(1)).comm_name_symbol_id) ==
              "comm_task_hccl_allreduce",
          "COMMUNICATION_TASK_INFO name decode mismatch");
  require(ir.source_refs
              .row(ir.communication_ops.row(CommunicationOpId(0)).source_ref_id)
              .table_name == "COMMUNICATION_OP",
          "COMMUNICATION_OP source table ref mismatch");
  require(ir.communication_ops.row(CommunicationOpId(0)).trace_event_id ==
              TraceEventId(2),
          "COMMUNICATION_OP trace event id mismatch");
  require(ir.communication_ops.row(CommunicationOpId(0)).raw_connection_id ==
              701,
          "COMMUNICATION_OP connection id mismatch");
  require(ir.communication_ops.row(CommunicationOpId(0)).raw_op_id == 55,
          "COMMUNICATION_OP op id mismatch");
  require(ir.communication_ops.row(CommunicationOpId(0)).linked_task_count ==
              1,
          "COMMUNICATION_OP linked task count mismatch");
  require(ir.communication_ops.row(CommunicationOpId(0)).linked_stream_count ==
              1,
          "COMMUNICATION_OP linked stream count mismatch");
  require(ir.symbols.value(
              ir.communication_ops.row(CommunicationOpId(0)).op_name_symbol_id) ==
              "hcclAllReduce",
          "COMMUNICATION_OP op name decode mismatch");
  require(ir.symbols.value(
              ir.communication_ops.row(CommunicationOpId(0)).op_type_symbol_id) ==
              "hcom_allReduce_",
          "COMMUNICATION_OP op type decode mismatch");
  require(ir.symbols.value(ir.communication_ops.row(CommunicationOpId(0))
                               .linked_task_name_symbol_id) ==
              "comm_task_hccl_allreduce",
          "COMMUNICATION_OP linked task name mismatch");
  require(ir.symbols.value(ir.communication_ops.row(CommunicationOpId(0))
                               .linked_task_type_symbol_id) ==
              "hcom_allReduce_",
          "COMMUNICATION_OP linked task type mismatch");

  const std::filesystem::path split_dir = temp_ascend_profile_dir("_split");
  const std::string golden_path = temp_ascend_db_path("_split_golden");
  materialize_ascend_split_golden_profiles(split_dir, golden_path);
  require(ascend_sqlite_has_usable_task_table(golden_path),
          "golden monolithic TASK table should be usable");
  require(looks_like_ascend_split_sqlite_profile(split_dir.string()),
          "split profile discovery missed AscendTask");
  const auto split_inventory =
      inventory_ascend_split_sqlite_profile(split_dir.string());
  bool saw_split_tasks = false;
  for (const auto& table : split_inventory) {
    if (table.table_name == "AscendTask") {
      saw_split_tasks = table.row_count == 4 && !table.create_sql.empty();
    }
  }
  require(saw_split_tasks,
          "split inventory did not report AscendTask schema/row count");
  const std::string unusable_monolithic =
      (split_dir / "msprof_without_task.db").string();
  traceloom::test::materialize_sqlite_fixture(
      unusable_monolithic,
      ascend_sqlite_fixture_dir("split_golden") / "negatives" /
          "unusable_monolithic.sql");
  require(!ascend_sqlite_has_usable_task_table(unusable_monolithic),
          "monolithic DB without TASK should not suppress split fallback");
  const std::string incompatible_monolithic =
      (split_dir / "msprof_incompatible_task.db").string();
  traceloom::test::materialize_sqlite_fixture(
      incompatible_monolithic,
      ascend_sqlite_fixture_dir("split_golden") / "negatives" /
          "incompatible_monolithic.sql");
  require(!ascend_sqlite_has_usable_task_table(incompatible_monolithic),
          "incompatible TASK schema should not suppress split fallback");

  AscendSQLiteAdapter golden_adapter(golden_path, "golden_monolithic");
  AscendSQLiteAdapter split_adapter(split_dir.string(), "golden_split");
  NativeIr golden_ir = golden_adapter.load();
  NativeIr split_ir = split_adapter.load();
  require(split_ir.tasks.size() == golden_ir.tasks.size(),
          "split TASK normalization changed task count");
  require(split_ir.trace_events.size() == golden_ir.trace_events.size(),
          "split TASK normalization changed timeline row count");
  require(golden_ir.communication_ops.size() == 1 &&
              split_ir.communication_ops.size() == 1,
          "split HCCLOP did not normalize to one communication operation");
  for (std::size_t index = 0; index < golden_ir.tasks.size(); ++index) {
    const TaskRow& golden_task = golden_ir.tasks.row(TaskId(index));
    const TaskRow& split_task = split_ir.tasks.row(TaskId(index));
    const TraceEventRow& golden_event =
        golden_ir.trace_events.row(golden_task.trace_event_id);
    const TraceEventRow& split_event =
        split_ir.trace_events.row(split_task.trace_event_id);
    require(split_event.device_id == golden_event.device_id &&
                split_event.stream_id == golden_event.stream_id &&
                split_event.start_ns == golden_event.start_ns &&
                split_event.end_ns == golden_event.end_ns,
            "split timeline does not match monolithic golden");
    require(split_ir.symbols.value(split_task.task_type_symbol_id) ==
                golden_ir.symbols.value(golden_task.task_type_symbol_id),
            "split task type does not match monolithic golden");
    require(split_task.raw_model_id == golden_task.raw_model_id,
            "split task model id does not match monolithic golden");
    require(split_task.raw_context_id == golden_task.raw_context_id,
            "split task context id does not match monolithic golden");
  }
  require(split_ir.symbols.value(split_ir.tasks.row(TaskId(0)).op_name_symbol_id) ==
              "linear",
          "split TaskInfo op name was not normalized");
  require(split_ir.symbols.value(split_ir.tasks.row(TaskId(0)).op_type_symbol_id) ==
              "MatMul",
          "split TaskInfo op type was not normalized");
  const CommunicationOpRow& split_comm =
      split_ir.communication_ops.row(CommunicationOpId(0));
  const TraceEventRow& split_comm_event =
      split_ir.trace_events.row(split_comm.trace_event_id);
  require(split_ir.source_refs.row(split_comm.source_ref_id).table_name ==
                  "HCCLOP" &&
              split_comm.raw_connection_id == 702 &&
              split_comm.raw_op_id == 55 &&
              split_comm.linked_task_count == 1 &&
              split_comm.linked_stream_count == 1 &&
              split_comm_event.start_ns == 220 &&
              split_comm_event.end_ns == 260 &&
              split_ir.symbols.value(split_comm.op_name_symbol_id) ==
                  "hcom_allReduce_" &&
              split_ir.symbols.value(
                  split_comm.linked_task_name_symbol_id) ==
                  "hcom_allReduce__golden_0",
          "split HCCLOP identity/task envelope normalization mismatch");

  NativePipelineOptions golden_pipeline_options;
  golden_pipeline_options.thread_count = 1;
  golden_pipeline_options.anchor_config
      .skip_tasks_covered_by_communication_ops = true;
  const NativePipelineResult golden_pipeline =
      run_native_pipeline(golden_ir, golden_pipeline_options);
  const NativePipelineResult split_pipeline =
      run_native_pipeline(split_ir, golden_pipeline_options);
  require(split_pipeline.anchor_stats.device_event_anchors ==
              golden_pipeline.anchor_stats.device_event_anchors,
          "split and monolithic anchor summaries differ");
  require(split_pipeline.cost_summary_lite.total_duration_ns ==
              golden_pipeline.cost_summary_lite.total_duration_ns,
          "split and monolithic cost summaries differ");

  require(std::filesystem::remove(split_dir / "host" / "sqlite" / "hccl.db"),
          "failed to remove split HCCLOP fixture DB");
  const NativeIr split_device_op_ir =
      AscendSQLiteAdapter(split_dir.string(), "golden_split_device_op").load();
  require(split_device_op_ir.communication_ops.size() == 1 &&
              split_device_op_ir.source_refs
                      .row(split_device_op_ir.communication_ops
                               .row(CommunicationOpId(0))
                               .source_ref_id)
                      .table_name == "HCCLOpSingleDevice",
          "device-side HCCLOpSingleDevice fallback was not normalized");

  const std::filesystem::path incomplete_dir = temp_ascend_profile_dir("_incomplete");
  std::filesystem::create_directories(incomplete_dir / "host" / "sqlite");
  bool caught_split_missing = false;
  try {
    const AscendSQLiteAdapter incomplete(incomplete_dir.string());
    (void)incomplete.load();
  } catch (const std::invalid_argument& ex) {
    caught_split_missing =
        std::string(ex.what()).find("AscendTask") != std::string::npos;
  }
  require(caught_split_missing,
          "missing split AscendTask did not produce a useful diagnostic");

  bool caught_missing = false;
  try {
    const AscendSQLiteAdapter missing(temp_ascend_db_path("_missing"));
    (void)missing.load();
  } catch (const std::invalid_argument&) {
    caught_missing = true;
  }
  require(caught_missing, "missing DB path did not raise invalid_argument");

  std::remove(db_path.c_str());
  std::remove(golden_path.c_str());
  std::filesystem::remove_all(split_dir);
  std::filesystem::remove_all(incomplete_dir);
  return 0;
}
