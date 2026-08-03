// E3: per-stream observable state timeline tests (idle evidence contract
// sections 2, 3.3, 8 step 5). Every scene runs the real E2 pipeline
// (build_productive_timelines) first, so the tests exercise the full
// E1-input -> E2 -> E3 chain including communication canonicalization and
// analysis-span resolution. Every test ends with the shared invariant
// checker: intervals are a positive-length, adjacent, span-covering
// partition; empty_observed carries no links; ambiguous_overlap carries at
// least two pairwise-distinct links.

#include "traceloom/analysis/productive_timeline.h"
#include "traceloom/analysis/semantic_task_classifier.h"
#include "traceloom/analysis/stream_state_timeline.h"
#include "traceloom/testing/test_util.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using traceloom::testing::require;

namespace {

using namespace traceloom;

// Sentinel stream id for events without stream metadata, mirroring the
// ascend adapter (no StreamRow is appended for it).
constexpr std::uint32_t kUnassignedStreamSentinel = 0xffffffffu;

ProductiveTimelineOptions explicit_span(std::int64_t start_ns,
                                        std::int64_t end_ns) {
  ProductiveTimelineOptions options;
  options.explicit_span_start_ns = start_ns;
  options.explicit_span_end_ns = end_ns;
  return options;
}

struct EventSpec {
  std::uint32_t device_id = 0;
  std::uint32_t stream_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  SemanticTaskRole role = SemanticTaskRole::kUnknown;
  std::int64_t connection_id = -1;
  bool as_comm_op = false;      // also materialize a COMMUNICATION_OP row
  std::string comm_name;        // task comm name (canonicalization metadata)
  std::string op_name;          // op name (canonicalization metadata)
  bool omit_stream_row = false;  // leave the StreamTable row out
  bool shared_trace_event = false;  // reuse the previous spec's TraceEventId
  bool invalid_trace_event = false;  // reference a dangling TraceEventId
  std::optional<std::uint32_t> op_stream_id;  // op stream (defaults to stream_id)
  bool shared_op = false;  // reuse the previous spec's COMMUNICATION_OP row
  std::uint64_t row_id = 0;
};

struct Scene {
  NativeIr ir;
  SemanticTaskClassificationResult classification;
  ProductiveTimelineRunResult productive;  // real E2 over the same IR
};

// Builds an IR whose tasks/communication ops mirror the specs, with a
// classification result aligned to task order and StreamTable rows for every
// observed (device, stream). Each task gets its own source ref so lineage
// row ids are distinguishable.
Scene make_scene(const std::vector<EventSpec>& specs,
                 const ProductiveTimelineOptions& options =
                     ProductiveTimelineOptions{}) {
  Scene scene;
  NativeIr& ir = scene.ir;
  const SymbolId task_type = ir.symbols.intern("TASK_TYPE");
  const SymbolId label = ir.symbols.intern("label");

  std::uint64_t row = 1;
  bool have_op = false;  // last created op row, for shared_op reuse
  for (const EventSpec& spec : specs) {
    const SourceRefId task_source =
        ir.source_refs.append("fixture", "memory", "TASK", row);
    TraceEventId event;
    if (spec.invalid_trace_event) {
      event = TraceEventId(99);  // dangling; no TraceEventRow appended
    } else if (spec.shared_trace_event && row > 1) {
      const TaskRow& previous = ir.tasks.row(TaskId(row - 2));
      event = previous.trace_event_id;
    } else {
      event = ir.trace_events.append(task_source, row, spec.device_id,
                                     spec.stream_id, spec.start_ns,
                                     spec.end_ns, label);
    }
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
      const std::uint32_t op_stream =
          spec.op_stream_id.value_or(spec.stream_id);
      const SourceRefId comm_source =
          ir.source_refs.append("fixture", "memory", "COMMUNICATION_OP", row);
      const TraceEventId comm_event = ir.trace_events.append(
          comm_source, row, spec.device_id, op_stream, spec.start_ns,
          spec.end_ns, label);
      const SymbolId op_name =
          spec.op_name.empty() ? SymbolId::invalid()
                               : ir.symbols.intern(spec.op_name);
      ir.communication_ops.append(comm_source, comm_event, spec.connection_id,
                                  static_cast<std::int64_t>(row), 1, 1,
                                  op_name);
      have_op = true;
    } else if (spec.shared_op) {
      require(have_op, "shared_op requires a previous COMMUNICATION_OP row");
    }
    ++row;
  }
  scene.classification.semantic_rules_version = "test-v1";
  scene.classification.semantic_rules_sha256 =
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

  std::set<std::pair<std::uint32_t, std::uint32_t>> seen_streams;
  for (const EventSpec& spec : specs) {
    if (spec.omit_stream_row) {
      continue;
    }
    if (seen_streams.insert({spec.device_id, spec.stream_id}).second) {
      const SourceRefId stream_source =
          ir.source_refs.append("fixture", "memory", "STREAM", row);
      ir.streams.append(stream_source, spec.device_id,
                        static_cast<std::uint64_t>(spec.stream_id));
    }
    if (spec.op_stream_id.has_value() &&
        *spec.op_stream_id != kUnassignedStreamSentinel &&
        seen_streams.insert({spec.device_id, *spec.op_stream_id}).second) {
      const SourceRefId stream_source =
          ir.source_refs.append("fixture", "memory", "STREAM", row);
      ir.streams.append(stream_source, spec.device_id,
                        static_cast<std::uint64_t>(*spec.op_stream_id));
    }
  }

