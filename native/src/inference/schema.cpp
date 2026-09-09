#include "internal.h"

namespace traceloom::inference::detail {
Db open(const std::string& path, bool readonly) {
  sqlite3* raw = nullptr;
  const int rc =
      sqlite3_open_v2(path.c_str(), &raw,
                      readonly ? SQLITE_OPEN_READONLY
                               : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                      nullptr);
  Db db(raw);
  require(rc == SQLITE_OK, "cannot open inference database");
  sqlite3_busy_timeout(raw, 1000);
  return db;
}
Stmt prepare(sqlite3* db, const std::string& sql) {
  sqlite3_stmt* raw = nullptr;
  const int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr);
  Stmt stmt(raw);
  require(rc == SQLITE_OK,
          "inference SQL prepare failed (SQLite JSON1 required)");
  return stmt;
}
void exec(sqlite3* db, const std::string& sql) {
  require(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK,
          "inference database transaction/schema failed");
}
void bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
  require(sqlite3_bind_text(stmt, index, value.data(),
                            static_cast<int>(value.size()),
                            SQLITE_TRANSIENT) == SQLITE_OK,
          "inference bind failed");
}
std::string text(sqlite3_stmt* stmt, int index) {
  const auto* p = sqlite3_column_text(stmt, index);
  return p ? std::string(reinterpret_cast<const char*>(p),
                         sqlite3_column_bytes(stmt, index))
           : "";
}
std::string quote(const std::string& s) {
  const char* hex = "0123456789abcdef";
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c < 32 || c == '<' || c == '>' || c == '&') {
      out += "\\u00";
      out += hex[c >> 4];
      out += hex[c & 15];
    } else
      out += c;
  }
  return out + '"';
}
void check_version(sqlite3* db) {
  auto q = prepare(db, "SELECT version FROM traceloom_inference_meta");
  require(sqlite3_step(q.get()) == SQLITE_ROW &&
              sqlite3_column_int(q.get(), 0) == 1,
          "unsupported inference SQL module version");
  require(sqlite3_step(q.get()) == SQLITE_DONE,
          "invalid inference SQL metadata");
}
void initialize(sqlite3* db, bool summaries) {
  exec(db, R"SQL(
CREATE TABLE IF NOT EXISTS traceloom_inference_meta(
 version INTEGER NOT NULL, include_summaries INTEGER NOT NULL);
)SQL");
  auto q = prepare(db, "SELECT count(*) FROM traceloom_inference_meta");
  require(sqlite3_step(q.get()) == SQLITE_ROW,
          "cannot read inference metadata");
  if (sqlite3_column_int(q.get(), 0) == 0)
    exec(db, "INSERT INTO traceloom_inference_meta VALUES(1," +
                 std::to_string(summaries ? 1 : 0) + ")");
  q.reset();
  check_version(db);
  q = prepare(db, "SELECT include_summaries FROM traceloom_inference_meta");
  require(sqlite3_step(q.get()) == SQLITE_ROW &&
              sqlite3_column_int(q.get(), 0) == (summaries ? 1 : 0),
          "summary policy differs; use a separate database");
  q.reset();
  exec(db, R"SQL(
CREATE TABLE IF NOT EXISTS traceloom_inference_event(
 trace_id TEXT NOT NULL, event_id TEXT NOT NULL, span_id TEXT NOT NULL,
 producer_id TEXT NOT NULL, sequence INTEGER NOT NULL,
 event_type TEXT NOT NULL, payload TEXT NOT NULL,
 source_line INTEGER NOT NULL, record_sha256 TEXT NOT NULL,
 PRIMARY KEY(trace_id,event_id), UNIQUE(trace_id,producer_id,sequence));
CREATE UNIQUE INDEX IF NOT EXISTS traceloom_inference_lifecycle
 ON traceloom_inference_event(trace_id,span_id,event_type)
 WHERE event_type IN ('span_start','span_end');
CREATE UNIQUE INDEX IF NOT EXISTS traceloom_inference_completion
 ON traceloom_inference_event(trace_id) WHERE event_type='trace_end';
CREATE INDEX IF NOT EXISTS traceloom_inference_span_events
 ON traceloom_inference_event(trace_id,span_id,event_type);
CREATE VIEW IF NOT EXISTS traceloom_v_inference_span AS
WITH ids AS (SELECT DISTINCT trace_id,span_id FROM traceloom_inference_event
 WHERE event_type NOT IN ('metrics','trace_end'))
SELECT i.trace_id,i.span_id,
 json_extract(s.payload,'$.parent_span_id') AS parent_span_id,
 coalesce(json_extract(s.payload,'$.name'),'unobserved') AS name,
 coalesce(json_extract(s.payload,'$.kind'),'step') AS kind,
 CASE WHEN s.event_id IS NULL THEN 'missing_start'
      WHEN e.event_id IS NULL THEN 'open'
      ELSE json_extract(e.payload,'$.status') END AS status,
 s.event_id IS NOT NULL AS has_start, e.event_id IS NOT NULL AS has_end,
 coalesce(s.producer_id,e.producer_id,f.producer_id) AS producer_id,
 coalesce(json_extract(s.payload,'$.clock_id'),json_extract(e.payload,'$.clock_id'),json_extract(f.payload,'$.clock_id')) AS clock_id,
 coalesce(json_extract(s.payload,'$.monotonic_ns'),json_extract(f.payload,'$.monotonic_ns')) AS start_ns,
 coalesce(json_extract(e.payload,'$.monotonic_ns'),json_extract(f.payload,'$.monotonic_ns')) AS end_ns,
 coalesce(json_extract(s.payload,'$.wall_time_ns'),json_extract(f.payload,'$.wall_time_ns')) AS wall_time_ns,
 CASE WHEN json_extract(s.payload,'$.clock_id')=json_extract(e.payload,'$.clock_id')
       AND s.producer_id=e.producer_id
 THEN json_extract(e.payload,'$.monotonic_ns')-json_extract(s.payload,'$.monotonic_ns') END AS duration_ns,
 json_patch(coalesce(json_extract(s.payload,'$.attributes'),'{}'),
            coalesce(json_extract(e.payload,'$.attributes'),'{}')) AS attributes,
 coalesce(json_extract(e.payload,'$.decision_summary'),json_extract(s.payload,'$.decision_summary'),'') AS decision_summary,
 coalesce(json_extract(s.payload,'$.links'),'[]') AS links,
 json_extract(e.payload,'$.status') AS terminal_status
FROM ids i
LEFT JOIN traceloom_inference_event s ON s.trace_id=i.trace_id AND s.span_id=i.span_id AND s.event_type='span_start'
LEFT JOIN traceloom_inference_event e ON e.trace_id=i.trace_id AND e.span_id=i.span_id AND e.event_type='span_end'
LEFT JOIN traceloom_inference_event f ON f.trace_id=i.trace_id AND f.event_id=(SELECT event_id FROM traceloom_inference_event WHERE trace_id=i.trace_id AND span_id=i.span_id ORDER BY sequence LIMIT 1);
CREATE TABLE IF NOT EXISTS traceloom_inference_span_metric(
 trace_id TEXT NOT NULL,span_id TEXT NOT NULL,depth INTEGER NOT NULL,
 missing_parent INTEGER NOT NULL,observed_path_ns INTEGER,
 evidence_state TEXT NOT NULL DEFAULT 'partial_observed_dependencies',
 PRIMARY KEY(trace_id,span_id));
CREATE VIEW IF NOT EXISTS traceloom_v_inference_dependency AS
SELECT e.trace_id,e.span_id AS target_span_id,j.value AS source_span_id,
 CASE WHEN p.span_id IS NULL OR NOT p.has_start OR NOT p.has_end OR NOT s.has_end
 THEN 'incomplete' WHEN s.producer_id!=p.producer_id OR s.clock_id!=p.clock_id
 THEN 'cross_clock' ELSE 'observed_same_clock' END AS timing_state
FROM traceloom_inference_event e,json_each(e.payload,'$.links') j
JOIN traceloom_v_inference_span s ON s.trace_id=e.trace_id AND s.span_id=e.span_id
LEFT JOIN traceloom_v_inference_span p ON p.trace_id=e.trace_id AND p.span_id=j.value
WHERE e.event_type='span_start';
CREATE VIEW IF NOT EXISTS traceloom_v_inference_trace AS
SELECT t.trace_id,
 CASE WHEN count(e.event_id)=0 THEN 'live_or_incomplete'
      WHEN NOT EXISTS(SELECT 1 FROM traceloom_v_inference_span root WHERE root.trace_id=t.trace_id AND root.span_id=e.span_id AND root.has_start AND root.has_end)
      OR EXISTS(SELECT 1 FROM traceloom_v_inference_span s WHERE s.trace_id=t.trace_id AND (NOT s.has_start OR NOT s.has_end))
      THEN 'finished_with_incomplete_spans' ELSE 'finished_observed' END AS state,
 max(json_extract(e.payload,'$.status')) AS terminal_status
FROM (SELECT DISTINCT trace_id FROM traceloom_inference_event) t
LEFT JOIN traceloom_inference_event e ON e.trace_id=t.trace_id AND e.event_type='trace_end'
GROUP BY t.trace_id;
)SQL");
}
}  // namespace traceloom::inference::detail
