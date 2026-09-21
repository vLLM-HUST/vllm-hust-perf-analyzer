#include "perfetto_export_internal.h"

#include <sqlite3.h>
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace traceloom::compat::perfetto_internal {
namespace {
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
Statement query(sqlite3* db, const char* sql) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    throw std::runtime_error("replay timeline query: " + std::string(sqlite3_errmsg(db)));
  return Statement(stmt, sqlite3_finalize);
}
std::string text(sqlite3_stmt* stmt, int column) {
  const auto* value = sqlite3_column_text(stmt, column);
  return value ? reinterpret_cast<const char*>(value) : "";
}
bool next(sqlite3_stmt* stmt) {
  const int rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) return true;
  if (rc != SQLITE_DONE) throw std::runtime_error("replay timeline read failed");
  return false;
}
std::string merge_args(const std::string& a, const std::string& b) {
  return a.substr(0, a.size() - 1) + "," + b.substr(1);
}
struct Pattern {
  int first, end, depth;
  std::string name, category, args;
  std::string position_id, label;
};
struct Member {
  int ordinal, count, db, device;
  std::int64_t stream, start, end;
  std::string id, name, args, launch_args, event_id;
  std::int64_t anchor_index;
};
}  // namespace

ReplayTimelineProjection load_replay_timeline(sqlite3* db) {
  ReplayTimelineProjection result;
  auto exists = query(db, "SELECT COUNT(*) FROM sqlite_master WHERE name IN ('traceloom_v_replay_body_position_occurrence','traceloom_v_annotated_anchor_timeline')");
  if (!next(exists.get()) || sqlite3_column_int(exists.get(),0)!=2) return result;  // Older/eager-only artifacts remain supported.

  using Domain = std::tuple<std::string, int, int>;
  std::map<Domain, std::vector<Pattern>> patterns;
  std::map<Domain, int> root_depth_shift;
  auto occurrences = query(db, R"SQL(
SELECT o.domain_id,o.position_start,o.position_end_exclusive,d.display_depth,
       'R'||substr(d.domain_id,length('replay-body-domain-')+1)||'/'||d.local_position_id||' · '||d.label,
       json_object('position_id',o.position_id,'template_occurrence_id',o.occurrence_id,
                   'parent_template_occurrence_id',o.parent_occurrence_id,
                   'position_start',o.position_start,'position_end_exclusive',o.position_end_exclusive),
       o.db_idx,o.device_id,d.position_kind,o.position_id,d.label
FROM traceloom_v_replay_body_position_occurrence o
JOIN traceloom_v_replay_body_position_definition d USING(position_id,domain_id,db_idx,device_id)
WHERE d.position_kind='seq'
)SQL");
  while (next(occurrences.get())) {
    auto* s = occurrences.get();
    const Domain domain{text(s,0),sqlite3_column_int(s,6),sqlite3_column_int(s,7)};
    if (sqlite3_column_int(s,3)==0 && text(s,8)=="seq") {
      root_depth_shift[domain]=1;
      continue; // Domain container is replay packaging, not another display level.
    }
    patterns[domain].push_back({sqlite3_column_int(s,1),sqlite3_column_int(s,2),
        sqlite3_column_int(s,3)*2,text(s,4),
        "traceloom.replay.structure",text(s,5),text(s,9),text(s,10)});
  }
  // Match ordinary-node projection: repeat iteration windows, not an extra
  // replay-only aggregate row above the same windows. Geometry uses direct members,
  // not repeat labels, median costs, or an envelope shared by all launches.
  auto bodies = query(db, R"SQL(
SELECT p.domain_id,MIN(COALESCE(c.position_start,m.terminal_position_ordinal)),
 MAX(COALESCE(c.position_end_exclusive,m.terminal_position_ordinal+1)),d.display_depth,
 'R'||substr(d.domain_id,length('replay-body-domain-')+1)||'/'||d.local_position_id||' · '||d.label||' · body '||m.member_order||'/'||d.repeat_count,
 json_object('position_id',p.position_id,'template_occurrence_id',p.occurrence_id,
             'repeat_iteration',m.member_order,'repeat_count',d.repeat_count,
             'position_start',MIN(COALESCE(c.position_start,m.terminal_position_ordinal)),
             'position_end_exclusive',MAX(COALESCE(c.position_end_exclusive,m.terminal_position_ordinal+1))),
 p.db_idx,p.device_id
FROM traceloom_v_replay_body_position_occurrence p
JOIN traceloom_v_replay_body_position_definition d USING(position_id,domain_id,db_idx,device_id)
JOIN traceloom_v_replay_body_position_direct_member m
 ON m.parent_occurrence_id=p.occurrence_id AND m.domain_id=p.domain_id
 AND m.db_idx=p.db_idx AND m.device_id=p.device_id
LEFT JOIN traceloom_v_replay_body_position_occurrence c
 ON c.occurrence_id=m.child_occurrence_id AND c.domain_id=m.domain_id
 AND c.db_idx=m.db_idx AND c.device_id=m.device_id
WHERE d.position_kind='repeat'
 AND (m.member_kind='child_occurrence' OR m.member_kind='terminal_token')
GROUP BY p.domain_id,p.db_idx,p.device_id,p.occurrence_id,m.member_order
)SQL");
  while (next(bodies.get())) {
    auto* s = bodies.get();
    patterns[{text(s,0),sqlite3_column_int(s,6),sqlite3_column_int(s,7)}].push_back({sqlite3_column_int(s,1),sqlite3_column_int(s,2),
        sqlite3_column_int(s,3)*2+1,text(s,4),
        "traceloom.replay.repeat_body",text(s,5),{}, {}});
  }

  using Realization = std::tuple<std::string,std::string,int,int>;
  std::map<Realization,std::vector<Member>> realizations;
  auto members = query(db, R"SQL(
SELECT p.domain_id,m.launch_id,p.db_idx,p.device_id,p.position_ordinal,d.position_count,
       m.stream_id,m.start_ns,m.end_ns,m.member_id,m.identity,
 json_object('member_id',m.member_id,'event_id',m.event_id,
             'lane_ordinal',m.lane_ordinal,'task_ordinal',m.task_ordinal,
             'position_ordinal',p.position_ordinal,'aggregate_id',p.aggregate_id),
 json_object('launch_id',m.launch_id,'domain_id',p.domain_id,'db_idx',p.db_idx,
             'device_id',p.device_id,'stream_id',m.stream_id,'anchor_id',g.anchor_id,
             'replay_unit_id',g.replay_unit_id,
             'geometry','exact_launch_members; envelopes_include_gaps; non_additive'),
 m.event_id,anchor.anchor_idx
FROM traceloom_replay_body_position p
JOIN traceloom_replay_body_pattern_domain d USING(domain_id,db_idx,device_id)
CROSS JOIN traceloom_replay_cost_aggregate_member a
 ON a.aggregate_id=p.aggregate_id AND a.db_idx=p.db_idx AND a.device_id=p.device_id
CROSS JOIN traceloom_replay_cost_member m
 ON m.member_id=a.member_id AND m.db_idx=a.db_idx AND m.device_id=a.device_id
JOIN traceloom_graph_body_member b
 ON b.launch_id=m.launch_id AND b.member_id=m.member_id AND b.db_idx=m.db_idx AND b.device_id=m.device_id
JOIN traceloom_graph_launch g
 ON g.launch_id=b.launch_id AND g.db_idx=b.db_idx AND g.device_id=b.device_id
JOIN traceloom_anchor anchor ON anchor.anchor_id=g.anchor_id AND anchor.db_idx=g.db_idx AND anchor.device_id=g.device_id
WHERE d.support_status='supported'
ORDER BY p.domain_id,m.launch_id,p.db_idx,p.device_id,p.position_ordinal
)SQL");
  while (next(members.get())) {
    auto* s=members.get();
    const int db_index=sqlite3_column_int(s,2), device=sqlite3_column_int(s,3);
    realizations[{text(s,0),text(s,1),db_index,device}].push_back({
      sqlite3_column_int(s,4),sqlite3_column_int(s,5),db_index,device,
      sqlite3_column_int64(s,6),sqlite3_column_int64(s,7),sqlite3_column_int64(s,8),
      text(s,9),text(s,10),text(s,11),text(s,12),text(s,13),sqlite3_column_int64(s,14)});
  }
  std::map<AnchorCoordinate, std::size_t> expected, realized;
  auto counts = query(db, R"SQL(
SELECT a.db_idx,a.device_id,a.anchor_idx,COUNT(*)
FROM traceloom_graph_launch g
JOIN traceloom_v_annotated_anchor_timeline a ON a.anchor_id=g.anchor_id AND a.db_idx=g.db_idx AND a.device_id=g.device_id
JOIN traceloom_graph_body_member m ON m.launch_id=g.launch_id AND m.db_idx=g.db_idx AND m.device_id=g.device_id
WHERE a.replay_annotation_support_state='supported' AND a.position_support_state='exact_position'
GROUP BY a.db_idx,a.device_id,a.anchor_idx
)SQL");
  while (next(counts.get())) expected[{sqlite3_column_int(counts.get(),0),sqlite3_column_int(counts.get(),1),
      sqlite3_column_int64(counts.get(),2)}]=sqlite3_column_int64(counts.get(),3);
  std::vector<TimelineSlice> slices;
  for (const auto& [key, rows] : realizations) {
    if (rows.empty()) continue;
    const std::string& domain=std::get<0>(key);
    bool complete=rows.front().count>0 && rows.size()==static_cast<std::size_t>(rows.front().count);
    std::set<std::string> ids;
    for (std::size_t i=0;i<rows.size();++i)
      complete = complete && rows[i].ordinal==static_cast<int>(i) &&
          rows[i].stream==rows.front().stream && rows[i].end>=rows[i].start && ids.insert(rows[i].id).second;
    if (!complete) continue; // Never fill a missing position with another launch.
    for (const auto& p : patterns[{domain,std::get<2>(key),std::get<3>(key)}]) {
      if (p.first<0 || p.end<=p.first || p.end>static_cast<int>(rows.size()))
        throw std::runtime_error("replay timeline pattern has invalid member coordinates");
      auto start=rows[p.first].start, end=rows[p.first].end;
      for (int i=p.first+1;i<p.end;++i) {
        start=std::min(start,rows[i].start); end=std::max(end,rows[i].end);
      }
      TimelineSlice slice;
      slice.db=rows.front().db; slice.device=rows.front().device;
      slice.depth=std::max(0,p.depth/2-root_depth_shift[{domain,slice.db,slice.device}]);
      slice.stream=rows.front().stream; slice.start=start; slice.end=end;
      slice.anchor_index=rows.front().anchor_index; slice.name=p.name;
      slice.repeat_body=p.category=="traceloom.replay.repeat_body";
      slice.category=slice.repeat_body ? "traceloom.repeat_body_window" : "traceloom.structural_interval";
      slice.args=merge_args(rows.front().launch_args,p.args); slice.replay=true;
      slice.launch_id=std::get<1>(key); slice.domain_id=domain;
      slice.position_id=p.position_id; slice.phase_label=p.label;
      slice.position_start=p.first; slice.position_end=p.end;
      slices.push_back(std::move(slice));
    }
    for (const auto& m:rows) {
      TimelineSlice slice;
      slice.db=m.db; slice.device=m.device; slice.stream=m.stream;
      slice.start=m.start; slice.end=m.end; slice.anchor_index=m.anchor_index;
      slice.name=m.name; slice.category="traceloom.timeline_event";
      slice.args=merge_args(m.launch_args,m.args); slice.event_id=m.event_id;
      slice.event=true; slice.replay=true;
      slice.launch_id=std::get<1>(key); slice.domain_id=domain;
      slices.push_back(std::move(slice));
      ++realized[{m.db,m.device,m.anchor_index}];
    }
  }
  for (const auto& [anchor,count] : realized)
    if (expected.count(anchor) && expected.at(anchor)==count)
      result.expanded_anchors.insert(anchor);
  for (auto& slice:slices)
    if (result.expanded_anchors.count({slice.db,slice.device,slice.anchor_index}))
      result.slices.push_back(std::move(slice));
  return result;
}
}  // namespace traceloom::compat::perfetto_internal
