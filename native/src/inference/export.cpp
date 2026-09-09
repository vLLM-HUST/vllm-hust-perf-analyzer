#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#include "internal.h"
#include "traceloom/inference/trace.h"
#if !defined(_WIN32)
#include <unistd.h>
#else
#include <chrono>
#endif

namespace traceloom::inference::detail {
void atomic_write(const std::string& path, const std::string& contents) {
  namespace fs = std::filesystem;
  const auto parent = fs::path(path).parent_path();
  if (!parent.empty()) fs::create_directories(parent);
  std::string temp;
#if !defined(_WIN32)
  std::string pattern = path + ".tmp.XXXXXX";
  std::vector<char> writable(pattern.begin(), pattern.end());
  writable.push_back(0);
  int fd = mkstemp(writable.data());
  require(fd >= 0, "cannot create inference projection");
  close(fd);
  temp = writable.data();
#else
  temp = path + ".tmp." +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count());
#endif
  try {
    std::ofstream out(temp, std::ios::binary);
    require(bool(out), "cannot write inference projection");
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.close();
    require(bool(out), "inference projection write failed");
    fs::permissions(temp, fs::perms::owner_read | fs::perms::owner_write);
    fs::rename(temp, path);
  } catch (...) {
    std::error_code ignored;
    fs::remove(temp, ignored);
    throw;
  }
}
namespace {
std::string escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&#39;";
        break;
      default:
        out += c;
    }
  }
  return out;
}
using Domain = std::tuple<std::string, std::string, std::string>;
struct Group {
  std::int64_t start = std::numeric_limits<std::int64_t>::max(), end = 0;
  std::vector<const Span*> spans;
  std::vector<const Observation*> observations;
};
std::map<Domain, Group> groups(const Snapshot& snapshot) {
  std::map<Domain, Group> out;
  for (const auto& s : snapshot.spans) {
    auto& g = out[{s.trace, s.producer, s.clock}];
    const auto start = s.has_start ? s.start : s.end;
    g.start = std::min(g.start, start);
    g.end = std::max(g.end, s.has_end ? s.end : start);
    g.spans.push_back(&s);
  }
  for (const auto& e : snapshot.observations) {
    auto& g = out[{e.trace, e.producer, e.clock}];
    g.start = std::min(g.start, e.monotonic);
    g.end = std::max(g.end, e.monotonic);
    g.observations.push_back(&e);
  }
  return out;
}
std::string ms(std::int64_t ns) {
  std::ostringstream o;
  o << std::fixed << std::setprecision(3) << double(ns) / 1e6;
  return o.str();
}
std::string args(const Span& s) {
  std::string deps = "[";
  for (const auto& d : s.dependencies) {
    if (deps.size() > 1) deps += ',';
    deps += quote(d);
  }
  deps += ']';
  return "{\"trace_id\":" + quote(s.trace) + ",\"span_id\":" + quote(s.id) +
         ",\"parent_span_id\":" + quote(s.parent) +
         ",\"status\":" + quote(s.status) +
         ",\"terminal_status\":" + quote(s.terminal_status) +
         ",\"clock_id\":" + quote(s.clock) +
         ",\"wall_time_ns\":" + quote(std::to_string(s.wall)) +
         ",\"start_monotonic_ns\":" + quote(std::to_string(s.start)) +
         ",\"decision_summary\":" + quote(s.summary) +
         ",\"evidence_refs\":" + quote(s.evidence) +
         ",\"dependencies\":" + deps + ",\"attributes\":" + s.attributes +
         ",\"path_evidence\":\"partial_observed_dependencies\"}";
}
}  // namespace
void write_html(const Snapshot& snapshot, const std::string& output,
                bool live) {
  std::ostringstream o;
  o << R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>TraceLoom · Inference</title><link rel="icon" href="data:,">)HTML";
  if (live) o << "<meta http-equiv=\"refresh\" content=\"2\">";
  o << R"HTML(<style>
