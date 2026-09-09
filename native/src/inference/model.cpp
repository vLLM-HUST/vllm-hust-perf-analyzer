#include <algorithm>
#include <limits>
#include <queue>
#include <set>

#include "internal.h"

namespace traceloom::inference::detail {
Snapshot load(sqlite3* db) {
  check_version(db);
  Snapshot out;
  auto q =
      prepare(db, R"SQL(SELECT trace_id,span_id,parent_span_id,name,kind,status,
    producer_id,clock_id,start_ns,end_ns,wall_time_ns,has_start,has_end,duration_ns,
    attributes,decision_summary,links,terminal_status FROM traceloom_v_inference_span
    ORDER BY trace_id,start_ns,span_id)SQL");
  int rc;
  while ((rc = sqlite3_step(q.get())) == SQLITE_ROW) {
    Span s;
    s.trace = text(q.get(), 0);
    s.id = text(q.get(), 1);
    s.parent = text(q.get(), 2);
    s.name = text(q.get(), 3);
    s.kind = text(q.get(), 4);
    s.status = text(q.get(), 5);
    s.producer = text(q.get(), 6);
    s.clock = text(q.get(), 7);
    s.start = sqlite3_column_int64(q.get(), 8);
    s.end = sqlite3_column_int64(q.get(), 9);
    s.wall = sqlite3_column_int64(q.get(), 10);
    s.has_start = sqlite3_column_int(q.get(), 11);
    s.has_end = sqlite3_column_int(q.get(), 12);
    if (s.has_start && s.has_end)
      require(sqlite3_column_type(q.get(), 13) == SQLITE_INTEGER &&
                  s.end >= s.start,
              "span has mismatched clocks or negative duration");
    s.attributes = text(q.get(), 14);
    s.summary = text(q.get(), 15);
    s.terminal_status = text(q.get(), 17);
    auto links = prepare(db, "SELECT value FROM json_each(?)");
    bind_text(links.get(), 1, text(q.get(), 16));
    while (next_row(links.get()))
      s.dependencies.push_back(text(links.get(), 0));
    out.spans.push_back(std::move(s));
  }
  require(rc == SQLITE_DONE, "cannot read inference spans");
  std::map<std::pair<std::string, std::string>, std::size_t> index;
  for (std::size_t i = 0; i < out.spans.size(); ++i)
    index[{out.spans[i].trace, out.spans[i].id}] = i;
  q = prepare(db, R"SQL(SELECT e.trace_id,e.span_id,j.value
    FROM traceloom_inference_event e,json_each(e.payload,'$.evidence_refs') j
    GROUP BY e.trace_id,e.span_id,j.value ORDER BY e.trace_id,e.span_id,j.value)SQL");
  while (next_row(q.get())) {
    auto it = index.find({text(q.get(), 0), text(q.get(), 1)});
    if (it != index.end()) {
      auto& refs = out.spans[it->second].evidence;
      if (!refs.empty()) refs += ", ";
      refs += text(q.get(), 2);
    }
  }
  q = prepare(db, R"SQL(SELECT trace_id,span_id,payload,producer_id,
    json_extract(payload,'$.clock_id'),coalesce(json_extract(payload,'$.name'),'observation'),
    json_extract(payload,'$.monotonic_ns') FROM traceloom_inference_event
    WHERE event_type='span_event' ORDER BY producer_id,sequence)SQL");
  while (next_row(q.get())) {
    auto it = index.find({text(q.get(), 0), text(q.get(), 1)});
    if (it != index.end())
      out.spans[it->second].observations += text(q.get(), 2) + "\n";
    out.observations.push_back(
        {text(q.get(), 0), text(q.get(), 1), text(q.get(), 3), text(q.get(), 4),
         text(q.get(), 5), text(q.get(), 2), sqlite3_column_int64(q.get(), 6)});
  }
  q = prepare(
      db,
      "SELECT trace_id,state,terminal_status FROM traceloom_v_inference_trace");
  while (next_row(q.get()))
    out.trace_states[text(q.get(), 0)] =
        text(q.get(), 1) +
        (text(q.get(), 2).empty() ? "" : " / " + text(q.get(), 2));
  q = prepare(db, R"SQL(SELECT trace_id,sum(dropped) FROM (
    SELECT trace_id,producer_id,max(json_extract(payload,'$.attributes.dropped_events')) AS dropped
    FROM traceloom_inference_event GROUP BY trace_id,producer_id) GROUP BY trace_id)SQL");
  while (next_row(q.get()))
    out.dropped[text(q.get(), 0)] = sqlite3_column_int64(q.get(), 1);
  validate_and_measure(out);
  return out;
}

void validate_and_measure(Snapshot& out) {
  const auto n = out.spans.size();
  std::map<std::pair<std::string, std::string>, std::size_t> index;
  for (std::size_t i = 0; i < n; ++i)
    index[{out.spans[i].trace, out.spans[i].id}] = i;
  std::vector<std::vector<std::size_t>> children(n), successors(n);
  std::vector<std::size_t> parent_degree(n), dependency_degree(n);
  for (std::size_t i = 0; i < n; ++i) {
    auto& s = out.spans[i];
    if (!s.parent.empty()) {
      auto p = index.find({s.trace, s.parent});
      if (p == index.end() || !out.spans[p->second].has_start)
        s.missing_parent = true;
      if (p != index.end()) {
        children[p->second].push_back(i);
        ++parent_degree[i];
      }
    }
    for (const auto& id : s.dependencies) {
      require(id != s.id, "self dependency");
      auto p = index.find({s.trace, id});
      if (p == index.end()) {
        ++out.unresolved_links;
        continue;
      }
      successors[p->second].push_back(i);
      ++dependency_degree[i];
      const auto& prev = out.spans[p->second];
      if (!prev.has_start || !prev.has_end || !s.has_start || !s.has_end)
        ++out.unresolved_links;
      else if (prev.producer != s.producer || prev.clock != s.clock)
        ++out.cross_clock_links;
      else
        require(prev.end <= s.start,
                "dependency contradicts observed completion order");
    }
    if (s.has_start && s.has_end) s.path_ns = s.end - s.start;
  }
  auto traverse = [&](std::vector<std::size_t> degree,
                      const std::vector<std::vector<std::size_t>>& edges,
                      bool paths) {
    std::queue<std::size_t> ready;
    for (std::size_t i = 0; i < n; ++i)
      if (!degree[i]) ready.push(i);
    std::size_t visited = 0;
    while (!ready.empty()) {
      auto i = ready.front();
      ready.pop();
      ++visited;
      for (auto j : edges[i]) {
        auto& child = out.spans[j];
        const auto& parent = out.spans[i];
        if (!paths)
          child.depth = std::max(child.depth, parent.depth + 1);
        else if (parent.has_start && parent.has_end && child.has_start &&
                 child.has_end && parent.producer == child.producer &&
                 parent.clock == child.clock) {
          const auto duration = child.end - child.start;
          require(parent.path_ns <=
                      std::numeric_limits<std::int64_t>::max() - duration,
                  "dependency duration overflow");
          child.path_ns = std::max(child.path_ns, parent.path_ns + duration);
          child.path_observed = true;
        }
        if (!--degree[j]) ready.push(j);
      }
    }
    require(visited == n, paths ? "dependency cycle" : "parent cycle");
  };
  traverse(parent_degree, children, false);
  traverse(dependency_degree, successors, true);
}
}  // namespace traceloom::inference::detail
