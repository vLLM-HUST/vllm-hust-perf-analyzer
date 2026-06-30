#include "traceloom/analysis/native_pipeline.h"
#include "traceloom/testing/test_util.h"

#include <vector>

namespace {

traceloom::NativeIr make_ir() {
  using namespace traceloom;

  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "memory", "TASK", 0);
  const SymbolId a = ir.symbols.intern("A");
  const SymbolId b = ir.symbols.intern("B");
  const SymbolId c = ir.symbols.intern("C");

  const TraceEventId event0 = ir.trace_events.append(source, 1, 0, 0, 0, 10, a);
  const TraceEventId event1 = ir.trace_events.append(source, 2, 0, 0, 10, 20, b);
  const TraceEventId event2 = ir.trace_events.append(source, 3, 0, 0, 20, 30, c);
  const TraceEventId event3 = ir.trace_events.append(source, 4, 0, 0, 30, 40, a);
  const TraceEventId event4 = ir.trace_events.append(source, 5, 0, 0, 40, 50, b);

  ir.tasks.append(source, event0, 1, 1001, -1, a, SymbolId::invalid(), a,
                  SymbolId::invalid(), SymbolId::invalid());
  ir.tasks.append(source, event1, 2, 1002, -1, b, SymbolId::invalid(), b,
                  SymbolId::invalid(), SymbolId::invalid());
  ir.tasks.append(source, event2, 3, 1003, -1, c, SymbolId::invalid(), c,
                  SymbolId::invalid(), SymbolId::invalid());
  ir.tasks.append(source, event3, 4, 1004, -1, a, SymbolId::invalid(), a,
                  SymbolId::invalid(), SymbolId::invalid());
  ir.tasks.append(source, event4, 5, 1005, -1, b, SymbolId::invalid(), b,
                  SymbolId::invalid(), SymbolId::invalid());
  return ir;
}

bool summaries_equal(const traceloom::PatternCandidateSummaryTable& lhs,
                     const traceloom::PatternCandidateSummaryTable& rhs) {
  if (lhs.rows.size() != rhs.rows.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.rows.size(); ++index) {
    if (!(lhs.rows[index].key == rhs.rows[index].key)) {
      return false;
    }
    if (lhs.rows[index].occurrence_count != rhs.rows[index].occurrence_count) {
      return false;
    }
    if (lhs.rows[index].first_begin != rhs.rows[index].first_begin) {
      return false;
    }
  }
  return true;
}

bool candidate_tables_equal(const traceloom::PatternCandidateTable& lhs,
                            const traceloom::PatternCandidateTable& rhs) {
  if (lhs.rows.size() != rhs.rows.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.rows.size(); ++index) {
    if (!(lhs.rows[index].key == rhs.rows[index].key)) {
      return false;
    }
    if (lhs.rows[index].begin != rhs.rows[index].begin) {
      return false;
    }
    if (lhs.rows[index].end != rhs.rows[index].end) {
      return false;
    }
    if (lhs.rows[index].partition_id != rhs.rows[index].partition_id) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativePipelineOptions options;
  options.partition_config = PartitionPlanConfig{2, 3};
  options.candidate_scan_config = CandidateScanConfig{2, 3};

  NativeIr ir1 = make_ir();
  options.thread_count = 1;
  const NativePipelineResult one_thread = run_native_pipeline(ir1, options);

  NativeIr ir4 = make_ir();
  options.thread_count = 4;
  const NativePipelineResult four_threads = run_native_pipeline(ir4, options);

  require(one_thread.stats.trace_event_count == 5);
  require(one_thread.stats.anchor_count == 5);
  require(one_thread.stats.token_count == 5);
  require(one_thread.stats.candidate_occurrence_count > 0);
  require(one_thread.stats.candidate_distinct_count ==
          one_thread.pattern_candidate_summary.rows.size());
  require(one_thread.stats.candidate_occurrence_count ==
          one_thread.pattern_candidate_table.rows.size());
  require(one_thread.pattern_mining_diagnostics.rows.size() ==
          one_thread.stats.candidate_diagnostic_count);
  require(one_thread.cost_summary_lite.anchor_count == 5);
  require(one_thread.memory.trace_event_bytes > 0);
  require(one_thread.memory.anchor_bytes > 0);
  require(one_thread.memory.candidate_occurrence_bytes > 0);

  require(candidate_tables_equal(one_thread.pattern_candidate_table,
                                 four_threads.pattern_candidate_table));
  require(summaries_equal(one_thread.pattern_candidate_summary,
                          four_threads.pattern_candidate_summary));
  require(one_thread.stats.candidate_occurrence_count ==
          four_threads.stats.candidate_occurrence_count);
  require(one_thread.stats.candidate_diagnostic_count ==
          four_threads.stats.candidate_diagnostic_count);

  return 0;
}