  scene.productive =
      build_productive_timelines(ir, scene.classification, options);
  return scene;
}

StreamStateRunResult build_stream_states(const Scene& scene) {
  return build_stream_state_timelines(scene.ir, scene.classification,
                                      scene.productive);
}

const StreamStateDeviceResult* find_device(const StreamStateRunResult& run,
                                           std::uint32_t device_id) {
  for (const StreamStateDeviceResult& device : run.devices) {
    if (device.device_id == device_id) {
      return &device;
    }
  }
  return nullptr;
}

const StreamStateTimeline* find_timeline(const StreamStateRunResult& run,
                                         std::uint32_t device_id,
                                         std::uint64_t stream_id) {
  const StreamStateDeviceResult* device = find_device(run, device_id);
  if (device == nullptr) {
    return nullptr;
  }
  for (const StreamStateTimeline& timeline : device->timelines) {
    if (timeline.stream_id == stream_id) {
      return &timeline;
    }
  }
  return nullptr;
}

bool has_diagnostic(const std::vector<TimelineDiagnostic>& diagnostics,
                    const char* code) {
  for (const TimelineDiagnostic& diagnostic : diagnostics) {
    if (diagnostic.message.find(code) == 0) {
      return true;
    }
  }
  return false;
}

// The invariant checker (contract section 3.3 and the E3 spec): every
// timeline is a positive-length, adjacent, span-covering partition;
// empty_observed carries no links; ambiguous_overlap carries at least two
// pairwise-distinct links; per-device universe metadata aggregates into the
// run-level fields.
void check_stream_state_invariants(const StreamStateRunResult& run) {
  std::size_t timeline_count = 0;
  std::size_t universe_sum = 0;
  bool all_devices_complete = true;
  for (const StreamStateDeviceResult& device : run.devices) {
    universe_sum += device.stream_universe_size;
    if (!device.observed_universe_scan_complete) {
      all_devices_complete = false;
    }
    if (device.status != AnalysisStatus::kOk) {
      require(device.timelines.empty(), "non-ok device has no timelines");
      require(!device.span_start_ns.has_value() &&
                  !device.span_end_ns.has_value(),
              "non-ok device has no span");
      continue;
    }
    require(device.span_start_ns.has_value() && device.span_end_ns.has_value(),
            "ok device carries span");
    require(*device.span_end_ns > *device.span_start_ns, "span positive");
    timeline_count += device.timelines.size();
    for (const StreamStateTimeline& timeline : device.timelines) {
      require(timeline.span_start_ns == *device.span_start_ns &&
                  timeline.span_end_ns == *device.span_end_ns,
              "timeline span equals device span");
      require(!timeline.intervals.empty(), "timeline non-empty");
      require(timeline.intervals.front().start_ns == timeline.span_start_ns,
              "first interval starts at span start");
      require(timeline.intervals.back().end_ns == timeline.span_end_ns,
              "last interval ends at span end");
      std::int64_t cursor = timeline.span_start_ns;
      std::int64_t covered_ns = 0;
      for (const StreamStateInterval& interval : timeline.intervals) {
        require(interval.end_ns > interval.start_ns, "interval positive");
        require(interval.start_ns == cursor, "intervals adjacent and sorted");
        cursor = interval.end_ns;
        covered_ns += interval.end_ns - interval.start_ns;
        if (interval.state == StreamState::kEmptyObserved) {
          require(interval.source_links.empty(),
                  "empty_observed carries no links");
        }
        if (interval.state == StreamState::kAmbiguousOverlap) {
          require(interval.source_links.size() >= 2,
                  "ambiguous_overlap carries at least two links");
          for (std::size_t a = 0; a < interval.source_links.size(); ++a) {
            for (std::size_t b = a + 1; b < interval.source_links.size();
                 ++b) {
              const StreamStateSourceLink& lhs = interval.source_links[a];
              const StreamStateSourceLink& rhs = interval.source_links[b];
              require(!(lhs.kind == rhs.kind &&
                        lhs.trace_event_id == rhs.trace_event_id &&
                        lhs.task_id == rhs.task_id &&
                        lhs.communication_op_id ==
                            rhs.communication_op_id),
                      "ambiguous_overlap links pairwise distinct");
            }
          }
        }
      }
      require(cursor == timeline.span_end_ns, "timeline covers span end");
      require(covered_ns == *device.span_end_ns - *device.span_start_ns,
              "interval durations sum to the span length");
    }
  }
  require(run.stream_universe_size == universe_sum,
          "run universe is the per-device sum");
  require(run.stream_universe_size == timeline_count,
          "universe equals emitted timelines");
  require(run.observed_universe_scan_complete ==
              (all_devices_complete && run.diagnostics.empty()),
          "run completeness is the device AND, voided by run-level damage");
}

