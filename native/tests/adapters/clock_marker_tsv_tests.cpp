#include "traceloom/adapters/clock_marker_tsv.h"
#include "traceloom/testing/test_util.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

using namespace traceloom;
using traceloom::testing::require;

std::string temp_path(const std::string& stem) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return (std::filesystem::temp_directory_path() /
          (stem + "_" + std::to_string(now) + ".tsv"))
      .string();
}

void write_text(const std::string& path, const std::string& text) {
  std::ofstream output(path);
  require(output.good(), "temporary marker TSV opens for writing");
  output << text;
  require(output.good(), "temporary marker TSV is written completely");
}

NativeIr resolvable_ir(bool duplicate_record_api = false) {
  NativeIr ir;
  const SourceRefId task_source =
      ir.source_refs.append("synthetic", "fixture.db", "TASK", 0);
  const SourceRefId api_source =
      ir.source_refs.append("synthetic", "fixture.db", "CANN_API", 0);
  const SymbolId event_record = ir.symbols.intern("EVENT_RECORD");
  const TraceEventId event = ir.trace_events.append(
      task_source, 1, 0, 7, 1000, 1001, event_record);
  ir.tasks.append(task_source, event, 1, 1, 42, event_record, event_record,
                  event_record, event_record, SymbolId::invalid());
  const std::uint64_t global_tid = (123ULL << 32u) | 456ULL;
  ir.host_api_events.append(
      api_source, 10, 95, 130, global_tid, 42, SymbolId::invalid(),
      ir.symbols.intern("aclrtRecordEvent"), false, 0);
  if (duplicate_record_api) {
    ir.host_api_events.append(
        api_source, 11, 140, 150, global_tid, 43, SymbolId::invalid(),
        ir.symbols.intern("aclrtRecordEvent"), false, 0);
  }
  return ir;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  {
    NativeIr ir = resolvable_ir();
    const std::string path = temp_path("clock_marker_brackets");
    write_text(
        path,
        "marker_id\thost_before_ns\thost_after_ns\thost_pid\thost_tid\t"
        "device_id\tstream_id\tcall_site\treturn_status\n"
        "marker-0\t100\t200\t123\t456\t0\t7\tunit-test\t0\n"
        "marker-failed\t300\t310\t123\t456\t0\t\tunit-test\t507000\n");
    const ClockMarkerTsvLoadResult result =
        resolve_ascend_clock_marker_bracket_tsv(path, ir);
    require(result.marker_count == 2 && result.rejected_marker_count == 1,
            "bracket resolver reports successful and failed markers");
    require(ir.clock_markers.size() == 2,
            "every bracket remains auditable in the marker table");
    const ClockMarkerRow& resolved = ir.clock_markers.row(ClockMarkerId(0));
    require(resolved.device_timestamp_ns == 1000 &&
                resolved.has_connection_id &&
                resolved.raw_connection_id == 42 && resolved.has_stream_id &&
                resolved.stream_id == 7,
            "unique connectionId resolves bracket to TASK.startNs");
    const ClockMarkerRow& failed = ir.clock_markers.row(ClockMarkerId(1));
    require(failed.return_status == 507000 &&
                failed.device_timestamp_ns == -1 &&
                !failed.has_connection_id,
            "failed collection is retained but cannot enter calibration");
    std::remove(path.c_str());
  }

  {
    NativeIr ir;
    const SourceRefId task_source =
        ir.source_refs.append("synthetic", "fixture.db", "TASK", 0);
    const SourceRefId api_source =
        ir.source_refs.append("synthetic", "fixture.db", "CANN_API", 0);
    const SymbolId event_record = ir.symbols.intern("EVENT_RECORD");
    const std::uint64_t global_tid = (123ULL << 32u) | 456ULL;
    for (std::int64_t index = 0; index < 2; ++index) {
      const std::int64_t connection_id = 42 + index;
      const TraceEventId event = ir.trace_events.append(
          task_source, static_cast<std::uint64_t>(index + 1), 0, 7,
          1000 + index * 1000, 1001 + index * 1000, event_record);
      ir.tasks.append(task_source, event, index + 1, index + 1,
                      connection_id, event_record, event_record, event_record,
                      event_record, SymbolId::invalid());
      ir.host_api_events.append(
          api_source, static_cast<std::uint64_t>(index + 10),
          50 + index * 200, 60 + index * 200, global_tid, connection_id,
          SymbolId::invalid(), ir.symbols.intern("aclrtRecordEvent"), false,
          0);
    }
    const std::string path = temp_path("clock_marker_ordered_bijection");
    write_text(
        path,
        "marker_id\thost_before_ns\thost_after_ns\thost_pid\thost_tid\t"
        "device_id\tstream_id\tcall_site\treturn_status\n"
        "marker-0\t100\t200\t123\t456\t0\t7\tunit-test\t0\n"
        "marker-1\t300\t400\t123\t456\t0\t7\tunit-test\t0\n");
    const ClockMarkerTsvLoadResult result =
        resolve_ascend_clock_marker_bracket_tsv(path, ir);
    require(result.marker_count == 2 &&
                ir.clock_markers.row(ClockMarkerId(0)).raw_connection_id ==
                    42 &&
                ir.clock_markers.row(ClockMarkerId(1)).raw_connection_id ==
                    43,
            "affine-validated order-preserving bijection resolves clock skew");
    std::remove(path.c_str());
  }

  {
    NativeIr ir;
    const SourceRefId task_source =
        ir.source_refs.append("synthetic", "fixture.db", "TASK", 0);
    const SourceRefId api_source =
        ir.source_refs.append("synthetic", "fixture.db", "CANN_API", 0);
    const SymbolId event_record = ir.symbols.intern("EVENT_RECORD");
    const std::uint64_t global_tid = (123ULL << 32u) | 456ULL;
    const std::int64_t api_starts[] = {50, 55, 450};
    for (std::int64_t index = 0; index < 3; ++index) {
      const std::int64_t connection_id = 42 + index;
      const TraceEventId event = ir.trace_events.append(
          task_source, static_cast<std::uint64_t>(index + 1), 0, 7,
          1000 + index * 1000, 1001 + index * 1000, event_record);
      ir.tasks.append(task_source, event, index + 1, index + 1,
                      connection_id, event_record, event_record, event_record,
                      event_record, SymbolId::invalid());
      ir.host_api_events.append(
          api_source, static_cast<std::uint64_t>(index + 10),
          api_starts[index], api_starts[index] + 10, global_tid,
          connection_id, SymbolId::invalid(),
          ir.symbols.intern("aclrtRecordEvent"), false, 0);
    }
    const std::string path = temp_path("clock_marker_non_affine_sequence");
    write_text(
        path,
        "marker_id\thost_before_ns\thost_after_ns\thost_pid\thost_tid\t"
        "device_id\tstream_id\tcall_site\treturn_status\n"
        "marker-0\t100\t200\t123\t456\t0\t7\tunit-test\t0\n"
        "marker-1\t300\t400\t123\t456\t0\t7\tunit-test\t0\n"
        "marker-2\t500\t600\t123\t456\t0\t7\tunit-test\t0\n");
    bool rejected = false;
    try {
      (void)resolve_ascend_clock_marker_bracket_tsv(path, ir);
    } catch (const std::invalid_argument& error) {
      rejected = std::string(error.what()).find("no unique") !=
                 std::string::npos;
    }
    require(rejected,
            "non-affine same-count sequence fails the ordinal fallback");
    std::remove(path.c_str());
  }

  {
    NativeIr ir = resolvable_ir(true);
    const std::string path = temp_path("clock_marker_ambiguous");
    write_text(
        path,
        "marker_id\thost_before_ns\thost_after_ns\thost_pid\thost_tid\t"
        "device_id\tstream_id\tcall_site\treturn_status\n"
        "marker-0\t100\t200\t123\t456\t0\t\tunit-test\t0\n");
    bool rejected = false;
    try {
      (void)resolve_ascend_clock_marker_bracket_tsv(path, ir);
    } catch (const std::invalid_argument& error) {
      rejected = std::string(error.what()).find("no unique") !=
                 std::string::npos;
    }
    require(rejected,
            "ambiguous aclrtRecordEvent bracket fails closed");
    std::remove(path.c_str());
  }

  {
    NativeIr ir = resolvable_ir();
    const std::string path = temp_path("clock_marker_missing_task");
    write_text(
        path,
        "marker_id\thost_before_ns\thost_after_ns\thost_pid\thost_tid\t"
        "device_id\tstream_id\tcall_site\treturn_status\n"
        "marker-0\t100\t200\t123\t456\t0\t999\tunit-test\t0\n");
    const ClockMarkerTsvLoadResult result =
        resolve_ascend_clock_marker_bracket_tsv(path, ir);
    const ClockMarkerRow& marker = ir.clock_markers.row(ClockMarkerId(0));
    require(result.marker_count == 1 && result.rejected_marker_count == 1 &&
                marker.return_status == 0 && marker.has_connection_id &&
                marker.raw_connection_id == 42 &&
                marker.device_timestamp_ns == -1,
            "missing TASK is retained as an auditable rejected marker");
    std::remove(path.c_str());
  }

  {
    NativeIr ir;
    const std::string path = temp_path("clock_markers_final");
    write_text(
        path,
        "marker_id\thost_before_ns\thost_after_ns\tdevice_timestamp_ns\t"
        "host_pid\thost_tid\tdevice_id\tstream_id\tconnection_id\t"
        "call_site\treturn_status\n"
        "marker-0\t100\t110\t1000\t123\t456\t0\t\t\tunit-test\t0\n");
    const ClockMarkerTsvLoadResult result = load_clock_marker_tsv(path, ir);
    require(result.marker_count == 1 && result.rejected_marker_count == 0 &&
                !ir.clock_markers.row(ClockMarkerId(0)).has_stream_id &&
                !ir.clock_markers.row(ClockMarkerId(0)).has_connection_id,
            "frozen final payload preserves optional blank ids");
    std::remove(path.c_str());
  }

  return 0;
}
