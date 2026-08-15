#include "traceloom/compat/aux_attribution_rows.h"
#include "traceloom/compat/structural_projection_rows.h"
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

  // Policy cost treatment is executable rather than descriptive metadata.
  // The same EVENT_WAIT remains normalized evidence in both cases, but only
  // retained_for_attribution may enter the next anchor's transition overlay.
  NativeIr policy_ir;
  const SourceRefId policy_source = policy_ir.source_refs.append(
      "ascend_sqlite_hot_path", "memory", "TASK", 0);
  const SymbolId ai_core = policy_ir.symbols.intern("AI_CORE");
  const SymbolId event_wait = policy_ir.symbols.intern("EVENT_WAIT");
  const SymbolId matmul_v2 = policy_ir.symbols.intern("MatMulV2");
  const TraceEventId first_event = policy_ir.trace_events.append(
      policy_source, 10, 0, 1, 0, 1000, matmul_v2);
  const TraceEventId wait_event = policy_ir.trace_events.append(
      policy_source, 11, 0, 1, 2000, 5000, event_wait);
  const TraceEventId second_event = policy_ir.trace_events.append(
      policy_source, 12, 0, 1, 10000, 11000, matmul_v2);
  policy_ir.tasks.append(policy_source, first_event, 10, 10, -1, ai_core,
                         matmul_v2, matmul_v2, ai_core,
                         SymbolId::invalid());
  policy_ir.tasks.append(policy_source, wait_event, 11, 11, -1, event_wait,
                         SymbolId::invalid(), SymbolId::invalid(),
                         SymbolId::invalid(), SymbolId::invalid());
  policy_ir.tasks.append(policy_source, second_event, 12, 12, -1, ai_core,
                         matmul_v2, matmul_v2, ai_core,
                         SymbolId::invalid());

  FlatAnchorBuildConfig retained_config;
  retained_config.filter_auxiliary_task_anchors = true;
  retained_config.classification_rules =
      load_default_signal_classification_ruleset();
  build_flat_anchors(policy_ir, retained_config);
  require(policy_ir.anchors.size() == 2);
  const compat::AuxAttributionSqlRows retained_rows =
      compat::build_aux_attribution_sql_rows(policy_ir, retained_config);
  require(retained_rows.aux_links.size() == 1);
  require(retained_rows.aux_links[0].aux_event_id == "event-1");
  const std::vector<StructuralProjectionToken> retained_tokens =
      compat::build_structural_projection_tokens_from_native_ir(policy_ir, retained_config);
  require(retained_tokens.size() == 2);
  require(retained_tokens[1].prelude_aux_event_count == 1.0);
  require(retained_tokens[1].prelude_aux_us == 3.0);

  FlatAnchorBuildConfig evidence_only_config = retained_config;
  evidence_only_config.classification_overrides.push_back(
      parse_signal_classification_override(
          "ascend.aux.task.type.event.wait.28ab0f94.cost_treatment="
          "retained_as_evidence"));
  const compat::AuxAttributionSqlRows evidence_only_rows =
      compat::build_aux_attribution_sql_rows(policy_ir,
                                             evidence_only_config);
  require(evidence_only_rows.aux_links.empty());
  const std::vector<StructuralProjectionToken> evidence_only_tokens =
      compat::build_structural_projection_tokens_from_native_ir(policy_ir,
                                                 evidence_only_config);
  require(evidence_only_tokens.size() == 2);
  require(evidence_only_tokens[1].prelude_aux_event_count == 0.0);
  require(evidence_only_tokens[1].prelude_aux_us == 0.0);

  return 0;
}
