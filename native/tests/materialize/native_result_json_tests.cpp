#include "traceloom/analysis/native_pipeline.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/materialize/native_result_json.h"
#include "traceloom/testing/test_util.h"

#include <sstream>
#include <string>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "json_smoke", "TASK", 0);
  const SymbolId a = ir.symbols.intern("A");
  const SymbolId b = ir.symbols.intern("B");
  const TraceEventId event0 = ir.trace_events.append(source, 1, 0, 0, 0, 10, a);
  const TraceEventId event1 = ir.trace_events.append(source, 2, 0, 0, 10, 20, b);
  const TraceEventId event2 = ir.trace_events.append(source, 3, 0, 0, 20, 30, a);
  const TraceEventId event3 = ir.trace_events.append(source, 4, 0, 0, 30, 40, b);
  ir.tasks.append(source, event0, 1, 1001, -1, a, SymbolId::invalid(), a,
                  SymbolId::invalid(), SymbolId::invalid());
  ir.tasks.append(source, event1, 2, 1002, -1, b, SymbolId::invalid(), b,
                  SymbolId::invalid(), SymbolId::invalid());
  ir.tasks.append(source, event2, 3, 1003, -1, a, SymbolId::invalid(), a,
                  SymbolId::invalid(), SymbolId::invalid());
  ir.tasks.append(source, event3, 4, 1004, -1, b, SymbolId::invalid(), b,
                  SymbolId::invalid(), SymbolId::invalid());

  NativePipelineOptions pipeline_options;
  pipeline_options.thread_count = 2;
  pipeline_options.partition_config = PartitionPlanConfig{2, 3};
  pipeline_options.candidate_scan_config = CandidateScanConfig{2, 3};
  const NativePipelineResult result =
      run_native_pipeline(ir, pipeline_options);

  NativeResultJsonOptions json_options;
  json_options.source_kind = "fixture";
  json_options.source_path = "json_smoke";
  json_options.thread_count = 2;
  json_options.top_candidate_limit = 2;

  std::ostringstream out;
  write_native_result_json(out, ir.symbols, result, json_options);
  const std::string json = out.str();

  require(json.find("\"schema_version\": \"native_in_memory_result_v1\"") !=
          std::string::npos);
  require(json.find("\"kind\": \"fixture\"") != std::string::npos);
  require(json.find("\"trace_event_count\": 4") != std::string::npos);
  require(json.find("\"candidate_distinct_count\"") != std::string::npos);
  require(json.find("\"candidates_preview\"") != std::string::npos);

  return 0;
}
