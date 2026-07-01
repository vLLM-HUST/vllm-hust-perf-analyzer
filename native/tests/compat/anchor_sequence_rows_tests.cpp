#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>
#include <string>
#include <vector>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "memory", "TASK", 0);
  const SymbolId matmul = ir.symbols.intern("MatMul");
  const SymbolId all_reduce = ir.symbols.intern("HcclAllReduce");
  const SymbolId graph = ir.symbols.intern("ACLGraph");

  const TraceEventId event0 =
      ir.trace_events.append(source, 10, 0, 3, 1000, 1500, matmul);
  const TraceEventId event1 =
      ir.trace_events.append(source, 11, 0, 3, 2000, 2600, all_reduce);

  const AnchorId anchor0 =
      ir.anchors.append(source, event0, ReplayUnitId::invalid(),
                        AnchorKind::kDeviceEvent, matmul, 0, 3, 1000, 1500);
  const AnchorId anchor1 =
      ir.anchors.append(source, event1, ReplayUnitId::invalid(),
                        AnchorKind::kCommunication, all_reduce, 0, 3, 2000,
                        2600);
  const AnchorId anchor2 =
      ir.anchors.append(source, TraceEventId::invalid(), ReplayUnitId(0),
                        AnchorKind::kGraphReplayUnit, graph, 0, 3, 3000,
                        5000);

  const std::vector<compat::AnchorSqlRow> rows =
      compat::build_anchor_sequence_sql_rows(ir, 4);
  require(rows.size() == 3);

  require(compat::anchor_compat_id(anchor0) == "anchor-0");
  require(rows[0].anchor_id == "anchor-0");
  require(rows[0].db_idx == 4);
  require(rows[0].device_id == 0);
  require(rows[0].anchor_idx == 1);
  require(rows[0].event_id == "event-0");
  require(rows[0].step_idx == 0);
  require(rows[0].symbol == "MatMul");
  require(rows[0].role == "compute");
  require(rows[0].family == "compute");
  require(rows[0].start_ns == 1000);
  require(rows[0].end_ns == 1500);
  require(rows[0].dur_us == 0.5);

  require(compat::anchor_compat_id(anchor1) == "anchor-1");
  require(rows[1].anchor_idx == 2);
  require(rows[1].event_id == "event-1");
  require(rows[1].symbol == "HcclAllReduce");
  require(rows[1].role == "comm");

  require(compat::anchor_compat_id(anchor2) == "anchor-2");
  require(rows[2].anchor_idx == 3);
  require(rows[2].event_id.empty());
  require(rows[2].step_idx == 2);
  require(rows[2].symbol == "ACLGraph");
  require(rows[2].role == "graph");
  require(rows[2].dur_us == 2.0);

  NativeIr bad_ir;
  const SourceRefId bad_source =
      bad_ir.source_refs.append("fixture", "bad", "TASK", 0);
  const SymbolId bad_symbol = bad_ir.symbols.intern("Bad");
  bad_ir.anchors.append(bad_source, TraceEventId(99), ReplayUnitId::invalid(),
                        AnchorKind::kDeviceEvent, bad_symbol, 0, 0, 0, 1);
  bool rejected_bad_anchor_ref = false;
  try {
    (void)compat::build_anchor_sequence_sql_rows(bad_ir);
  } catch (const std::invalid_argument&) {
    rejected_bad_anchor_ref = true;
  }
  require(rejected_bad_anchor_ref);

  return 0;
}
