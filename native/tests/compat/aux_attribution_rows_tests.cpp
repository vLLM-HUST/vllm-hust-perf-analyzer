#include "traceloom/compat/aux_attribution_rows.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "memory", "TASK", 0);
  const SymbolId memcpy = ir.symbols.intern("Memcpy");
  const SymbolId matmul = ir.symbols.intern("MatMul");
  const SymbolId late = ir.symbols.intern("LateTask");

  const TraceEventId aux_event =
      ir.trace_events.append(source, 1, 0, 3, 1000, 1500, memcpy);
  const TraceEventId anchor_event =
      ir.trace_events.append(source, 2, 0, 3, 2000, 3000, matmul);
  ir.trace_events.append(source, 3, 0, 3, 3500, 3600, late);
  const AnchorId anchor =
      ir.anchors.append(source, anchor_event, ReplayUnitId::invalid(),
                        AnchorKind::kDeviceEvent, matmul, 0, 3, 2000, 3000);
  ir.tokens.append(anchor, matmul, 0, 0, 2000, 3000);

  const compat::AuxAttributionSqlRows rows =
      compat::build_aux_attribution_sql_rows(ir, 9);
  require(rows.aux_links.size() == 1);
  require(rows.aux_slots.size() == 1);

  require(rows.aux_links[0].anchor_id == "anchor-0");
  require(rows.aux_links[0].aux_event_id == "event-0");
  require(rows.aux_links[0].db_idx == 9);
  require(rows.aux_links[0].device_id == 0);
  require(rows.aux_links[0].aux_order == 1);
  require(rows.aux_links[0].aux_step_idx == aux_event.value());
  require(rows.aux_links[0].link_type == "prelude");
  require(rows.aux_links[0].reason == "unanchored_event_before_anchor");
  require(rows.aux_links[0].aux_kind == "Memcpy");
  require(rows.aux_links[0].aux_dur_us == 0.5);

  require(rows.aux_slots[0].anchor_id == "anchor-0");
  require(rows.aux_slots[0].anchor_idx == 1);
  require(rows.aux_slots[0].anchor_step_idx == anchor_event.value());
  require(rows.aux_slots[0].aux_start_step_idx == aux_event.value());
  require(rows.aux_slots[0].aux_end_step_idx == aux_event.value());
  require(rows.aux_slots[0].aux_event_count == 1);
  require(rows.aux_slots[0].aux_dur_us == 0.5);

  NativeIr bad_ir;
  const SourceRefId bad_source =
      bad_ir.source_refs.append("fixture", "bad", "TASK", 0);
  const SymbolId bad_symbol = bad_ir.symbols.intern("Bad");
  bad_ir.anchors.append(bad_source, TraceEventId(42), ReplayUnitId::invalid(),
                        AnchorKind::kDeviceEvent, bad_symbol, 0, 0, 0, 1);
  bool rejected_bad_anchor_ref = false;
  try {
    (void)compat::build_aux_attribution_sql_rows(bad_ir);
  } catch (const std::invalid_argument&) {
    rejected_bad_anchor_ref = true;
  }
  require(rejected_bad_anchor_ref);

  return 0;
}
