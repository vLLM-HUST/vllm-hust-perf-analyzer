#include "traceloom/compat/aclgraph_graph_replay_rows.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "traceloom/compat/anchor_cost_breakdown_rows.h"
#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/sidecar_writer.h"

namespace traceloom::compat {
namespace {

double ns_to_us(std::int64_t ns) {
  return static_cast<double>(ns) / 1000.0;
}

std::string graph_event_id_for_unit(const AclGraphReplayUnitFixtureRow& unit) {
  return "aclgraph-replay-unit-" + unit.replay_unit_id;
}

std::string child_event_id_for_subslot(
    const AclGraphReplaySubslotFixtureRow& subslot) {
  return "aclgraph-subslot-" + subslot.subslot_id;
}

std::unordered_map<std::string, const AclGraphReplayActivityFixtureRow*>
activities_by_id(const AclGraphSemanticFixture& fixture) {
  std::unordered_map<std::string, const AclGraphReplayActivityFixtureRow*> out;
  for (const AclGraphReplayActivityFixtureRow& activity :
       fixture.replay_activities) {
    out.emplace(activity.replay_activity_id, &activity);
  }
  return out;
}

std::unordered_map<std::string, const AclGraphReplayTilingFixtureRow*>
tilings_by_unit_id(const AclGraphSemanticFixture& fixture) {
  std::unordered_map<std::string, const AclGraphReplayTilingFixtureRow*> out;
  for (const AclGraphReplayTilingFixtureRow& tiling : fixture.replay_tilings) {
    out.emplace(tiling.replay_unit_id, &tiling);
  }
  return out;
}

std::unordered_map<std::string, std::vector<const AclGraphReplaySubslotFixtureRow*>>
subslots_by_tiling_id(const AclGraphSemanticFixture& fixture) {
  std::unordered_map<std::string,
                     std::vector<const AclGraphReplaySubslotFixtureRow*>>
      out;
  for (const AclGraphReplaySubslotFixtureRow& subslot :
       fixture.replay_subslots) {
    out[subslot.replay_tiling_id].push_back(&subslot);
  }
  for (auto& entry : out) {
    std::sort(entry.second.begin(), entry.second.end(),
              [](const AclGraphReplaySubslotFixtureRow* lhs,
                 const AclGraphReplaySubslotFixtureRow* rhs) {
                return lhs->subslot_idx < rhs->subslot_idx;
              });
  }
  return out;
}

std::int64_t choose_graph_stream_id(
    const AclGraphReplayUnitFixtureRow& unit,
    const std::unordered_map<std::string,
                             const AclGraphReplayActivityFixtureRow*>&
        activity_index,
    const std::vector<const AclGraphReplaySubslotFixtureRow*>& subslots) {
  const auto activity_found = activity_index.find(unit.replay_activity_id);
  if (activity_found != activity_index.end() &&
      !activity_found->second->stream_ids.empty()) {
    return activity_found->second->stream_ids.front();
  }
  if (!subslots.empty()) {
    return subslots.front()->stream_id;
  }
  return -1;
}

std::uint32_t choose_enclosed_kernel_count(
    const std::vector<const AclGraphReplaySubslotFixtureRow*>& subslots) {
  std::uint32_t out = 0;
  for (const AclGraphReplaySubslotFixtureRow* subslot : subslots) {
    out += subslot->raw_child_task_count;
  }
  return out;
}

double sum_child_us(
    const std::vector<const AclGraphReplaySubslotFixtureRow*>& subslots) {
  double out = 0.0;
  for (const AclGraphReplaySubslotFixtureRow* subslot : subslots) {
    out += ns_to_us(subslot->end_ns - subslot->start_ns);
  }
  return out;
}

EventSourceSqlRow source_for_event(const EventSqlRow& event,
                                   std::string source_role) {
  EventSourceSqlRow source;
  source.event_id = event.event_id;
  source.source_ordinal = 0;
  source.db_idx = event.db_idx;
  source.device_id = event.device_id;
  source.source_table = event.source_table;
  source.source_key = event.source_key;
  source.source_role = std::move(source_role);
  return source;
}

struct SyntheticEventRef {
  std::string event_id;
  std::uint32_t step_idx = 0;
};

void attach_anchor_rows_to_subslot_events(
    const AclGraphSemanticFixture& fixture,
    const std::unordered_map<std::string, SyntheticEventRef>&
        subslot_event_refs,
    std::vector<AnchorSqlRow>& anchors) {
  if (fixture.hlt_anchor_seeds.size() != anchors.size()) {
    throw std::invalid_argument(
        "ACLGraph HLT anchor seeds must match generated anchor rows");
  }
  for (std::size_t index = 0; index < fixture.hlt_anchor_seeds.size();
       ++index) {
    const AclGraphHltAnchorSeedFixtureRow& seed =
        fixture.hlt_anchor_seeds[index];
    const auto found = subslot_event_refs.find(seed.subslot_id);
    if (found == subslot_event_refs.end()) {
      throw std::invalid_argument(
          "ACLGraph HLT anchor seed references unknown replay subslot");
    }
    anchors[index].event_id = found->second.event_id;
    anchors[index].step_idx = found->second.step_idx;
  }
}

}  // namespace

GraphReplaySqlRows build_aclgraph_fixture_graph_replay_sql_rows(
    const AclGraphSemanticFixture& fixture,
    const NativeIr& ir,
    std::uint32_t db_idx) {
  const auto activity_index = activities_by_id(fixture);
  const auto tiling_index = tilings_by_unit_id(fixture);
  const auto subslot_index = subslots_by_tiling_id(fixture);

  GraphReplaySqlRows rows;
  rows.anchors = build_anchor_sequence_sql_rows(ir, db_idx);

  std::uint32_t event_step_idx = 0;
  std::uint32_t envelope_idx = 1;
  std::unordered_map<std::string, SyntheticEventRef> subslot_event_refs;
  for (const AclGraphReplayUnitFixtureRow& unit : fixture.replay_units) {
    const auto tiling_found = tiling_index.find(unit.replay_unit_id);
    if (tiling_found == tiling_index.end()) {
      throw std::invalid_argument(
          "ACLGraph replay unit has no replay tiling");
    }
    const AclGraphReplayTilingFixtureRow& tiling = *tiling_found->second;
    const auto subslots_found = subslot_index.find(tiling.replay_tiling_id);
    const std::vector<const AclGraphReplaySubslotFixtureRow*> empty_subslots;
    const std::vector<const AclGraphReplaySubslotFixtureRow*>& subslots =
        subslots_found == subslot_index.end() ? empty_subslots
                                              : subslots_found->second;

    const std::uint32_t graph_step_idx = event_step_idx++;
    const std::int64_t graph_stream_id =
        choose_graph_stream_id(unit, activity_index, subslots);
    const std::string graph_event_id = graph_event_id_for_unit(unit);

    EventSqlRow graph_event;
    graph_event.event_id = graph_event_id;
    graph_event.db_idx = db_idx;
    graph_event.device_id = 0;
    graph_event.step_idx = graph_step_idx;
    graph_event.source_table = "ACLGRAPH_REPLAY_UNIT";
    graph_event.source_key = unit.replay_unit_id;
    graph_event.stream_id = graph_stream_id;
    graph_event.start_ns = unit.start_ns;
    graph_event.end_ns = unit.end_ns;
    graph_event.dur_us = ns_to_us(unit.end_ns - unit.start_ns);
    graph_event.category = "graph";
    graph_event.role = "compute";
    graph_event.semantic_role = "anchor";
    graph_event.symbol = "ACLGraphReplay";
    graph_event.label = "ACLGraphReplay";
    graph_event.task_type = "ACL_GRAPH";
    rows.event_sources.push_back(source_for_event(graph_event,
                                                  "synthetic_graph_replay"));
    rows.events.push_back(std::move(graph_event));

    GraphReplaySqlRow replay;
    replay.graph_event_id = graph_event_id;
    replay.db_idx = db_idx;
    replay.device_id = 0;
    replay.graph_provider = "aclgraph";
    replay.graph_kind = "aclgraph_replay";
    replay.graph_event_idx = unit.unit_idx_global + 1;
    replay.event_id = graph_event_id;
    replay.step_idx = graph_step_idx;
    replay.stream_id = graph_stream_id;
    replay.graph_id = unit.replay_activity_id;
    replay.graph_exec_id = unit.replay_unit_id;
    replay.start_ns = unit.start_ns;
    replay.end_ns = unit.end_ns;
    replay.dur_us = ns_to_us(unit.end_ns - unit.start_ns);
    replay.enclosed_event_count =
        static_cast<std::uint32_t>(subslots.size());
    replay.enclosed_event_us = sum_child_us(subslots);
    replay.enclosed_kernel_count = choose_enclosed_kernel_count(subslots);
    replay.enclosed_kernel_us = replay.enclosed_event_us;
    rows.graph_replays.push_back(replay);

    for (const AclGraphReplaySubslotFixtureRow* subslot : subslots) {
      const std::uint32_t child_step_idx = event_step_idx++;
      const std::string child_event_id = child_event_id_for_subslot(*subslot);
      subslot_event_refs.emplace(subslot->subslot_id,
                                 SyntheticEventRef{child_event_id,
                                                   child_step_idx});

      EventSqlRow child_event;
      child_event.event_id = child_event_id;
      child_event.db_idx = db_idx;
      child_event.device_id = 0;
      child_event.step_idx = child_step_idx;
      child_event.source_table = "ACLGRAPH_REPLAY_SUBSLOT";
      child_event.source_key = subslot->subslot_id;
      child_event.stream_id = subslot->stream_id;
      child_event.start_ns = subslot->start_ns;
      child_event.end_ns = subslot->end_ns;
      child_event.dur_us = ns_to_us(subslot->end_ns - subslot->start_ns);
      child_event.category = "exec";
      child_event.role = "compute";
      child_event.semantic_role = "graph_child";
      child_event.symbol = subslot->slot_symbol;
      child_event.label = subslot->slot_symbol;
      child_event.raw_label = subslot->body_match_signature;
      child_event.task_type = subslot->slot_kind;
      rows.event_sources.push_back(
          source_for_event(child_event, "synthetic_graph_child"));
      rows.events.push_back(std::move(child_event));

      GraphEnvelopeSqlRow envelope;
      envelope.envelope_id =
          "aclgraph-envelope-" + std::to_string(envelope_idx);
      envelope.db_idx = db_idx;
      envelope.device_id = 0;
      envelope.graph_provider = replay.graph_provider;
      envelope.graph_kind = replay.graph_kind;
      envelope.envelope_idx = envelope_idx++;
      envelope.graph_event_id = replay.graph_event_id;
      envelope.child_event_id = child_event_id;
      envelope.graph_step_idx = graph_step_idx;
      envelope.child_step_idx = child_step_idx;
      envelope.relation = "contains";
      envelope.stream_relation =
          graph_stream_id == static_cast<std::int64_t>(subslot->stream_id)
              ? "same_stream"
              : "cross_stream";
      envelope.graph_id = replay.graph_id;
      envelope.graph_exec_id = replay.graph_exec_id;
      envelope.graph_start_ns = replay.start_ns;
      envelope.graph_end_ns = replay.end_ns;
      envelope.child_start_ns = subslot->start_ns;
      envelope.child_end_ns = subslot->end_ns;
      envelope.start_offset_us = ns_to_us(subslot->start_ns - unit.start_ns);
      envelope.end_offset_us = ns_to_us(unit.end_ns - subslot->end_ns);
      envelope.child_dur_us = ns_to_us(subslot->end_ns - subslot->start_ns);
      rows.graph_envelopes.push_back(std::move(envelope));
    }
  }

  attach_anchor_rows_to_subslot_events(fixture, subslot_event_refs,
                                       rows.anchors);
  return rows;
}

std::vector<EventSqlRow> split_graph_replay_timeline_sql_rows(
    const GraphReplaySqlRows& rows) {
  return rows.events;
}

std::vector<EventSourceSqlRow> split_graph_replay_source_lineage_sql_rows(
    const GraphReplaySqlRows& rows) {
  return rows.event_sources;
}

std::vector<AnchorSqlRow> split_graph_replay_anchor_sequence_sql_rows(
    const GraphReplaySqlRows& rows) {
  return rows.anchors;
}

GraphReplayEvidenceSqlRows split_graph_replay_evidence_sql_rows(
    const GraphReplaySqlRows& rows) {
  GraphReplayEvidenceSqlRows out;
  out.graph_replays = rows.graph_replays;
  out.graph_envelopes = rows.graph_envelopes;
  return out;
}

void write_aclgraph_fixture_compatibility_sidecar(
    const std::string& sqlite_path,
    const AclGraphSemanticFixture& fixture,
    const NativeIr& ir,
    const AnchorInternalCostBreakdown& breakdown,
    const NativeCompatibilitySidecarOptions& options) {
  NativeCompatibilitySidecarOptions basic_options = options;
  basic_options.materialize_collective_tags = false;
  write_basic_native_compatibility_sidecar(sqlite_path, ir, basic_options);
  replace_anchor_cost_breakdown_rows(
      sqlite_path, build_anchor_cost_breakdown_sql_rows(breakdown));

  const GraphReplaySqlRows graph_rows =
      build_aclgraph_fixture_graph_replay_sql_rows(fixture, ir, options.db_idx);
  replace_timeline_rows(sqlite_path,
                        split_graph_replay_timeline_sql_rows(graph_rows));
  replace_event_source_rows(
      sqlite_path, split_graph_replay_source_lineage_sql_rows(graph_rows));
  replace_anchor_rows(sqlite_path,
                      split_graph_replay_anchor_sequence_sql_rows(graph_rows));
  replace_graph_replay_evidence_rows(
      sqlite_path, split_graph_replay_evidence_sql_rows(graph_rows));
  if (options.materialize_collective_tags) {
    replace_collective_global_link_rows(sqlite_path, {});
  }
}

}  // namespace traceloom::compat