bool throws_invalid_argument(const std::function<void()>& fn) {
  try {
    fn();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  // ---- Contract state names. ----
  require(stream_state_name(StreamState::kRunningCompute) ==
              "running_compute" &&
              stream_state_name(StreamState::kRunningComm) ==
                  "running_comm" &&
              stream_state_name(StreamState::kRunningDataMove) ==
                  "running_data_move" &&
              stream_state_name(StreamState::kRunningWait) ==
                  "running_wait" &&
              stream_state_name(StreamState::kRunningCaptureControl) ==
                  "running_capture_control" &&
              stream_state_name(StreamState::kRunningRecord) ==
                  "running_record" &&
              stream_state_name(StreamState::kRunningRuntimeControl) ==
                  "running_runtime_control" &&
              stream_state_name(StreamState::kUnknown) == "unknown" &&
              stream_state_name(StreamState::kEmptyObserved) ==
                  "empty_observed" &&
              stream_state_name(StreamState::kAmbiguousOverlap) ==
                  "ambiguous_overlap",
          "contract state names");

  // ---- 1. Single event. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100,
          .end_ns = 200,
          .role = SemanticTaskRole::kProductiveCompute}});
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "single event run ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "single event timeline exists");
    require(timeline->intervals.size() == 1, "single interval");
    require(timeline->intervals[0].state == StreamState::kRunningCompute &&
                timeline->intervals[0].start_ns == 100 &&
                timeline->intervals[0].end_ns == 200 &&
                timeline->intervals[0].source_links.size() == 1 &&
                timeline->intervals[0].source_links[0].kind ==
                    StreamStateSourceLink::Kind::kTask,
            "[100,200) running_compute {TASK}");
    require(timeline->diagnostics.empty(), "no diagnostics");
    check_stream_state_invariants(run);
  }

  // ---- 2. Middle gap: empty_observed between two events. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 200,
          .end_ns = 250,
          .role = SemanticTaskRole::kProductiveCompute}});
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "middle gap run ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "middle gap timeline exists");
    require(timeline->intervals.size() == 3, "three intervals");
    require(timeline->intervals[0].state == StreamState::kRunningCompute &&
                timeline->intervals[0].start_ns == 100 &&
                timeline->intervals[0].end_ns == 150,
            "[100,150) running_compute");
    require(timeline->intervals[1].state == StreamState::kEmptyObserved &&
                timeline->intervals[1].start_ns == 150 &&
                timeline->intervals[1].end_ns == 200 &&
                timeline->intervals[1].source_links.empty(),
            "[150,200) empty_observed");
    require(timeline->intervals[2].state == StreamState::kRunningCompute &&
                timeline->intervals[2].start_ns == 200 &&
                timeline->intervals[2].end_ns == 250,
            "[200,250) running_compute");
    check_stream_state_invariants(run);
  }

  // ---- 3. Adjacent distinct states: no merge, no ambiguity. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 150,
          .end_ns = 200,
          .role = SemanticTaskRole::kProductiveDataMove}});
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "adjacent states run ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "adjacent states timeline exists");
    require(timeline->intervals.size() == 2, "two intervals, no merge");
    require(timeline->intervals[0].state == StreamState::kRunningCompute &&
                timeline->intervals[1].state ==
                    StreamState::kRunningDataMove,
            "states in order");
    require(timeline->diagnostics.empty(), "no diagnostics");
    check_stream_state_invariants(run);
  }

  // ---- 4. Two-way overlap -> ambiguous_overlap partition (contract 3.3). ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100,
          .end_ns = 180,
          .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 150,
          .end_ns = 220,
          .role = SemanticTaskRole::kProductiveDataMove}});
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "2-way overlap run ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "2-way overlap timeline exists");
    require(timeline->intervals.size() == 3, "three segments");
    require(timeline->intervals[0].state == StreamState::kRunningCompute &&
                timeline->intervals[0].start_ns == 100 &&
                timeline->intervals[0].end_ns == 150 &&
                timeline->intervals[0].source_links.size() == 1,
            "[100,150) running_compute {A}");
    require(timeline->intervals[1].state == StreamState::kAmbiguousOverlap &&
                timeline->intervals[1].start_ns == 150 &&
                timeline->intervals[1].end_ns == 180 &&
                timeline->intervals[1].source_links.size() == 2,
            "[150,180) ambiguous_overlap {A,B}");
    require(timeline->intervals[2].state ==
                StreamState::kRunningDataMove &&
                timeline->intervals[2].start_ns == 180 &&
                timeline->intervals[2].end_ns == 220 &&
                timeline->intervals[2].source_links.size() == 1,
            "[180,220) running_data_move {B}");
    require(timeline->diagnostics.empty(),
            "partial overlap is a state, not a diagnostic");
    check_stream_state_invariants(run);
  }

  // ---- 5. Three-way overlap: every segment carries its own link set. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100,
          .end_ns = 200,
          .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 150,
          .end_ns = 250,
          .role = SemanticTaskRole::kProductiveDataMove},
         {.start_ns = 175,
          .end_ns = 225,
          .role = SemanticTaskRole::kVisibleWait}});
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "3-way overlap run ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "3-way overlap timeline exists");
    require(timeline->intervals.size() == 5, "five segments");
    require(timeline->intervals[0].state == StreamState::kRunningCompute &&
                timeline->intervals[0].source_links.size() == 1,
            "[100,150) running_compute {A}");
    require(timeline->intervals[1].state == StreamState::kAmbiguousOverlap &&
                timeline->intervals[1].start_ns == 150 &&
                timeline->intervals[1].end_ns == 175 &&
                timeline->intervals[1].source_links.size() == 2,
            "[150,175) ambiguous {A,B}");
    require(timeline->intervals[2].state == StreamState::kAmbiguousOverlap &&
                timeline->intervals[2].start_ns == 175 &&
                timeline->intervals[2].end_ns == 200 &&
                timeline->intervals[2].source_links.size() == 3,
            "[175,200) ambiguous {A,B,C}");
    require(timeline->intervals[3].state == StreamState::kAmbiguousOverlap &&
                timeline->intervals[3].start_ns == 200 &&
                timeline->intervals[3].end_ns == 225 &&
                timeline->intervals[3].source_links.size() == 2,
            "[200,225) ambiguous {B,C}");
    require(timeline->intervals[4].state ==
                StreamState::kRunningDataMove &&
                timeline->intervals[4].start_ns == 225 &&
                timeline->intervals[4].end_ns == 250 &&
                timeline->intervals[4].source_links.size() == 1,
            "[225,250) running_data_move {B}");
    // [150,175) and [175,200) are both ambiguous but with different link
    // sets: they must NOT merge.
    require(timeline->intervals[1].source_links !=
                timeline->intervals[2].source_links,
            "link sets differ, no merge across segments");
    check_stream_state_invariants(run);
  }

  // ---- 6. Multiple streams: independent timelines, universe size. ----
  {
    const Scene scene = make_scene(
        {{.device_id = 0,
          .stream_id = 0,
          .start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveCompute},
         {.device_id = 0,
          .stream_id = 1,
          .start_ns = 200,
          .end_ns = 250,
          .role = SemanticTaskRole::kProductiveDataMove}});
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "multi-stream run ok");
    require(run.stream_universe_size == 2, "two streams in the universe");
    const StreamStateTimeline* s0 = find_timeline(run, 0, 0);
    const StreamStateTimeline* s1 = find_timeline(run, 0, 1);
    require(s0 != nullptr && s1 != nullptr, "both timelines exist");
    require(s0->intervals.size() == 2 && s1->intervals.size() == 2,
            "each timeline has an empty_observed segment");
    require(s0->intervals[0].state == StreamState::kRunningCompute &&
                s1->intervals[1].state ==
                    StreamState::kRunningDataMove,
            "stream 0 computes while stream 1 moves data");
    check_stream_state_invariants(run);
  }

  // ---- 7. Event crossing the left span boundary. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 0,
          .end_ns = 500,
          .role = SemanticTaskRole::kProductiveCompute}},
        explicit_span(100, 600));
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "left clip run ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "left clip timeline exists");
    require(timeline->intervals.size() == 2, "two segments");
    require(timeline->intervals[0].state == StreamState::kRunningCompute &&
                timeline->intervals[0].start_ns == 100 &&
                timeline->intervals[0].end_ns == 500,
            "[100,500) running_compute");
    require(timeline->intervals[1].state == StreamState::kEmptyObserved &&
                timeline->intervals[1].start_ns == 500 &&
                timeline->intervals[1].end_ns == 600,
            "[500,600) empty_observed");
    require(has_diagnostic(timeline->diagnostics, "event_clipped_to_span"),
            "clip diagnostic emitted");
    check_stream_state_invariants(run);
  }

  // ---- 8. Event crossing the right span boundary. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 200,
          .end_ns = 700,
          .role = SemanticTaskRole::kProductiveCompute}},
        explicit_span(100, 600));
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "right clip run ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "right clip timeline exists");
    require(timeline->intervals.size() == 2, "two segments");
    require(timeline->intervals[0].state == StreamState::kEmptyObserved &&
                timeline->intervals[0].start_ns == 100 &&
                timeline->intervals[0].end_ns == 200,
            "[100,200) empty_observed");
    require(timeline->intervals[1].state == StreamState::kRunningCompute &&
                timeline->intervals[1].start_ns == 200 &&
                timeline->intervals[1].end_ns == 600,
            "[200,600) running_compute");
    require(has_diagnostic(timeline->diagnostics, "event_clipped_to_span"),
            "clip diagnostic emitted");
    check_stream_state_invariants(run);
  }

  // ---- 9. Fully outside events and boundary touch are silently ignored. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 1000,
          .end_ns = 1100,
          .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 200,
          .end_ns = 300,
          .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 600,
          .end_ns = 700,
          .role = SemanticTaskRole::kProductiveCompute}},
        explicit_span(100, 600));
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "outside events run ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "timeline exists");
    require(timeline->intervals.size() == 3, "three segments");
    require(timeline->intervals[0].state == StreamState::kEmptyObserved &&
                timeline->intervals[0].start_ns == 100 &&
                timeline->intervals[0].end_ns == 200,
            "[100,200) empty_observed");
    require(timeline->intervals[1].state == StreamState::kRunningCompute &&
                timeline->intervals[1].start_ns == 200 &&
                timeline->intervals[1].end_ns == 300,
            "[200,300) running_compute");
    require(timeline->intervals[2].state == StreamState::kEmptyObserved &&
                timeline->intervals[2].start_ns == 300 &&
                timeline->intervals[2].end_ns == 600,
            "[300,600) empty_observed");
    require(timeline->diagnostics.empty(),
            "fully outside events produce no diagnostics");
    check_stream_state_invariants(run);
  }

  // ---- 10. Exact duplicate: same lineage + interval + state, keep one. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveCompute,
          .shared_trace_event = true}});
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk,
            "exact duplicate keeps status ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "duplicate timeline exists");
    require(timeline->intervals.size() == 1, "one interval");
    require(timeline->intervals[0].state == StreamState::kRunningCompute &&
                timeline->intervals[0].source_links.size() == 1,
            "single link retained");
    require(has_diagnostic(timeline->diagnostics, "exact_duplicate_event"),
            "exact_duplicate_event diagnostic emitted");
    check_stream_state_invariants(run);
  }

  // ---- 11. Different lineage, same interval: ambiguous, not deduped. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveCompute}});
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk,
            "different lineage keeps status ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "coincident timeline exists");
    require(timeline->intervals.size() == 1, "one interval");
    require(timeline->intervals[0].state == StreamState::kAmbiguousOverlap &&
                timeline->intervals[0].source_links.size() == 2,
            "same interval, same state, different lineage -> ambiguous");
    require(has_diagnostic(timeline->diagnostics, "coincident_distinct_events"),
            "coincident diagnostic emitted");
    check_stream_state_invariants(run);
  }

  // ---- 12. Unknown role task -> unknown state. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 120,
          .end_ns = 150,
          .role = SemanticTaskRole::kUnknown}},
        explicit_span(100, 200));
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "unknown role run ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "unknown role timeline exists");
    require(timeline->intervals.size() == 3, "three segments");
    require(timeline->intervals[0].state == StreamState::kEmptyObserved &&
                timeline->intervals[0].start_ns == 100 &&
                timeline->intervals[0].end_ns == 120,
            "[100,120) empty_observed");
    require(timeline->intervals[1].state == StreamState::kUnknown &&
                timeline->intervals[1].start_ns == 120 &&
                timeline->intervals[1].end_ns == 150,
            "[120,150) unknown");
    require(timeline->intervals[2].state == StreamState::kEmptyObserved &&
                timeline->intervals[2].start_ns == 150 &&
                timeline->intervals[2].end_ns == 200,
            "[150,200) empty_observed");
    check_stream_state_invariants(run);
  }

  // ---- 13. No observed stream: zero timelines, status stays ok. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 1000,
          .end_ns = 1100,
          .role = SemanticTaskRole::kProductiveCompute}},
        explicit_span(100, 600));
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "no observed stream run ok");
    const StreamStateDeviceResult* device = find_device(run, 0);
    require(device != nullptr && device->status == AnalysisStatus::kOk,
            "device ok with explicit span");
    require(device->timelines.empty(), "no timelines");
    require(run.stream_universe_size == 0, "universe empty");
    require(device->diagnostics.empty(), "no diagnostics");
    check_stream_state_invariants(run);
  }

  // ---- 14. Invalid duration -> run kInvalidInput, healthy sibling intact. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100,
          .end_ns = 90,
          .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 200,
          .end_ns = 300,
          .role = SemanticTaskRole::kProductiveCompute}},
        explicit_span(100, 400));
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kInvalidInput,
            "invalid duration degrades the run");
    const StreamStateDeviceResult* device = find_device(run, 0);
    require(device != nullptr && device->status == AnalysisStatus::kOk,
            "device still ok");
    require(has_diagnostic(device->diagnostics, "invalid_event_duration"),
            "invalid_event_duration diagnostic emitted");
    require(!device->observed_universe_scan_complete,
            "damaged event voids the device scan completeness");
    require(!run.observed_universe_scan_complete,
            "run completeness reflects the device flag");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "healthy sibling timeline exists");
    require(timeline->intervals.size() == 3, "three segments");
    require(timeline->intervals[0].state == StreamState::kEmptyObserved &&
                timeline->intervals[0].start_ns == 100 &&
                timeline->intervals[0].end_ns == 200 &&
                timeline->intervals[1].state == StreamState::kRunningCompute &&
                timeline->intervals[1].start_ns == 200 &&
                timeline->intervals[1].end_ns == 300 &&
                timeline->intervals[2].state == StreamState::kEmptyObserved &&
                timeline->intervals[2].start_ns == 300 &&
                timeline->intervals[2].end_ns == 400,
            "invalid event skipped, span still partitioned");
    check_stream_state_invariants(run);
  }

  // ---- 15. Invalid trace_event_id -> run kInvalidInput, run-level diag. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveCompute,
          .invalid_trace_event = true},
         {.start_ns = 200,
          .end_ns = 300,
          .role = SemanticTaskRole::kProductiveCompute}},
        explicit_span(100, 400));
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kInvalidInput,
            "dangling reference degrades the run");
    require(has_diagnostic(run.diagnostics, "invalid_trace_event_reference"),
            "run-level invalid_trace_event_reference diagnostic");
    const StreamStateDeviceResult* device = find_device(run, 0);
    require(device != nullptr && device->observed_universe_scan_complete,
            "device with only healthy events stays complete");
    require(!run.observed_universe_scan_complete,
            "device-unattributable damage voids the run completeness");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "healthy sibling timeline exists");
    require(timeline->intervals[1].state == StreamState::kRunningCompute,
            "healthy event still partitioned");
    check_stream_state_invariants(run);
  }

  // ---- 16. Classification mismatch throws (both codes). ----
  {
    Scene scene = make_scene(
        {{.start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 200,
          .end_ns = 300,
          .role = SemanticTaskRole::kProductiveCompute}});
    scene.classification.rows[1].task_id = TaskId(7);
    require(throws_invalid_argument(
                [&scene]() { build_stream_states(scene); }),
            "classification_task_mismatch throws");
  }
  {
    Scene scene = make_scene(
        {{.start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 200,
          .end_ns = 300,
          .role = SemanticTaskRole::kProductiveCompute}});
    scene.classification.rows[0].trace_event_id = TraceEventId(7);
    require(throws_invalid_argument(
                [&scene]() { build_stream_states(scene); }),
            "classification_event_mismatch throws");
  }

  // ---- 17. Order independence (monolithic/split equivalence at E3 level):
  // the output depends only on the event multiset, not task-table order. ----
  {
    const std::vector<EventSpec> order_a = {
        {.start_ns = 100, .end_ns = 180,
         .role = SemanticTaskRole::kProductiveCompute},
        {.start_ns = 150, .end_ns = 220,
         .role = SemanticTaskRole::kVisibleWait},
        {.start_ns = 300, .end_ns = 350,
         .role = SemanticTaskRole::kRuntimeControl}};
    const std::vector<EventSpec> order_b = {order_a[2], order_a[0], order_a[1]};
    const Scene scene_a = make_scene(order_a);
    const Scene scene_b = make_scene(order_b);
    const StreamStateRunResult run_a = build_stream_states(scene_a);
    const StreamStateRunResult run_b = build_stream_states(scene_b);
    require(run_a.status == AnalysisStatus::kOk &&
                run_b.status == AnalysisStatus::kOk,
            "both orders run ok");
    require(run_a.stream_universe_size == run_b.stream_universe_size,
            "same universe size");
    const StreamStateTimeline* a = find_timeline(run_a, 0, 0);
    const StreamStateTimeline* b = find_timeline(run_b, 0, 0);
    require(a != nullptr && b != nullptr, "both timelines exist");
    require(a->intervals.size() == b->intervals.size(),
            "same interval count");
    for (std::size_t i = 0; i < a->intervals.size(); ++i) {
      require(a->intervals[i].start_ns == b->intervals[i].start_ns &&
                  a->intervals[i].end_ns == b->intervals[i].end_ns &&
                  a->intervals[i].state == b->intervals[i].state &&
                  a->intervals[i].source_links.size() ==
                      b->intervals[i].source_links.size(),
              "interval structure identical regardless of task order");
    }
    check_stream_state_invariants(run_a);
    check_stream_state_invariants(run_b);
  }

  // ---- 18. Absorbed task: one canonical running_comm event carrying the
  // op link plus the absorbed task link. Not ambiguous: ambiguity is
  // decided by the canonical event count, never the link count. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveComm,
          .connection_id = 42,
          .as_comm_op = true,
          .comm_name = "AllReduce",
          .op_name = "AllReduce"}});
    require(scene.productive.devices.size() == 1, "one E2 device");
    require(scene.productive.devices[0].absorbed_task_links.size() == 1,
            "E2 exported the absorbed task link");
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "absorbed task run ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "absorbed task timeline exists");
    require(timeline->intervals.size() == 1, "one interval");
    require(timeline->intervals[0].state == StreamState::kRunningComm &&
                timeline->intervals[0].start_ns == 100 &&
                timeline->intervals[0].end_ns == 150 &&
                timeline->intervals[0].source_links.size() == 2,
            "[100,150) running_comm with two source links");
    bool saw_op = false;
    bool saw_task = false;
    for (const StreamStateSourceLink& link :
         timeline->intervals[0].source_links) {
      if (link.kind == StreamStateSourceLink::Kind::kCommunicationOp) {
        saw_op = true;
      }
      if (link.kind == StreamStateSourceLink::Kind::kTask) {
        saw_task = true;
      }
    }
    require(saw_op && saw_task,
            "op link plus absorbed task link preserved");
    require(timeline->diagnostics.empty(),
            "no ambiguity from the absorbed task");
    check_stream_state_invariants(run);
  }

  // ---- 18b. Multiple absorbed tasks share one canonical op event: one
  // running_comm interval with the full lineage (op + every task). ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100,
          .end_ns = 180,
          .role = SemanticTaskRole::kProductiveComm,
          .connection_id = 42,
          .as_comm_op = true,
          .comm_name = "AllReduce",
          .op_name = "AllReduce"},
         {.start_ns = 120,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveComm,
          .connection_id = 42,
          .comm_name = "AllReduce",
          .shared_op = true}});
    require(scene.productive.devices[0].absorbed_task_links.size() == 2,
            "E2 exported both absorbed task links");
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "multi-absorbed run ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "multi-absorbed timeline exists");
    require(timeline->intervals.size() == 1, "one interval");
    require(timeline->intervals[0].state == StreamState::kRunningComm &&
                timeline->intervals[0].start_ns == 100 &&
                timeline->intervals[0].end_ns == 180 &&
                timeline->intervals[0].source_links.size() == 3,
            "one running_comm event with op + two absorbed task links");
    require(timeline->diagnostics.empty(),
            "still one canonical event, no ambiguity");
    check_stream_state_invariants(run);
  }

  // ---- 19. No absorption (task without connectionId + overlapping op):
  // both stay canonical and overlap -> ambiguous. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveComm,
          .as_comm_op = true}});
    require(scene.productive.devices[0].absorbed_task_links.empty(),
            "E2 kept the task canonical");
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "no-absorption run ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "no-absorption timeline exists");
    require(timeline->intervals.size() == 1, "one interval");
    require(timeline->intervals[0].state == StreamState::kAmbiguousOverlap &&
                timeline->intervals[0].source_links.size() == 2,
            "task and op overlap -> ambiguous with both links");
    require(has_diagnostic(timeline->diagnostics, "coincident_distinct_events"),
            "coincident diagnostic for the identical intervals");
    check_stream_state_invariants(run);
  }

  // ---- 20. E2 status mirroring. ----
  {
    // (a) No productive task at all -> kNoProductiveSpan, no timelines.
    const Scene scene_a = make_scene(
        {{.start_ns = 100,
          .end_ns = 200,
          .role = SemanticTaskRole::kVisibleWait}});
    const StreamStateRunResult run_a = build_stream_states(scene_a);
    require(run_a.status == AnalysisStatus::kOk,
            "run ok with no productive span on the device");
    const StreamStateDeviceResult* device_a = find_device(run_a, 0);
    require(device_a != nullptr &&
                device_a->status == AnalysisStatus::kNoProductiveSpan,
            "device mirrors kNoProductiveSpan");
    require(device_a->timelines.empty(), "no timelines without a span");
  }
  {
    // (b) Explicit span end <= start -> kInvalidAnalysisSpan.
    const Scene scene_b = make_scene(
        {{.start_ns = 100,
          .end_ns = 200,
          .role = SemanticTaskRole::kProductiveCompute}},
        explicit_span(200, 100));
    const StreamStateRunResult run_b = build_stream_states(scene_b);
    const StreamStateDeviceResult* device_b = find_device(run_b, 0);
    require(device_b != nullptr &&
                device_b->status == AnalysisStatus::kInvalidAnalysisSpan,
            "device mirrors kInvalidAnalysisSpan");
    require(device_b->timelines.empty(), "no timelines for invalid span");
  }
  {
    // (c) Empty IR -> kEmptyInput.
    const Scene scene_c = make_scene({});
    const StreamStateRunResult run_c = build_stream_states(scene_c);
    require(run_c.status == AnalysisStatus::kEmptyInput, "empty input");
    require(run_c.devices.empty(), "no devices");
    require(run_c.stream_universe_size == 0, "universe empty");
  }
  {
    // Invariants still hold for the mirroring cases that produced devices.
    const Scene scene_a = make_scene(
        {{.start_ns = 100,
          .end_ns = 200,
          .role = SemanticTaskRole::kVisibleWait}});
    check_stream_state_invariants(build_stream_states(scene_a));
    const Scene scene_b = make_scene(
        {{.start_ns = 100,
          .end_ns = 200,
          .role = SemanticTaskRole::kProductiveCompute}},
        explicit_span(200, 100));
    check_stream_state_invariants(build_stream_states(scene_b));
    const Scene scene_c = make_scene({});
    check_stream_state_invariants(build_stream_states(scene_c));
  }

  // ---- 21. Multi-device: independent timelines per device. ----
  {
    const Scene scene = make_scene(
        {{.device_id = 0,
          .stream_id = 0,
          .start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveCompute},
         {.device_id = 1,
          .stream_id = 0,
          .start_ns = 200,
          .end_ns = 250,
          .role = SemanticTaskRole::kProductiveDataMove}});
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "multi-device run ok");
    require(run.devices.size() == 2, "two device results");
    require(run.stream_universe_size == 2, "two timelines in the universe");
    const StreamStateTimeline* d0 = find_timeline(run, 0, 0);
    const StreamStateTimeline* d1 = find_timeline(run, 1, 0);
    require(d0 != nullptr && d1 != nullptr, "both device timelines exist");
    require(d0->intervals[0].state == StreamState::kRunningCompute &&
                d1->intervals[0].state == StreamState::kRunningDataMove,
            "per-device states independent");
    check_stream_state_invariants(run);
  }

  // ---- 22. Unknown stream identity -> kInvalidInput, healthy stream kept. ----
  {
    const Scene scene = make_scene(
        {{.stream_id = 7,
          .start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveCompute,
          .omit_stream_row = true},
         {.start_ns = 200,
          .end_ns = 300,
          .role = SemanticTaskRole::kProductiveCompute}},
        explicit_span(100, 400));
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kInvalidInput,
            "unknown stream degrades the run");
    const StreamStateDeviceResult* device = find_device(run, 0);
    require(device != nullptr, "device result exists");
    require(has_diagnostic(device->diagnostics, "unknown_stream_identity"),
            "unknown_stream_identity diagnostic emitted");
    require(!device->observed_universe_scan_complete,
            "unresolvable event voids the device scan completeness");
    require(find_timeline(run, 0, 7) == nullptr,
            "unresolvable stream emits no timeline");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "healthy stream timeline exists");
    check_stream_state_invariants(run);
  }

  // ---- 23. Clip at both boundaries: one diagnostic. ----
  {
    const Scene scene = make_scene(
        {{.start_ns = 0,
          .end_ns = 1000,
          .role = SemanticTaskRole::kProductiveCompute}},
        explicit_span(100, 900));
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk, "double clip run ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr, "double clip timeline exists");
    require(timeline->intervals.size() == 1, "one interval");
    require(timeline->intervals[0].state == StreamState::kRunningCompute &&
                timeline->intervals[0].start_ns == 100 &&
                timeline->intervals[0].end_ns == 900,
            "[100,900) running_compute");
    std::size_t clip_count = 0;
    for (const TimelineDiagnostic& diagnostic : timeline->diagnostics) {
      if (diagnostic.message.find("event_clipped_to_span") == 0) {
        ++clip_count;
      }
    }
    require(clip_count == 1, "exactly one clip diagnostic per event");
    check_stream_state_invariants(run);
  }

  // ---- 24. Unassigned stream (0xFFFFFFFF sentinel): observable but not
  // placeable; unassigned_stream diagnostic, run status stays ok, no fake
  // timeline, device scan completeness voided. ----
  {
    const Scene scene = make_scene(
        {{.stream_id = 5,
          .start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveCompute},
         {.start_ns = 100,
          .end_ns = 150,
          .role = SemanticTaskRole::kProductiveComm,
          .as_comm_op = true,
          .op_stream_id = kUnassignedStreamSentinel}});
    const StreamStateRunResult run = build_stream_states(scene);
    require(run.status == AnalysisStatus::kOk,
            "sentinel event is not corrupt input; run stays ok");
    const StreamStateDeviceResult* device = find_device(run, 0);
    require(device != nullptr && device->status == AnalysisStatus::kOk,
            "device still ok");
    require(has_diagnostic(device->diagnostics, "unassigned_stream"),
            "unassigned_stream diagnostic emitted");
    require(!has_diagnostic(device->diagnostics, "unknown_stream_identity"),
            "sentinel is not an unknown stream identity");
    require(!device->observed_universe_scan_complete,
            "unassignable event voids the device scan completeness");
    require(!run.observed_universe_scan_complete,
            "run completeness reflects the device flag");
    require(find_timeline(run, 0, kUnassignedStreamSentinel) == nullptr,
            "no fake timeline for the sentinel stream");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 5);
    require(timeline != nullptr, "healthy stream timeline exists");
    require(timeline->intervals[0].state == StreamState::kRunningCompute,
            "healthy stream unaffected");
    check_stream_state_invariants(run);
  }

  // ---- 25. Large-scale sweep: 10k adjacent events on one stream. The
  // partition must stay linear-ish; a quadratic regression would blow the
  // generous time tripwire. ----
  {
    constexpr std::size_t kEventCount = 10000;
    std::vector<EventSpec> specs;
    specs.reserve(kEventCount);
    for (std::size_t index = 0; index < kEventCount; ++index) {
      specs.push_back(
          {.start_ns = static_cast<std::int64_t>(index),
           .end_ns = static_cast<std::int64_t>(index + 1),
           .role = SemanticTaskRole::kProductiveCompute});
    }
    const auto begin = std::chrono::steady_clock::now();
    const Scene scene = make_scene(specs);
    const StreamStateRunResult run = build_stream_states(scene);
    const auto end = std::chrono::steady_clock::now();
    const std::int64_t elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
            .count();
    require(run.status == AnalysisStatus::kOk, "10k adjacent run ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr &&
                timeline->intervals.size() == kEventCount,
            "10k adjacent events -> 10k intervals, none merged");
    require(elapsed_ms < 10000,
            "10k-event sweep must stay sub-quadratic");
    std::cout << "  (10k adjacent events: " << elapsed_ms << " ms)\n";
    check_stream_state_invariants(run);
  }

  // ---- 26. Large-scale sweep: 10k sparse events. ----
  {
    constexpr std::size_t kEventCount = 10000;
    std::vector<EventSpec> specs;
    specs.reserve(kEventCount);
    for (std::size_t index = 0; index < kEventCount; ++index) {
      const std::int64_t start = static_cast<std::int64_t>(index) * 10;
      specs.push_back(
          {.start_ns = start,
           .end_ns = start + 1,
           .role = SemanticTaskRole::kProductiveCompute});
    }
    const auto begin = std::chrono::steady_clock::now();
    const Scene scene = make_scene(specs);
    const StreamStateRunResult run = build_stream_states(scene);
    const auto end = std::chrono::steady_clock::now();
    const std::int64_t elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
            .count();
    require(run.status == AnalysisStatus::kOk, "10k sparse run ok");
    const StreamStateTimeline* timeline = find_timeline(run, 0, 0);
    require(timeline != nullptr &&
                timeline->intervals.size() == kEventCount * 2 - 1,
            "10k sparse events -> compute + gap intervals (span ends at the "
            "last event end)");
    require(elapsed_ms < 10000, "10k sparse sweep must stay sub-quadratic");
    std::cout << "  (10k sparse events: " << elapsed_ms << " ms)\n";
    check_stream_state_invariants(run);
  }

  // ---- 27. Large-scale sweep: many events concentrated on few streams. ----
  {
    constexpr std::size_t kEventCount = 10000;
    constexpr std::uint32_t kStreamCount = 4;
    std::vector<EventSpec> specs;
    specs.reserve(kEventCount);
    for (std::size_t index = 0; index < kEventCount; ++index) {
      const std::uint32_t stream = static_cast<std::uint32_t>(index % kStreamCount);
      const std::int64_t local = static_cast<std::int64_t>(index / kStreamCount);
      specs.push_back(
          {.stream_id = stream,
           .start_ns = local,
           .end_ns = local + 1,
           .role = SemanticTaskRole::kProductiveCompute});
    }
    const auto begin = std::chrono::steady_clock::now();
    const Scene scene = make_scene(specs);
    const StreamStateRunResult run = build_stream_states(scene);
    const auto end = std::chrono::steady_clock::now();
    const std::int64_t elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
            .count();
    require(run.status == AnalysisStatus::kOk, "concentrated run ok");
    require(run.stream_universe_size == kStreamCount,
            "four observed streams");
    for (std::uint32_t stream = 0; stream < kStreamCount; ++stream) {
      const StreamStateTimeline* timeline = find_timeline(run, 0, stream);
      require(timeline != nullptr &&
                  timeline->intervals.size() == kEventCount / kStreamCount,
              "each stream carries its share of intervals");
    }
    require(elapsed_ms < 10000,
            "concentrated sweep must stay sub-quadratic");
    std::cout << "  (10k events on 4 streams: " << elapsed_ms << " ms)\n";
    check_stream_state_invariants(run);
  }

  std::cout << "PASS: stream state timeline\n";
  return 0;
}
