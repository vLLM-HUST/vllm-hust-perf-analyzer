#include "perfetto_export_internal.h"

#include <sqlite3.h>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace traceloom::compat::perfetto_internal {
namespace {

// Context lanes use the profiler clock, never the JSONL monotonic clock.
// Pack overlaps instead of implying that concurrent kernels are nested calls.
class ContextLanes {
 public:
  explicit ContextLanes(RawTraceWriter& writer) : writer_(writer) {}

  void emit(int pid, const std::string& group, const std::string& name,
            std::int64_t start, std::int64_t end, const std::string& category,
            const std::string& args) {
    auto& lanes = lanes_[{pid, group}];
    std::size_t lane = 0;
    while (lane < lanes.size() && lanes[lane].second > start) ++lane;
    if (lane == lanes.size()) {
      const int tid = ++next_tid_;
      lanes.emplace_back(tid, end);
      writer_.thread(pid, tid, group + " · lane " + std::to_string(lane), tid);
    } else {
      lanes[lane].second = end;
    }
    writer_.slice(pid, lanes[lane].first, name, start, end, category, args);
  }

 private:
  RawTraceWriter& writer_;
  int next_tid_ = 0;
  std::map<std::pair<int, std::string>,
           std::vector<std::pair<int, std::int64_t>>> lanes_;
};

void export_rows(sqlite3* db, ContextLanes& lanes, int pid,
                 const std::string& category, const char* sql) {
  sqlite3_stmt* raw = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK)
    throw std::runtime_error("Perfetto context query: " + std::string(sqlite3_errmsg(db)));
  std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(raw, sqlite3_finalize);
  auto text = [&](int column) {
    const auto* value = sqlite3_column_text(raw, column);
    return value ? std::string(reinterpret_cast<const char*>(value)) : std::string();
  };
  int status;
  while ((status = sqlite3_step(raw)) == SQLITE_ROW)
    lanes.emit(pid, text(0), text(1), sqlite3_column_int64(raw, 2),
               sqlite3_column_int64(raw, 3), category, text(4));
  if (status != SQLITE_DONE)
    throw std::runtime_error("Perfetto context read: " + std::string(sqlite3_errmsg(db)));
}
}  // namespace

void export_context_timeline(sqlite3* db, RawTraceWriter& writer) {
  sqlite3_stmt* raw = nullptr;
  sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE name='traceloom_v_context_device_work'",
                     -1, &raw, nullptr);
  std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> check(raw, sqlite3_finalize);
  if (!raw || sqlite3_step(raw) != SQLITE_ROW) return;

  writer.process(130, "Runtime context · host execution scopes", 130);
  writer.process(131, "Runtime context · step device envelopes (not busy time)", 131);
  writer.process(132, "Runtime context · associated device work", 132);
  ContextLanes lanes(writer);
  export_rows(db, lanes, 130, "traceloom.context.host", R"SQL(
SELECT 'host thread '||m.global_tid,'Step '||e.ordinal||' · '||e.phase,
 m.start_ns,m.end_ns,
 json_object('run_id',e.run_id,'step_id',e.step_id,'execution_id',e.execution_id,
             'phase',e.phase,'rank',e.worker_rank,'scheduled_tokens',e.total_scheduled_tokens,
             'boundary','host dispatch scope; not device duration')
FROM traceloom_v_execution_context e JOIN traceloom_context_marker m USING(execution_id)
WHERE e.support_state='supported_marker' ORDER BY m.start_ns,m.end_ns
)SQL");
  export_rows(db, lanes, 131, "traceloom.context.step_envelope", R"SQL(
WITH work AS (
 SELECT DISTINCT run_id,step_id,device_id,device_work_id,start_ns,end_ns,dur_us
 FROM traceloom_v_context_device_work
)
SELECT 'device '||d.device_id,'Step '||s.ordinal||' · '||s.total_scheduled_tokens||' tokens',
 MIN(d.start_ns),MAX(d.end_ns),
 json_object('run_id',s.run_id,'step_id',s.step_id,'ordinal',s.ordinal,
             'device_id',d.device_id,'linked_device_work',COUNT(*),
             'summed_device_work_us',SUM(d.dur_us),
             'duration_semantics','observation sum; graph envelopes and members may overlap',
             'boundary','linked device envelope; not busy time or complete membership')
FROM work d JOIN traceloom_scheduler_step s USING(run_id,step_id)
GROUP BY s.run_id,s.step_id,d.device_id ORDER BY MIN(d.start_ns)
)SQL");
  export_rows(db, lanes, 132, "traceloom.context.device", R"SQL(
SELECT DISTINCT 'device '||d.device_id,d.symbol,d.start_ns,d.end_ns,
 json_object('run_id',d.run_id,'step_id',d.step_id,'ordinal',s.ordinal,
             'device_work_id',d.device_work_id,'event_id',d.event_id,
             'association_basis',d.association_basis)
FROM traceloom_v_context_device_work d JOIN traceloom_scheduler_step s USING(run_id,step_id)
ORDER BY d.start_ns,d.end_ns
)SQL");
}
}  // namespace traceloom::compat::perfetto_internal
