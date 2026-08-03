#include "traceloom/analysis/productive_timeline.h"
#include "traceloom/analysis/semantic_task_classifier.h"
#include "traceloom/testing/test_util.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using traceloom::testing::require;

namespace {

using namespace traceloom;

struct TaskSpec {
  std::uint32_t device_id = 0;
  std::uint32_t stream_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  SemanticTaskRole role = SemanticTaskRole::kUnknown;
  std::int64_t connection_id = -1;
  bool as_comm_op = false;  // also materialize a COMMUNICATION_OP row
  std::string comm_name;    // task comm name (metadata)
  std::string op_name;      // op name (metadata)
  std::uint64_t row_id = 0;
};

struct Scene {
  NativeIr ir;
  SemanticTaskClassificationResult classification;
};

// Builds an IR whose tasks/communication ops mirror the specs, with a
// classification result aligned to task order. Each task gets its own
// source ref so lineage row ids are distinguishable.
Scene make_scene(const std::vector<TaskSpec>& specs) {
  Scene scene;
  NativeIr& ir = scene.ir;
  const SymbolId task_type = ir.symbols.intern("TASK_TYPE");
  const SymbolId label = ir.symbols.intern("label");

  std::uint64_t row = 1;
  for (const TaskSpec& spec : specs) {
    const SourceRefId task_source =
        ir.source_refs.append("fixture", "memory", "TASK", row);
    const TraceEventId event = ir.trace_events.append(
        task_source, row, spec.device_id, spec.stream_id, spec.start_ns,
        spec.end_ns, label);
    const SymbolId comm_name =
        spec.comm_name.empty() ? SymbolId::invalid()
                               : ir.symbols.intern(spec.comm_name);
    ir.tasks.append(task_source, event, row, static_cast<std::int64_t>(row),
                    spec.connection_id, task_type, SymbolId::invalid(),
                    SymbolId::invalid(), SymbolId::invalid(), comm_name);
    SemanticTaskClassificationRow classification_row;
    classification_row.task_id = ir.tasks.row(TaskId(row - 1)).id;
    classification_row.trace_event_id = event;
    classification_row.role = spec.role;
    scene.classification.rows.push_back(classification_row);
    if (spec.as_comm_op) {
      const SourceRefId comm_source =
          ir.source_refs.append("fixture", "memory", "COMMUNICATION_OP", row);
      const TraceEventId comm_event = ir.trace_events.append(
          comm_source, row, spec.device_id, spec.stream_id, spec.start_ns,
          spec.end_ns, label);
      const SymbolId op_name =
          spec.op_name.empty() ? SymbolId::invalid()
                               : ir.symbols.intern(spec.op_name);
      ir.communication_ops.append(comm_source, comm_event, spec.connection_id,
                                  static_cast<std::int64_t>(row), 1, 1,
                                  op_name);
    }
    ++row;
  }
  scene.classification.semantic_rules_version = "test-v1";
  scene.classification.semantic_rules_sha256 =
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  return scene;
}

DeviceTimelineResult single_device(const Scene& scene,
                                   const ProductiveTimelineOptions& options =
                                       ProductiveTimelineOptions{}) {
  const ProductiveTimelineRunResult run =
      build_productive_timelines(scene.ir, scene.classification, options);
  require(run.devices.size() == 1, "exactly one device result");
  return run.devices.front();
}

std::int64_t interval_duration(const DeviceTimelineResult& result,
                               DeviceIntervalKind kind) {
  std::int64_t total = 0;
  for (const DeviceIntervalRow& row : result.intervals) {
    if (row.kind == kind) {
      total += row.end_ns - row.start_ns;
    }
  }
  return total;
}

void check_coverage_invariant(const DeviceTimelineResult& result) {
  if (result.status != AnalysisStatus::kOk || !result.span_start_ns ||
      !result.span_end_ns) {
    return;
  }
  const std::int64_t span = *result.span_end_ns - *result.span_start_ns;
  require(interval_duration(result, DeviceIntervalKind::kProductiveActive) +
                  interval_duration(result,
                                    DeviceIntervalKind::kVisibleProductiveIdle) ==
              span,
          "productive + gap covers the span exactly");
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  // ---- Interval algebra: overlap merges. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 0, .end_ns = 10, .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 5, .end_ns = 15, .role = SemanticTaskRole::kProductiveCompute}});
    const DeviceTimelineResult result = single_device(scene);
    require(result.status == AnalysisStatus::kOk, "overlap case ok");
    require(result.intervals.size() == 1, "overlap merges to one interval");
    require(result.intervals[0].kind == DeviceIntervalKind::kProductiveActive &&
                result.intervals[0].start_ns == 0 &&
                result.intervals[0].end_ns == 15,
            "[0,10)+[5,15) -> [0,15)");
    check_coverage_invariant(result);
  }
  // ---- Interval algebra: adjacent intervals merge (half-open union). ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 0, .end_ns = 10, .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 10, .end_ns = 20, .role = SemanticTaskRole::kProductiveCompute}});
    const DeviceTimelineResult result = single_device(scene);
    require(result.status == AnalysisStatus::kOk, "adjacent case ok");
    require(result.intervals.size() == 1 &&
                result.intervals[0].start_ns == 0 &&
                result.intervals[0].end_ns == 20,
            "[0,10)+[10,20) -> [0,20)");
    check_coverage_invariant(result);
  }
  // ---- Interval algebra: disjoint intervals produce a gap. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 0, .end_ns = 10, .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 12, .end_ns = 20, .role = SemanticTaskRole::kProductiveCompute}});
    const DeviceTimelineResult result = single_device(scene);
    require(result.status == AnalysisStatus::kOk, "disjoint case ok");
    require(result.intervals.size() == 3, "prod + gap + prod");
    require(result.intervals[0].kind == DeviceIntervalKind::kProductiveActive &&
                result.intervals[0].start_ns == 0 &&
                result.intervals[0].end_ns == 10,
            "first productive interval");
    require(result.intervals[1].kind ==
                DeviceIntervalKind::kVisibleProductiveIdle &&
                result.intervals[1].start_ns == 10 &&
                result.intervals[1].end_ns == 12,
            "gap [10,12)");
    check_coverage_invariant(result);
  }
  // ---- Multi-stream: global union, not per-stream. ----
  {
    const Scene scene = make_scene(
        {{.stream_id = 0, .start_ns = 0, .end_ns = 10, .role = SemanticTaskRole::kProductiveCompute},
         {.stream_id = 1, .start_ns = 5, .end_ns = 15, .role = SemanticTaskRole::kProductiveDataMove}});
    const DeviceTimelineResult result = single_device(scene);
    require(result.status == AnalysisStatus::kOk, "multi-stream ok");
    require(result.intervals.size() == 1 &&
                result.intervals[0].start_ns == 0 &&
                result.intervals[0].end_ns == 15,
            "global union [0,15)");
    check_coverage_invariant(result);
  }
  // ---- Wait tasks never enter the productive union. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 0, .end_ns = 10, .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 10, .end_ns = 15, .role = SemanticTaskRole::kVisibleWait},
         {.start_ns = 20, .end_ns = 30, .role = SemanticTaskRole::kProductiveCompute}});
    const DeviceTimelineResult result = single_device(scene);
    require(result.intervals.size() == 3, "wait task stays out of union");
    require(result.intervals[1].kind ==
                DeviceIntervalKind::kVisibleProductiveIdle &&
                result.intervals[1].start_ns == 10 &&
                result.intervals[1].end_ns == 20,
            "gap [10,20) covers the wait");
    check_coverage_invariant(result);
  }
  // ---- Communication canonicalization: task absorbed by op, exact
  // lineage preserved. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100, .end_ns = 150, .role = SemanticTaskRole::kProductiveComm,
          .connection_id = 42, .as_comm_op = true, .comm_name = "AllReduce",
          .op_name = "AllReduce"}});
    const DeviceTimelineResult result = single_device(scene);
    require(result.status == AnalysisStatus::kOk, "comm canonical ok");
    require(result.intervals.size() == 1 &&
                result.intervals[0].kind == DeviceIntervalKind::kProductiveActive &&
                result.intervals[0].start_ns == 100 &&
                result.intervals[0].end_ns == 150,
            "one canonical productive interval");
    require(result.intervals[0].source_links.size() == 2,
            "op + task both in lineage");
    const ProductiveSourceLink& op_link = result.intervals[0].source_links[0];
    const ProductiveSourceLink& task_link = result.intervals[0].source_links[1];
    require(op_link.kind == ProductiveSourceLink::Kind::kCommunicationOp &&
                op_link.communication_op_id.valid() &&
                !op_link.task_id.valid(),
            "op link identity");
    require(task_link.kind == ProductiveSourceLink::Kind::kTask &&
                task_link.task_id.valid() && task_link.trace_event_id.valid(),
            "task link identity");
    check_coverage_invariant(result);
  }
  // ---- Communication: metadata conflict prevents absorption. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100, .end_ns = 150, .role = SemanticTaskRole::kProductiveComm,
          .connection_id = 42, .as_comm_op = true, .comm_name = "AllReduce",
          .op_name = "AllGather"}});
    const DeviceTimelineResult result = single_device(scene);
    require(result.status == AnalysisStatus::kOk, "metadata conflict ok");
    require(result.intervals.size() == 1 &&
                result.intervals[0].source_links.size() == 2,
            "conflicting task stays canonical (two sources)");
    require(!result.diagnostics.empty(), "metadata conflict diagnostic");
    check_coverage_invariant(result);
  }
  // ---- Communication: task without connectionId but with a temporally
  // overlapping op reports ambiguity and stays canonical. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100, .end_ns = 150, .role = SemanticTaskRole::kProductiveComm,
          .as_comm_op = true, .op_name = "AllReduce"}});
    const DeviceTimelineResult result = single_device(scene);
    require(result.status == AnalysisStatus::kOk, "no-connection ambiguity ok");
    require(result.intervals.size() == 1 &&
                result.intervals[0].source_links.size() == 2,
            "task and op both canonical");
    bool saw_ambiguity = false;
    for (const TimelineDiagnostic& d : result.diagnostics) {
      if (d.message.find("no connectionId") != std::string::npos) {
        saw_ambiguity = true;
      }
    }
    require(saw_ambiguity, "ambiguity diagnostic emitted");
    check_coverage_invariant(result);
  }
  // ---- Communication: task without connectionId and no overlapping op. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100, .end_ns = 150, .role = SemanticTaskRole::kProductiveComm}});
    const DeviceTimelineResult result = single_device(scene);
    require(result.intervals.size() == 1 &&
                result.intervals[0].start_ns == 100 &&
                result.intervals[0].end_ns == 150,
            "no-connection comm task is canonical");
    check_coverage_invariant(result);
  }
  // ---- Shard merge semantics: per-shard analysis would drop the gap. ----
  {
    const Scene shard_a = make_scene(
        {{.start_ns = 0, .end_ns = 100, .role = SemanticTaskRole::kProductiveCompute}});
    const Scene shard_b = make_scene(
        {{.start_ns = 200, .end_ns = 300, .role = SemanticTaskRole::kProductiveCompute}});
    const DeviceTimelineResult a = single_device(shard_a);
    const DeviceTimelineResult b = single_device(shard_b);
    require(a.intervals.size() == 1, "shard A has no gap");
    require(b.intervals.size() == 1, "shard B has no gap");

    Scene merged = make_scene(
        {{.start_ns = 0, .end_ns = 100, .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 200, .end_ns = 300, .role = SemanticTaskRole::kProductiveCompute}});
    const DeviceTimelineResult m = single_device(merged);
    require(m.intervals.size() == 3 &&
                m.intervals[1].kind == DeviceIntervalKind::kVisibleProductiveIdle &&
                m.intervals[1].start_ns == 100 && m.intervals[1].end_ns == 200,
            "merged span [0,300) exposes gap [100,200)");
    check_coverage_invariant(m);
  }
  // ---- No productive task, no explicit span. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 10, .end_ns = 15, .role = SemanticTaskRole::kVisibleWait}});
    const ProductiveTimelineRunResult run =
        build_productive_timelines(scene.ir, scene.classification);
    require(run.devices.size() == 1 &&
                run.devices[0].status == AnalysisStatus::kNoProductiveSpan,
            "no productive task -> no_productive_span");
    require(run.devices[0].intervals.empty(), "no intervals emitted");
    require(!run.devices[0].span_start_ns && !run.devices[0].span_end_ns,
            "span boundaries null");
  }
  // ---- No productive task, explicit span: whole span is idle. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 10, .end_ns = 15, .role = SemanticTaskRole::kVisibleWait}});
    ProductiveTimelineOptions options;
    options.explicit_span_start_ns = 0;
    options.explicit_span_end_ns = 100;
    const DeviceTimelineResult result = single_device(scene, options);
    require(result.status == AnalysisStatus::kOk, "explicit span ok");
    require(result.intervals.size() == 1 &&
                result.intervals[0].kind ==
                    DeviceIntervalKind::kVisibleProductiveIdle &&
                result.intervals[0].start_ns == 0 &&
                result.intervals[0].end_ns == 100,
            "explicit span is fully visible idle");
    check_coverage_invariant(result);
  }
  // ---- Explicit span with boundary gaps. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100, .end_ns = 150, .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 200, .end_ns = 250, .role = SemanticTaskRole::kProductiveCompute}});
    ProductiveTimelineOptions options;
    options.explicit_span_start_ns = 50;
    options.explicit_span_end_ns = 300;
    const DeviceTimelineResult result = single_device(scene, options);
    require(result.status == AnalysisStatus::kOk, "explicit span boundaries");
    require(result.intervals.size() == 5, "gap prod gap prod gap");
    require(result.intervals[0].kind ==
                    DeviceIntervalKind::kVisibleProductiveIdle &&
                result.intervals[0].start_ns == 50 &&
                result.intervals[0].end_ns == 100,
            "leading boundary gap");
    require(result.intervals[4].kind ==
                    DeviceIntervalKind::kVisibleProductiveIdle &&
                result.intervals[4].start_ns == 250 &&
                result.intervals[4].end_ns == 300,
            "trailing boundary gap");
    check_coverage_invariant(result);
  }
  // ---- Invalid explicit span. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 0, .end_ns = 10, .role = SemanticTaskRole::kProductiveCompute}});
    ProductiveTimelineOptions options;
    options.explicit_span_start_ns = 100;
    options.explicit_span_end_ns = 100;
    const DeviceTimelineResult result = single_device(scene, options);
    require(result.status == AnalysisStatus::kInvalidAnalysisSpan,
            "zero-length span rejected");
    require(result.intervals.empty(), "no intervals for invalid span");
  }
  // ---- Multi-device: one result per device. ----
  {
    const Scene scene = make_scene(
        {{.device_id = 0, .start_ns = 0, .end_ns = 10, .role = SemanticTaskRole::kProductiveCompute},
         {.device_id = 1, .start_ns = 20, .end_ns = 30, .role = SemanticTaskRole::kProductiveCompute}});
    const ProductiveTimelineRunResult run =
        build_productive_timelines(scene.ir, scene.classification);
    require(run.devices.size() == 2, "two device results");
    require(run.devices[0].device_id == 0 && run.devices[0].intervals.size() == 1,
            "device 0 timeline");
    require(run.devices[1].device_id == 1 && run.devices[1].intervals.size() == 1,
            "device 1 timeline");
    require(run.semantic_rules_version == "test-v1",
            "ruleset version propagated at run level");
  }
  // ---- Empty input: run-level status, not an empty vector. ----
  {
    NativeIr empty_ir;
    SemanticTaskClassificationResult empty_class;
    empty_class.semantic_rules_version = "test-v1";
    const ProductiveTimelineRunResult run =
        build_productive_timelines(empty_ir, empty_class);
    require(run.status == AnalysisStatus::kEmptyInput,
            "empty input -> empty_input");
    require(run.devices.empty(), "empty input -> no devices");
  }
  // ---- Invalid input: negative-duration task is skipped, status degrades,
  // valid remainder still analyzed. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100, .end_ns = 90, .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 200, .end_ns = 300, .role = SemanticTaskRole::kProductiveCompute}});
    const ProductiveTimelineRunResult run =
        build_productive_timelines(scene.ir, scene.classification);
    require(run.status == AnalysisStatus::kInvalidInput,
            "negative duration -> invalid_input");
    require(run.devices.size() == 1, "device still analyzed");
    require(run.devices[0].intervals.size() == 1 &&
                run.devices[0].intervals[0].start_ns == 200 &&
                run.devices[0].intervals[0].end_ns == 300,
            "invalid task skipped, remainder analyzed");
    require(!run.devices[0].diagnostics.empty(), "invalid interval diagnostic");
  }
  // ---- Invalid input: zero-duration task. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100, .end_ns = 100, .role = SemanticTaskRole::kProductiveCompute}});
    const ProductiveTimelineRunResult run =
        build_productive_timelines(scene.ir, scene.classification);
    require(run.status == AnalysisStatus::kInvalidInput,
            "zero duration -> invalid_input");
  }
  // ---- Invalid input: out-of-range task trace_event_id. ----
  {
    Scene scene = make_scene(
        {{.start_ns = 0, .end_ns = 10, .role = SemanticTaskRole::kProductiveCompute}});
    const SourceRefId source =
        scene.ir.source_refs.append("fixture", "memory", "TASK", 9);
    const SymbolId label = scene.ir.symbols.intern("label");
    scene.ir.tasks.append(source, TraceEventId(99), 9, 9, -1, label,
                          SymbolId::invalid(), SymbolId::invalid(),
                          SymbolId::invalid(), SymbolId::invalid());
    SemanticTaskClassificationRow row;
    row.task_id = scene.ir.tasks.row(TaskId(1)).id;
    row.trace_event_id = TraceEventId(99);
    row.role = SemanticTaskRole::kProductiveCompute;
    scene.classification.rows.push_back(row);
    const ProductiveTimelineRunResult run =
        build_productive_timelines(scene.ir, scene.classification);
    require(run.status == AnalysisStatus::kInvalidInput,
            "out-of-range trace_event_id -> invalid_input");
    require(run.devices.size() == 1 && run.devices[0].intervals.size() == 1,
            "valid remainder still analyzed");
  }
  // ---- Classification alignment: mismatched row ids are rejected. ----
  {
    Scene scene = make_scene(
        {{.start_ns = 0, .end_ns = 10, .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 20, .end_ns = 30, .role = SemanticTaskRole::kProductiveCompute}});
    scene.classification.rows[1].task_id = scene.classification.rows[0].task_id;
    bool rejected = false;
    try {
      (void)build_productive_timelines(scene.ir, scene.classification);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "misaligned classification row rejected");
  }
  return 0;
}
