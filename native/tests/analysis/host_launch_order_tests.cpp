#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/analysis/host_launch_order.h"
#include "traceloom/compat/structural_projection_rows.h"
#include "traceloom/testing/test_util.h"
#include <map>
#include <algorithm>
#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/pattern/grammar_state.h"
using namespace traceloom;
using traceloom::testing::require;

NativeIr fixture(bool multithread = false, bool tie = false,
                 bool reuse = false, bool same_stream = false,
                 bool other_file = false, bool two_compute = false) {
  NativeIr ir;
  auto ts = ir.source_refs.append("ascend", "fixture.db", "TASK", 0);
  auto cs = ir.source_refs.append("ascend", "fixture.db", "COMMUNICATION_OP", 0);
  auto hs = ir.source_refs.append("ascend", other_file ? "other.db" : "fixture.db", "CANN_API", 0);
  auto a = ir.symbols.intern("MatMul"), b = ir.symbols.intern("AllGather");
  auto e0 = ir.trace_events.append(ts, 1, 0, 1, 100, 200, a);
  auto e1 = ir.trace_events.append(cs, 2, 0, same_stream ? 1 : 2, 110, 210, b);
  ir.tasks.append(ts, e0, 1, -1, 1, a, a, a, a, SymbolId::invalid());
  if (two_compute)
    ir.tasks.append(cs, e1, 2, -1, 2, b, b, b, b, SymbolId::invalid());
  else ir.communication_ops.append(cs, e1, reuse ? 1 : 2, 2, 0, 0, b);
  for (int i = 0; i < (reuse ? 1 : 2); ++i)
    ir.runtime_calls.append(hs, i + 1, RuntimeCallProvider::kAscend,
      RuntimeCallClockDomain::kProfilerHost, RuntimeCallMatchPolicy::kAscendConnectionId,
      i == 0 || tie ? 20 : 10, 30, i + 1, ir.symbols.intern("node"),
      ir.symbols.intern("launch"), -1, -1, multithread ? i + 1 : 1);
  build_flat_anchors(ir);
  return ir;
}
int main() {
  auto ir = fixture();
  const auto before = compat::build_structural_projection_tokens_from_native_ir(ir);
  apply_host_launch_order(ir);
  require(ir.tokens.size() == 2 && ir.anchors.size() == 2);
  require(ir.tokens.row(TokenId(0)).anchor_id == AnchorId(1));
  require(ir.tokens.row(TokenId(0)).start_ns == 110);
  require(ir.tokens.row(TokenId(1)).start_ns == 100);
  require(ir.tokens.row(TokenId(0)).order_runtime_call_id.valid());
  const auto after = compat::build_structural_projection_tokens_from_native_ir(ir);
  require(before[0].timeline_anchor_us == after[1].timeline_anchor_us);
  require(before[1].timeline_anchor_us == after[0].timeline_anchor_us);
  require(before[0].prelude_idle_us == after[1].prelude_idle_us);
  for (auto bad : {fixture(true), fixture(false,true), fixture(false,false,true),
                   fixture(false,false,false,true), fixture(false,false,false,false,true)}) {
    apply_host_launch_order(bad);
    require(bad.tokens.row(TokenId(0)).anchor_id == AnchorId(0));
    require(!bad.tokens.row(TokenId(0)).order_runtime_call_id.valid());
  }
  auto compute = fixture(false,false,false,false,false,true);
  auto compute_before = compat::build_structural_projection_tokens_from_native_ir(compute);
  apply_host_launch_order(compute);
  auto compute_after = compat::build_structural_projection_tokens_from_native_ir(compute);
  require(compute_before[0].timeline_anchor_us == compute_after[1].timeline_anchor_us);
  require(compute_before[1].timeline_anchor_us == compute_after[0].timeline_anchor_us);
  // Repeated grammar phrases must use min/max geometry, not first/last time.
  NativeIr grammar;
  auto a = grammar.symbols.intern("A"), b = grammar.symbols.intern("B");
  for (int i = 0; i < 8; ++i)
    grammar.tokens.append(AnchorId(i),i%2 ? b : a,0,i,
                          i%2 ? 100 : 110,i%2 ? 120 : 210);
  auto state = build_initial_grammar_state(grammar);
  auto result = run_grammar_state_machine(state);
  require(result.ok());
  require(!state.macro_defs.empty());
  for (const auto& n : state.nodes) {
    auto low = std::int64_t(210), high = std::int64_t(0);
    for (auto i = n.source_begin_token_index; i < n.source_end_token_index_exclusive; ++i) {
      low = std::min(low,grammar.tokens.row(TokenId(i)).start_ns);
      high = std::max(high,grammar.tokens.row(TokenId(i)).end_ns);
    }
    require(n.start_ns == low && n.end_ns == high);
  }
  auto protected_ir = fixture();
  protected_ir.protected_intervals.append(ProtectedIntervalKind::kGraphReplayUnit,
      BoundaryPolicy::kNoCross,TokenId(0),TokenId(1),AnchorId(0),AnchorId(1),SourceRefId(0));
  apply_host_launch_order(protected_ir);
  require(protected_ir.tokens.row(TokenId(0)).anchor_id == AnchorId(0));
  require(protected_ir.protected_intervals.size() == 1);
  // An unmapped event is retained and prevents moving across its position.
  auto barrier = fixture();
  TokenTable tokens;
  tokens.append(AnchorId(0), ir.symbols.intern("MatMul"), 0, 0, 100, 200);
  auto src = barrier.source_refs.append("ascend", "fixture.db", "TASK", 0);
  auto ev = barrier.trace_events.append(src, 3, 0, 3, 105, 106, SymbolId::invalid());
  auto aid = barrier.anchors.append(src,ev,ReplayUnitId::invalid(),AnchorKind::kUnknown,
                                   SymbolId::invalid(),0,3,105,106);
  tokens.append(aid,SymbolId::invalid(),0,1,105,106);
  tokens.append(AnchorId(1),SymbolId::invalid(),0,2,110,210);
  barrier.tokens=std::move(tokens);
  apply_host_launch_order(barrier);
  require(barrier.tokens.row(TokenId(0)).anchor_id==AnchorId(0));
  require(barrier.tokens.row(TokenId(1)).anchor_id==aid);
  require(barrier.tokens.row(TokenId(2)).anchor_id==AnchorId(1));
}
