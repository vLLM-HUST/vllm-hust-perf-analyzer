#include "traceloom/adapters/aclgraph_fixture_adapter.h"
#include "traceloom/compat/aclgraph_graph_replay_rows.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  AclGraphSemanticFixture fixture;
  fixture.fixture_id = "aclgraph-test";

  AclGraphReplayActivityFixtureRow activity;
  activity.replay_activity_id = "activity-1";
  activity.stream_ids = {7};
  fixture.replay_activities.push_back(activity);

  AclGraphReplayUnitFixtureRow unit;
  unit.replay_unit_id = "unit-1";
  unit.replay_activity_id = activity.replay_activity_id;
  unit.unit_idx_global = 0;
  unit.start_ns = 1000;
  unit.end_ns = 7000;
  fixture.replay_units.push_back(unit);

  AclGraphReplayTilingFixtureRow tiling;
  tiling.replay_tiling_id = "tiling-1";
  tiling.replay_unit_id = unit.replay_unit_id;
  fixture.replay_tilings.push_back(tiling);

  AclGraphReplaySubslotFixtureRow subslot_a;
  subslot_a.subslot_id = "subslot-a";
  subslot_a.replay_tiling_id = tiling.replay_tiling_id;
  subslot_a.subslot_idx = 1;
  subslot_a.slot_kind = "kernel";
  subslot_a.slot_symbol = "T";
  subslot_a.start_ns = 4000;
  subslot_a.end_ns = 6000;
  subslot_a.stream_id = 8;
  subslot_a.raw_child_task_count = 2;
  fixture.replay_subslots.push_back(subslot_a);

  AclGraphReplaySubslotFixtureRow subslot_b;
  subslot_b.subslot_id = "subslot-b";
  subslot_b.replay_tiling_id = tiling.replay_tiling_id;
  subslot_b.subslot_idx = 0;
  subslot_b.slot_kind = "kernel";
  subslot_b.slot_symbol = "H";
  subslot_b.start_ns = 1500;
  subslot_b.end_ns = 2500;
  subslot_b.stream_id = 7;
  subslot_b.raw_child_task_count = 1;
  fixture.replay_subslots.push_back(subslot_b);

  AclGraphHltAnchorSeedFixtureRow anchor_h;
  anchor_h.replay_unit_id = unit.replay_unit_id;
  anchor_h.subslot_id = subslot_b.subslot_id;
  anchor_h.symbol = "H";
  anchor_h.slot_symbol = "H";
  anchor_h.start_ns = subslot_b.start_ns;
  anchor_h.end_ns = subslot_b.end_ns;
  fixture.hlt_anchor_seeds.push_back(anchor_h);

  AclGraphHltAnchorSeedFixtureRow anchor_t;
  anchor_t.replay_unit_id = unit.replay_unit_id;
  anchor_t.subslot_id = subslot_a.subslot_id;
  anchor_t.symbol = "T";
  anchor_t.slot_symbol = "T";
  anchor_t.start_ns = subslot_a.start_ns;
  anchor_t.end_ns = subslot_a.end_ns;
  fixture.hlt_anchor_seeds.push_back(anchor_t);

  const NativeIr ir = AclGraphFixtureAdapter(fixture).load();
  const compat::GraphReplaySqlRows rows =
      compat::build_aclgraph_fixture_graph_replay_sql_rows(fixture, ir, 5);

  require(rows.anchors.size() == 2);
  require(rows.events.size() == 3);
  require(rows.event_sources.size() == rows.events.size());
  require(rows.graph_replays.size() == 1);
  require(rows.graph_envelopes.size() == 2);

  require(rows.anchors[0].anchor_id == "anchor-0");
  require(rows.anchors[0].event_id == "aclgraph-subslot-subslot-b");
  require(rows.anchors[0].step_idx == 1);
  require(rows.anchors[1].anchor_id == "anchor-1");
  require(rows.anchors[1].event_id == "aclgraph-subslot-subslot-a");
  require(rows.anchors[1].step_idx == 2);

  require(rows.graph_replays[0].graph_provider == "aclgraph");
  require(rows.graph_replays[0].graph_kind == "aclgraph_replay");
  require(rows.graph_replays[0].graph_event_idx == 1);
  require(rows.graph_replays[0].event_id == "aclgraph-replay-unit-unit-1");
  require(rows.graph_replays[0].graph_exec_id == "unit-1");
  require(rows.graph_replays[0].stream_id == 7);
  require(rows.graph_replays[0].enclosed_event_count == 2);
  require(rows.graph_replays[0].enclosed_event_us == 3.0);
  require(rows.graph_replays[0].enclosed_kernel_count == 3);

  require(rows.events[0].event_id == rows.graph_replays[0].event_id);
  require(rows.events[0].source_table == "ACLGRAPH_REPLAY_UNIT");
  require(rows.events[0].semantic_role == "anchor");

  require(rows.events[1].event_id == "aclgraph-subslot-subslot-b");
  require(rows.events[1].step_idx == 1);
  require(rows.events[1].symbol == "H");
  require(rows.events[2].event_id == "aclgraph-subslot-subslot-a");
  require(rows.events[2].step_idx == 2);
  require(rows.events[2].symbol == "T");

  require(rows.graph_envelopes[0].child_event_id == rows.events[1].event_id);
  require(rows.graph_envelopes[0].stream_relation == "same_stream");
  require(rows.graph_envelopes[1].child_event_id == rows.events[2].event_id);
  require(rows.graph_envelopes[1].stream_relation == "cross_stream");

  AclGraphSemanticFixture bad_fixture = fixture;
  bad_fixture.replay_tilings.clear();
  bool rejected_missing_tiling = false;
  try {
    (void)compat::build_aclgraph_fixture_graph_replay_sql_rows(bad_fixture, ir);
  } catch (const std::invalid_argument&) {
    rejected_missing_tiling = true;
  }
  require(rejected_missing_tiling);

  AclGraphSemanticFixture bad_anchor_fixture = fixture;
  bad_anchor_fixture.hlt_anchor_seeds[0].subslot_id = "missing-subslot";
  bool rejected_missing_anchor_subslot = false;
  try {
    (void)compat::build_aclgraph_fixture_graph_replay_sql_rows(
        bad_anchor_fixture, ir);
  } catch (const std::invalid_argument&) {
    rejected_missing_anchor_subslot = true;
  }
  require(rejected_missing_anchor_subslot);

  return 0;
}