body{margin:0;background:#111827;color:#e5e7eb;font:15px system-ui,sans-serif}main{max-width:1280px;margin:auto;padding:30px}h1{font-size:28px}h2{font-size:18px;margin-top:28px}p{color:#aebbd0}input{background:#243247;border:1px solid #64748b;color:white;padding:10px;width: min(90%,480px);border-radius:6px}.span{border-bottom:1px solid #334155;padding:12px 0}.top{display:flex;gap:12px;align-items:center;flex-wrap:wrap}.name{font-weight:600}.badge{font-size:12px;padding:3px 7px;border-radius:4px;background:#334155}.error,.cancelled{color:#fda4af}.open,.missing_start{color:#fcd34d}.lane{height:14px;background:#1e293b;margin:8px 0;border-radius:4px}.bar{height:100%;min-width:3px;background:#38bdf8;border-radius:4px}.error .bar{background:#fb7185}.cancelled .bar{background:#fbbf24}.open .bar{background:#a78bfa}summary{cursor:pointer;color:#aebbd0}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}code{font-size:12px}small{color:#94a3b8}.trace{padding:12px;background:#1e293b;margin:8px 0;border-radius:6px}
</style><main><h1>TraceLoom · Observable inference</h1>)HTML";
  o << "<p>"
    << (live ? "Live snapshots · refresh every 2 seconds" : "Saved snapshot")
    << " · " << snapshot.spans.size() << " observed spans</p>";
  o << "<p>Clock groups have independent origins. Bars show measured spans; "
       "open spans are points. "
       "Parentage is scope, not a dependency. Longest observed dependency "
       "paths are partial evidence, not a global critical path.</p>";
  o << "<input id=\"filter\" aria-label=\"Filter spans\" placeholder=\"Filter "
       "by name, status, span ID or metadata\">";
  for (const auto& [trace, state] : snapshot.trace_states) {
    const auto it = snapshot.dropped.find(trace);
    o << "<div class=\"trace\"><code>" << escape(trace) << "</code> · "
      << escape(state) << " · reported producer cumulative drops: "
      << (it == snapshot.dropped.end() ? 0 : it->second) << "</div>";
  }
  o << "<p>Unresolved dependency links: " << snapshot.unresolved_links
    << " · cross-clock links with unknown timing: "
    << snapshot.cross_clock_links << "</p>";
  for (const auto& [domain, g] : groups(snapshot)) {
    auto [trace, producer, clock] = domain;
    std::int64_t longest = 0;
    std::vector<std::pair<std::int64_t, int>> endpoints;
    for (const auto* s : g.spans) {
      if (s->path_observed) longest = std::max(longest, s->path_ns);
      if (s->has_start && s->has_end && s->end > s->start) {
        endpoints.emplace_back(s->start, 1);
        endpoints.emplace_back(s->end, -1);
      }
    }
    std::sort(endpoints.begin(), endpoints.end());
    int active = 0, peak = 0;
    for (const auto& e : endpoints) {
      active += e.second;
      peak = std::max(peak, active);
    }
    o << "<section><h2>" << escape(producer) << " / " << escape(clock)
      << "</h2><small>Trace " << escape(trace)
      << " · peak overlapping spans: " << peak
      << " (includes nested scopes) · longest observed dependency path: "
      << (longest ? ms(longest) + " ms" : "unavailable") << "</small>";
    for (const auto* p : g.spans) {
      const auto& s = *p;
      const auto start = s.has_start ? s.start : s.end;
      const double range = std::max<std::int64_t>(1, g.end - g.start);
      const double left = 100.0 * double(start - g.start) / range;
      const double width = s.has_start && s.has_end
                               ? 100.0 * double(s.end - s.start) / range
                               : 0;
      o << "<article class=\"span " << escape(s.status) << "\" data-trace-id=\""
        << s.trace << "\" data-span-id=\"" << s.id
        << "\"><div class=\"top\" style=\"padding-left:"
        << std::min<std::size_t>(s.depth, 12) * 14
        << "px\"><span class=\"name\">" << escape(s.name)
        << "</span><span class=\"badge\">" << escape(s.kind) << "</span><span>"
        << escape(s.status) << "</span><span>"
        << (s.has_start && s.has_end ? ms(s.end - s.start) + " ms"
                                     : "duration unknown")
        << "</span><code>" << s.id
        << "</code></div><div class=\"lane\"><div class=\"bar\" "
           "style=\"margin-left:"
        << left << "%;width:" << width << "%\"></div></div>";
      if (!s.summary.empty()) o << "<p>" << escape(s.summary) << "</p>";
      o << "<details><summary>Metadata and evidence</summary><pre>Parent: "
        << escape(s.parent) << (s.missing_parent ? " (not observed)" : "")
        << "\nDependencies:";
      for (const auto& d : s.dependencies) o << " " << d;
      o << "\nTerminal status: " << escape(s.terminal_status)
        << "\nEvidence refs: " << escape(s.evidence)
        << "\nAttributes: " << escape(s.attributes)
        << "\nWall start ns (display only): " << s.wall
        << "\nSpan observations:\n"
        << escape(s.observations) << "</pre></details></article>";
    }
    o << "</section>";
  }
  o << R"HTML(<script>(()=>{const key=location.pathname+':traceloom',filter=document.getElementById('filter'),spans=[...document.querySelectorAll('.span')];let state={filter:'',open:[]};try{state=JSON.parse(sessionStorage.getItem(key))||state;}catch{}const save=()=>{try{sessionStorage.setItem(key,JSON.stringify({filter:filter.value,open:spans.filter(s=>s.querySelector('details').open).map(s=>s.dataset.traceId+':'+s.dataset.spanId)}));}catch{}};const apply=()=>{const q=filter.value.toLowerCase();spans.forEach(s=>s.hidden=!s.textContent.toLowerCase().includes(q));};filter.value=state.filter||'';spans.forEach(s=>{const d=s.querySelector('details');d.open=(state.open||[]).includes(s.dataset.traceId+':'+s.dataset.spanId);d.addEventListener('toggle',save);});apply();filter.addEventListener('input',()=>{apply();save();});})();</script></main></html>)HTML";
  atomic_write(output, o.str());
}
void write_perfetto(const Snapshot& snapshot, const std::string& output) {
  std::ostringstream o;
  o << std::setprecision(17) << "{\"traceEvents\":[";
  bool first = true;
  auto emit = [&](const std::string& e) {
    if (!first) o << ',';
    first = false;
    o << e;
  };
  int pid = 0;
  for (const auto& [domain, g] : groups(snapshot)) {
    ++pid;
    auto [trace, producer, clock] = domain;
    emit("{\"ph\":\"M\",\"name\":\"process_name\",\"pid\":" +
         std::to_string(pid) + ",\"args\":{\"name\":" +
         quote("Inference " + trace + " / " + producer + " / " + clock +
               " (independent origin)") +
         "}}");
    int tid = 0;
    std::map<std::string, int> tids;
    for (const auto* p : g.spans) {
      const auto& s = *p;
      ++tid;
      tids[s.id] = tid;
      const auto start = s.has_start ? s.start : s.end;
      emit("{\"ph\":\"M\",\"name\":\"thread_name\",\"pid\":" +
           std::to_string(pid) + ",\"tid\":" + std::to_string(tid) +
           ",\"args\":{\"name\":" + quote(s.name + " " + s.id) + "}}");
      std::ostringstream event;
      event << std::setprecision(17) << "{\"name\":" << quote(s.name)
            << ",\"cat\":" << quote("inference." + s.kind) << ",\"pid\":" << pid
            << ",\"tid\":" << tid
            << ",\"ts\":" << double(start - g.start) / 1000.0;
      if (s.has_start && s.has_end)
        event << ",\"ph\":\"X\",\"dur\":" << double(s.end - s.start) / 1000.0;
      else
        event << ",\"ph\":\"i\",\"s\":\"t\"";
      event << ",\"args\":" << args(s) << '}';
      emit(event.str());
    }
    std::map<std::string, const Span*> by_id;
    for (const auto* span : g.spans) by_id[span->id] = span;
    for (const auto* target : g.spans) {
      if (!target->has_start || !target->has_end) continue;
      for (const auto& id : target->dependencies) {
        auto it = by_id.find(id);
        if (it == by_id.end() || !it->second->has_start || !it->second->has_end)
          continue;
        const auto* source = it->second;
        const auto flow_id =
            std::to_string(pid) + ":" + source->id + ":" + target->id;
        auto flow = [&](const char* phase, int thread, std::int64_t timestamp) {
          std::ostringstream event;
          event << std::setprecision(17)
                << "{\"name\":\"observed "
                   "dependency\",\"cat\":\"inference.dependency\",\"ph\":"
                << quote(phase) << ",\"id\":" << quote(flow_id)
                << ",\"pid\":" << pid << ",\"tid\":" << thread
                << ",\"ts\":" << double(timestamp - g.start) / 1000.0;
          if (std::string(phase) == "f") event << ",\"bp\":\"e\"";
          event << '}';
          emit(event.str());
        };
        flow("s", tids[source->id], source->end);
        flow("f", tids[target->id], target->start);
      }
    }
    for (const auto* e : g.observations) {
      std::ostringstream point;
      point << std::setprecision(17) << "{\"name\":" << quote(e->name)
            << ",\"cat\":\"inference.observation\",\"ph\":\"i\",\"s\":\"t\","
               "\"pid\":"
            << pid << ",\"tid\":" << tids[e->span]
            << ",\"ts\":" << double(e->monotonic - g.start) / 1000.0
            << ",\"args\":" << e->payload << '}';
      emit(point.str());
    }
    const auto dropped = snapshot.dropped.find(trace);
    if (dropped != snapshot.dropped.end())
      emit(
          "{\"name\":\"reported_producer_drop_receipts\",\"ph\":\"C\","
          "\"pid\":" +
          std::to_string(pid) + ",\"tid\":0,\"ts\":0,\"args\":{\"count\":" +
          std::to_string(dropped->second) + "}}");
  }
  o << "],\"displayTimeUnit\":\"ms\",\"metadata\":{\"clock_alignment\":"
       "\"independent_origins\",\"path_evidence\":\"partial_observed_"
       "dependencies\"}}";
  atomic_write(output, o.str());
}
}  // namespace traceloom::inference::detail

namespace traceloom::inference {
void export_trace(const std::string& database, const std::string& html,
                  const std::string& perfetto, bool live) {
  namespace fs = std::filesystem;
  for (const auto& p : {html, perfetto})
    if (!p.empty())
      detail::require(
          fs::weakly_canonical(p) != fs::weakly_canonical(database) &&
              (!fs::exists(p) || !fs::equivalent(p, database)),
          "projection/database paths collide");
  if (!html.empty() && !perfetto.empty())
    detail::require(
        fs::weakly_canonical(html) != fs::weakly_canonical(perfetto) &&
            (!fs::exists(html) || !fs::exists(perfetto) ||
             !fs::equivalent(html, perfetto)),
        "projection paths collide");
  auto db = detail::open(database, true);
  detail::exec(db.get(), "BEGIN");
  auto snapshot = detail::load(db.get());
  detail::exec(db.get(), "COMMIT");
  if (!html.empty()) detail::write_html(snapshot, html, live);
  if (!perfetto.empty()) detail::write_perfetto(snapshot, perfetto);
}
}  // namespace traceloom::inference
