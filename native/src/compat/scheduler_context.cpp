#include "traceloom/compat/scheduler_context.h"
#include "sidecar_sqlite_utils.h"
#include "sidecar_views.h"
#include "scheduler_replay_context.h"
#include "scheduler_query_views.h"
#include "traceloom/core/sha256.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
#include <sqlite3.h>
namespace traceloom::compat {
namespace {
using namespace detail;
using Db = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Stmt = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
Stmt prepare(sqlite3 *db, const std::string &sql) {
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    throw std::runtime_error("context SQL preparation failed: " +
                             std::string(sqlite3_errmsg(db)));
  return Stmt(stmt, sqlite3_finalize);
}
void exec(sqlite3 *db, const std::string &sql) {
  sqlite_exec(db, sql, "scheduler context import failed");
}
std::string text(sqlite3_stmt *stmt, int col) {
  const auto *value = sqlite3_column_text(stmt, col);
  return value ? std::string(reinterpret_cast<const char *>(value),
                             sqlite3_column_bytes(stmt, col))
               : "";
}
void require_no_rows(sqlite3 *db, const std::string &sql, const char *reason) {
  auto stmt = prepare(db, sql);
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE)
    throw std::runtime_error(reason);
}
const char *kSchema = R"SQL(
CREATE TABLE traceloom_context_source(
 source_id INTEGER PRIMARY KEY, source_path TEXT NOT NULL UNIQUE,
 sha256 TEXT NOT NULL, size_bytes INTEGER NOT NULL, capture_state TEXT NOT NULL);
CREATE TABLE traceloom_context_record(
 source_id INTEGER NOT NULL, line_no INTEGER NOT NULL,
 record_type TEXT NOT NULL, run_id TEXT NOT NULL, producer_id TEXT NOT NULL,
 raw_json TEXT NOT NULL CHECK(json_valid(raw_json)), PRIMARY KEY(source_id,line_no));
CREATE UNIQUE INDEX idx_context_session ON traceloom_context_record(run_id,producer_id)
 WHERE record_type='session';
CREATE UNIQUE INDEX idx_context_step_id ON traceloom_context_record(run_id,json_extract(raw_json,'$.step_id'))
 WHERE record_type='step';
CREATE UNIQUE INDEX idx_context_execution_id ON traceloom_context_record(json_extract(raw_json,'$.execution_id'))
 WHERE record_type='execution';
CREATE TABLE traceloom_context_marker(
 execution_id TEXT NOT NULL, source_id TEXT NOT NULL, source_path TEXT NOT NULL,
 source_table TEXT NOT NULL, source_key TEXT NOT NULL, marker_name TEXT NOT NULL,
 start_ns INTEGER NOT NULL, end_ns INTEGER NOT NULL, global_tid TEXT,
 PRIMARY KEY(execution_id,source_id,source_table,source_key));
CREATE TABLE traceloom_context_task_queue(
 source_id TEXT,source_path TEXT,source_key TEXT,queue_kind TEXT,
 start_ns INTEGER,end_ns INTEGER,global_tid TEXT,connection_id INTEGER);
CREATE INDEX idx_context_queue_identity ON traceloom_context_task_queue(source_id,connection_id,queue_kind);
)SQL";

void load_file(sqlite3 *db, const std::string &path, std::size_t source_id) {
  namespace fs = std::filesystem;
  if (!fs::is_regular_file(path) || fs::file_size(path) > 256ULL * 1024 * 1024)
    throw std::invalid_argument(
        "context must be a regular JSONL file of at most 256 MiB");
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot open context input");
  auto metadata =
      prepare(db, "SELECT json_type(?1),json_extract(?1,'$.schema'),"
                  "json_extract(?1,'$.type'),json_extract(?1,'$.run_id'),"
                  "json_extract(?1,'$.producer_id'),json_type(?1,'$.run_id'),"
                  "json_type(?1,'$.producer_id')");
  auto insert = prepare(
      db, "INSERT INTO traceloom_context_record VALUES(?1,?2,?3,?4,?5,?6)");
  std::string line, run, producer;
  std::size_t line_no = 0, records = 0;
  bool closed = false;
  Sha256 digest;
  std::uint64_t bytes = 0;
  while (std::getline(input, line)) {
    ++line_no;
    // A missing final newline indicates a potentially torn writer record.
    if (input.eof())
      throw std::invalid_argument("context record lacks terminating newline");
    if (line.size() > 1024 * 1024)
      throw std::invalid_argument("context record exceeds 1 MiB");
    digest.update(line);
    digest.update("\n");
    bytes += line.size() + 1;
    if (bytes > 256ULL * 1024 * 1024)
      throw std::invalid_argument("context exceeds 256 MiB");
    if (closed)
      throw std::invalid_argument("context contains records after summary");
    sqlite3_bind_text(metadata.get(), 1, line.data(),
                      static_cast<int>(line.size()), SQLITE_TRANSIENT);
    if (sqlite3_step(metadata.get()) != SQLITE_ROW ||
        text(metadata.get(), 0) != "object" ||
        text(metadata.get(), 1) != "traceloom.scheduler.v1" ||
        text(metadata.get(), 5) != "text" || text(metadata.get(), 6) != "text")
      throw std::invalid_argument(
          "invalid context JSON, schema version, or identity type");
    const std::string kind = text(metadata.get(), 2);
    const std::string row_run = text(metadata.get(), 3),
                      row_producer = text(metadata.get(), 4);
    sqlite3_reset(metadata.get());
    const auto safe_identity = [](const std::string &id) {
      return !id.empty() && id.size() <= 128 &&
             std::all_of(id.begin(), id.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                      c == '.';
             });
    };
    if (!safe_identity(row_run) || !safe_identity(row_producer))
      throw std::invalid_argument("invalid context identity");
    if (line_no == 1) {
      if (kind != "session")
        throw std::invalid_argument("context must begin with a session");
      run = row_run;
      producer = row_producer;
    } else if (kind == "session" || run != row_run ||
               producer != row_producer) {
      throw std::invalid_argument(
          "context producer identity changes within file");
    }
    if (kind != "session" && kind != "step" && kind != "execution" &&
        kind != "summary")
      throw std::invalid_argument("unknown context record type");
    closed = kind == "summary";
    if (kind == "step" || kind == "execution")
      ++records;
    sqlite3_bind_int64(insert.get(), 1, source_id);
    sqlite3_bind_int64(insert.get(), 2, line_no);
    for (const auto &pair : std::vector<std::pair<int, std::string>>{
             {3, kind}, {4, run}, {5, producer}, {6, line}})
      sqlite3_bind_text(insert.get(), pair.first, pair.second.data(),
                        static_cast<int>(pair.second.size()), SQLITE_TRANSIENT);
    if (sqlite3_step(insert.get()) != SQLITE_DONE)
      throw std::invalid_argument(
          "duplicate or invalid context record identity");
    sqlite3_reset(insert.get());
  }
  if (input.bad() || line_no == 0)
    throw std::invalid_argument("empty or unreadable context file");
  if (closed) {
    auto summary = prepare(db, "SELECT json_type(raw_json,'$.written_records'),"
                               "json_extract(raw_json,'$.written_records'),"
                               "json_type(raw_json,'$.dropped_records'),"
                               "json_extract(raw_json,'$.dropped_records') "
                               "FROM traceloom_context_record "
                               "WHERE source_id=" +
                                   std::to_string(source_id) +
                                   " AND record_type='summary'");
    if (sqlite3_step(summary.get()) != SQLITE_ROW ||
        text(summary.get(), 0) != "integer" ||
        sqlite3_column_int64(summary.get(), 1) !=
            static_cast<sqlite3_int64>(records) ||
        text(summary.get(), 2) != "integer" ||
        sqlite3_column_int64(summary.get(), 3) < 0)
      throw std::invalid_argument(
          "context summary counts do not match records");
    closed = sqlite3_column_int64(summary.get(), 3) == 0;
    // A summary with dropped records is complete as a file but incomplete
    // evidence.
    const std::string state = closed ? "closed" : "dropped_records";
    exec(db, "INSERT INTO traceloom_context_source VALUES(" +
                 std::to_string(source_id) + "," + quote_literal(path) + "," +
                 quote_literal(digest.hex_digest()) + "," +
                 std::to_string(bytes) + "," + quote_literal(state) + ")");
  } else {
    exec(db, "INSERT INTO traceloom_context_source VALUES(" +
                 std::to_string(source_id) + "," + quote_literal(path) + "," +
                 quote_literal(digest.hex_digest()) + "," +
                 std::to_string(bytes) + ",'unclosed')");
  }
}

void normalize(sqlite3 *db) {
  require_no_rows(
      db,
      R"SQL(SELECT 1 FROM traceloom_context_record WHERE record_type='session'
    AND NOT COALESCE(json_type(raw_json,'$.metadata')='object',0))SQL",
      "invalid session metadata");
  for (const char *field : {"free_blocks", "pool_blocks"}) {
    const std::string path = std::string("$.cache.") + field;
    require_no_rows(
        db,
        "SELECT 1 FROM traceloom_context_record WHERE record_type='step' "
        "AND NOT COALESCE(json_type(raw_json," +
            quote_literal(path) +
            ") IS NULL OR "
            "json_type(raw_json," +
            quote_literal(path) + ")='null' OR (json_type(raw_json," +
            quote_literal(path) + ")='integer' AND json_extract(raw_json," +
            quote_literal(path) + ")>=0),0)",
        "invalid cache counter");
  }

  require_no_rows(
      db,
      R"SQL(SELECT 1 FROM traceloom_context_record WHERE record_type='step' AND NOT COALESCE(
    json_type(raw_json,'$.step_id')='text' AND length(json_extract(raw_json,'$.step_id')) BETWEEN 1 AND 128
    AND json_type(raw_json,'$.ordinal')='integer' AND json_extract(raw_json,'$.ordinal')>=0
    AND json_type(raw_json,'$.total_scheduled_tokens')='integer' AND json_extract(raw_json,'$.total_scheduled_tokens')>=0
    AND json_type(raw_json,'$.requests')='array'
    AND json_array_length(raw_json,'$.requests')<=4096
    AND json_type(raw_json,'$.cache')='object'
    AND json_extract(raw_json,'$.cache.observation_phase')='after_schedule',0))SQL",
      "invalid scheduler step or unsupported cache observation phase");
  require_no_rows(
      db,
      R"SQL(SELECT 1 FROM traceloom_context_record WHERE record_type='execution' AND NOT COALESCE(
    json_type(raw_json,'$.execution_id')='text' AND length(json_extract(raw_json,'$.execution_id')) BETWEEN 1 AND 128
    AND json_type(raw_json,'$.step_id')='text' AND length(json_extract(raw_json,'$.step_id')) BETWEEN 1 AND 128
    AND json_extract(raw_json,'$.marker')='traceloom.execution.'||json_extract(raw_json,'$.execution_id')
    AND (json_type(raw_json,'$.phase') IS NULL OR json_extract(raw_json,'$.phase') IN ('execute_model','sample_tokens'))
    AND json_extract(raw_json,'$.status') IN ('returned','raised')
    AND json_extract(raw_json,'$.marker_state') IN ('emitted','unavailable'),0))SQL",
      "invalid context execution or marker identity");
  require_no_rows(
      db,
      R"SQL(SELECT 1 FROM traceloom_context_record r, json_each(r.raw_json,'$.requests') q
    WHERE r.record_type='step' AND NOT COALESCE(json_type(q.value)='object'
    AND json_type(q.value,'$.request_id')='text' AND length(json_extract(q.value,'$.request_id')) BETWEEN 1 AND 128
    AND json_type(q.value,'$.scheduled_tokens')='integer' AND json_extract(q.value,'$.scheduled_tokens')>=0,0))SQL",
      "invalid scheduled request");
  exec(db, R"SQL(
CREATE TABLE traceloom_scheduler_step AS
 SELECT run_id, json_extract(raw_json,'$.step_id') AS step_id, producer_id AS scheduler_id,
 json_extract(raw_json,'$.ordinal') AS ordinal,
 json_extract(raw_json,'$.total_scheduled_tokens') AS total_scheduled_tokens,
 json_array_length(raw_json,'$.requests') AS scheduled_requests,
 json_extract(raw_json,'$.cache') AS cache_json,
 json_extract(raw_json,'$.cache.free_blocks') AS free_blocks_after_schedule,
 json_extract(raw_json,'$.cache.pool_blocks') AS pool_blocks_after_schedule,source_id,line_no
 FROM traceloom_context_record WHERE record_type='step';
CREATE UNIQUE INDEX idx_scheduler_step ON traceloom_scheduler_step(run_id,step_id);
CREATE UNIQUE INDEX idx_scheduler_ordinal ON traceloom_scheduler_step(run_id,scheduler_id,ordinal);
CREATE TABLE traceloom_scheduler_request AS
 SELECT r.run_id,json_extract(r.raw_json,'$.step_id') AS step_id,
 json_extract(q.value,'$.request_id') AS request_id,
 json_extract(q.value,'$.scheduled_tokens') AS scheduled_tokens,
 q.value AS request_json,r.source_id,r.line_no
 FROM traceloom_context_record r,json_each(r.raw_json,'$.requests') q WHERE r.record_type='step';
CREATE UNIQUE INDEX idx_scheduler_request ON traceloom_scheduler_request(run_id,step_id,request_id);
CREATE TABLE traceloom_context_execution AS
 SELECT run_id,json_extract(raw_json,'$.execution_id') AS execution_id,
 json_extract(raw_json,'$.step_id') AS step_id,json_extract(raw_json,'$.marker') AS marker,
 json_extract(raw_json,'$.worker_rank') AS worker_rank,
 COALESCE(json_extract(raw_json,'$.phase'),'execute_model') AS phase,
 json_extract(raw_json,'$.status') AS execution_status,
 json_extract(raw_json,'$.marker_state') AS marker_state,source_id,line_no
 FROM traceloom_context_record WHERE record_type='execution';
CREATE UNIQUE INDEX idx_context_execution ON traceloom_context_execution(execution_id);
CREATE INDEX idx_context_execution_step ON traceloom_context_execution(run_id,step_id);
CREATE UNIQUE INDEX idx_context_execution_marker ON traceloom_context_execution(marker);
)SQL");
  require_no_rows(
      db,
      R"SQL(SELECT 1 FROM traceloom_scheduler_step s WHERE s.total_scheduled_tokens !=
    (SELECT COALESCE(SUM(q.scheduled_tokens),0) FROM traceloom_scheduler_request q
     WHERE q.run_id=s.run_id AND q.step_id=s.step_id))SQL",
      "scheduled token total does not equal request sum");
}

std::set<std::string> columns(sqlite3 *db, const std::string &table) {
  auto stmt = prepare(db, "PRAGMA table_info(" + quote_identifier(table) + ")");
  std::set<std::string> result;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW)
    result.insert(text(stmt.get(), 1));
  return result;
}

void bind_markers(sqlite3 *db) {
  auto tables = prepare(
      db,
      "SELECT source_id,source_path,embedded_table_name,source_rowid_column "
      "FROM traceloom_raw_table WHERE source_table='PYTORCH_API'");
  while (sqlite3_step(tables.get()) == SQLITE_ROW) {
    const std::string source_id = text(tables.get(), 0),
                      path = text(tables.get(), 1);
    const std::string table = text(tables.get(), 2),
                      rowid = text(tables.get(), 3);
    if (rowid.empty())
      continue; // No auditable source-row coordinate.
    const auto fields = columns(db, table);
    bool supported = true;
    for (const char *field : {"name", "startNs", "endNs", "globalTid"})
      supported = supported && fields.count(field);
    if (!supported)
      continue;
    auto strings = prepare(
        db, "SELECT embedded_table_name FROM traceloom_raw_table "
            "WHERE source_id=" +
                quote_literal(source_id) + " AND source_table='STRING_IDS'");
    std::string string_table;
    if (sqlite3_step(strings.get()) == SQLITE_ROW)
      string_table = text(strings.get(), 0);
    std::string name = "p.name", string_join;
    if (!string_table.empty()) {
      const auto string_fields = columns(db, string_table);
      if (string_fields.count("id") && string_fields.count("value")) {
        string_join = " LEFT JOIN " + quote_identifier(string_table) +
                      " n ON n.id=p.name ";
        name = "CASE WHEN typeof(p.name)='text' THEN p.name ELSE n.value END";
      }
    }
    exec(db, "INSERT INTO traceloom_context_marker SELECT e.execution_id," +
                 quote_literal(source_id) + "," + quote_literal(path) +
                 ",'PYTORCH_API',CAST(p." + quote_identifier(rowid) +
                 " AS TEXT)," + name +
                 ",CAST(p.startNs AS INTEGER),CAST(p.endNs AS INTEGER),CAST(p.globalTid AS TEXT) FROM " +
                 quote_identifier(table) + " p" + string_join +
                 " JOIN traceloom_context_execution e ON e.marker=" + name +
                 " WHERE e.marker_state='emitted' AND "
                 "CAST(CAST(p.startNs AS INTEGER) AS TEXT)=CAST(p.startNs AS TEXT) AND "
                 "CAST(CAST(p.endNs AS INTEGER) AS TEXT)=CAST(p.endNs AS TEXT) AND "
                 "CAST(p.startNs AS INTEGER)>=0 AND "
                 "CAST(p.endNs AS INTEGER)>=CAST(p.startNs AS INTEGER) AND "
                 "typeof(p.globalTid)='integer' AND "
                 "p.globalTid>=0");

    // torch-npu task queue IDs explicitly bridge Python and CANN submission
    // threads. Never infer this bridge from relative time or list position.
    if (!fields.count("connectionId") || !fields.count("type")) continue;
    auto ids = prepare(db, "SELECT embedded_table_name FROM traceloom_raw_table "
                          "WHERE source_id=" + quote_literal(source_id) +
                          " AND source_table='CONNECTION_IDS'");
    if (sqlite3_step(ids.get()) != SQLITE_ROW) continue;
    const std::string ids_table = text(ids.get(), 0);
    const auto id_fields = columns(db, ids_table);
    if (!id_fields.count("id") || !id_fields.count("connectionId")) continue;
    exec(db, "INSERT INTO traceloom_context_task_queue SELECT DISTINCT " +
             quote_literal(source_id) + "," + quote_literal(path) +
             ",CAST(p." + quote_identifier(rowid) + " AS TEXT),"
             "CASE WHEN substr(" + name + ",1,8)='Enqueue@' THEN 'enqueue' ELSE 'dequeue' END,"
             "CAST(p.startNs AS INTEGER),CAST(p.endNs AS INTEGER),CAST(p.globalTid AS TEXT),"
             "ids.connectionId FROM " + quote_identifier(table) + " p" + string_join +
             " JOIN " + quote_identifier(ids_table) + " ids ON ids.id=p.connectionId"
             " WHERE p.type=50002 AND (substr(" + name + ",1,8)='Enqueue@' OR substr(" +
             name + ",1,8)='Dequeue@') AND "
             "CAST(CAST(p.startNs AS INTEGER) AS TEXT)=CAST(p.startNs AS TEXT) AND "
             "CAST(CAST(p.endNs AS INTEGER) AS TEXT)=CAST(p.endNs AS TEXT) AND "
             "CAST(p.startNs AS INTEGER)>=0 AND CAST(p.endNs AS INTEGER)>=CAST(p.startNs AS INTEGER) AND "
             "typeof(p.globalTid)='integer' AND p.globalTid>=0 AND "
             "typeof(ids.connectionId)='integer' AND ids.connectionId>=0");
  }
  exec(db, R"SQL(
CREATE VIEW traceloom_v_execution_context AS
 SELECT e.*,s.scheduler_id,s.ordinal,s.total_scheduled_tokens,s.scheduled_requests,s.cache_json,
 (SELECT COUNT(*) FROM traceloom_context_marker m WHERE m.execution_id=e.execution_id) AS marker_count,
 CASE WHEN s.step_id IS NULL THEN 'missing_step'
      WHEN e.marker_state!='emitted' THEN 'marker_unavailable'
      WHEN (SELECT COUNT(*) FROM traceloom_context_marker m WHERE m.execution_id=e.execution_id)=0 THEN 'marker_not_found'
      WHEN (SELECT COUNT(*) FROM traceloom_context_marker m WHERE m.execution_id=e.execution_id)>1 THEN 'ambiguous_marker'
      ELSE 'supported_marker' END AS support_state
 FROM traceloom_context_execution e LEFT JOIN traceloom_scheduler_step s
 ON s.run_id=e.run_id AND s.step_id=e.step_id;
CREATE INDEX idx_context_host_scope ON traceloom_runtime_call(provider,clock_domain,global_tid,start_ns,end_ns);
CREATE TABLE traceloom_context_runtime_candidate AS
 SELECT e.execution_id,c.runtime_call_id,
 'same_thread_host_scope' AS association_basis
 FROM traceloom_v_execution_context e JOIN traceloom_context_marker m USING(execution_id)
 JOIN traceloom_runtime_call c ON c.provider='ascend' AND c.clock_domain='profiler_host'
 AND json_extract(c.raw_json,'$.source_path')=m.source_path
 AND c.global_tid=m.global_tid AND c.start_ns>=m.start_ns
 AND c.start_ns<=m.end_ns AND c.end_ns<=m.end_ns
 WHERE e.support_state='supported_marker';
CREATE TABLE traceloom_context_queue_pair AS
 WITH queue AS (
 SELECT *,COUNT(*) OVER(PARTITION BY source_id,connection_id,queue_kind) AS identity_count,
          COUNT(*) OVER(PARTITION BY source_id,source_key) AS row_id_count
 FROM traceloom_context_task_queue)
 SELECT a.source_id,a.source_path,a.source_key AS enqueue_key,b.source_key AS dequeue_key,
 a.connection_id,a.start_ns AS enqueue_start_ns,a.end_ns AS enqueue_end_ns,
 a.global_tid AS enqueue_tid,b.start_ns AS dequeue_start_ns,b.end_ns AS dequeue_end_ns,
 b.global_tid AS dequeue_tid
 FROM queue a JOIN queue b ON a.source_id=b.source_id AND a.connection_id=b.connection_id
 WHERE a.queue_kind='enqueue' AND b.queue_kind='dequeue'
 AND a.identity_count=1 AND b.identity_count=1 AND a.row_id_count=1 AND b.row_id_count=1
 AND a.start_ns<=b.start_ns
 AND (CAST(a.global_tid AS INTEGER)>>32)=(CAST(b.global_tid AS INTEGER)>>32);
CREATE INDEX idx_context_queue_scope ON traceloom_context_queue_pair(enqueue_tid,enqueue_start_ns);
-- Keep the selected marker outermost and bound both ends of the indexed start
-- columns. Reordering from all runtime calls can make this join quadratic.
CREATE TABLE traceloom_context_queue_runtime AS
 SELECT e.execution_id,c.runtime_call_id,q.source_id,q.enqueue_key,q.dequeue_key,q.connection_id
 FROM traceloom_v_execution_context e JOIN traceloom_context_marker m USING(execution_id)
 CROSS JOIN traceloom_context_queue_pair q ON q.source_path=m.source_path
 AND q.enqueue_tid=m.global_tid AND q.enqueue_start_ns>=m.start_ns
 AND q.enqueue_start_ns<=m.end_ns AND q.enqueue_end_ns<=m.end_ns
 CROSS JOIN traceloom_runtime_call c ON c.provider='ascend' AND c.clock_domain='profiler_host'
 AND json_extract(c.raw_json,'$.source_path')=q.source_path AND c.global_tid=q.dequeue_tid
 AND c.start_ns>=q.dequeue_start_ns AND c.start_ns<=q.dequeue_end_ns
 AND c.end_ns<=q.dequeue_end_ns
 WHERE e.support_state='supported_marker';
INSERT INTO traceloom_context_runtime_candidate
 SELECT DISTINCT execution_id,runtime_call_id,'task_queue_connection'
 FROM traceloom_context_queue_runtime;
CREATE TABLE traceloom_context_runtime_call AS
 SELECT *,CASE WHEN COUNT(*) OVER(PARTITION BY runtime_call_id)=1 THEN 'supported_host_scope'
               ELSE 'ambiguous_execution_scope' END AS support_state
 FROM (SELECT execution_id,runtime_call_id,MIN(association_basis) AS association_basis
       FROM traceloom_context_runtime_candidate GROUP BY execution_id,runtime_call_id);
CREATE UNIQUE INDEX idx_context_runtime ON traceloom_context_runtime_call(execution_id,runtime_call_id);
CREATE INDEX idx_context_runtime_reverse ON traceloom_context_runtime_call(runtime_call_id);
CREATE VIEW traceloom_v_context_direct_device_candidate AS
 SELECT e.run_id,e.step_id,e.execution_id,e.worker_rank,e.phase,c.runtime_call_id,
 d.device_work_id,d.device_id,d.event_id,d.start_ns,d.end_ns,d.dur_us,d.symbol,
 r.support_state AS provider_support_state,
 c.association_basis||'_then_provider_relation' AS association_basis
 FROM traceloom_context_runtime_call c JOIN traceloom_context_execution e USING(execution_id)
 JOIN traceloom_runtime_device_relation r USING(runtime_call_id)
 JOIN traceloom_device_work d USING(device_work_id)
 WHERE c.support_state='supported_host_scope'
 AND r.support_state IN ('supported_exact','supported_deterministic');
)SQL");
  bind_scheduler_replay_context(db);
  exec(db, R"SQL(
CREATE TABLE traceloom_context_device_assignment AS
 SELECT device_work_id,COUNT(*) AS step_count FROM
 (SELECT DISTINCT run_id,step_id,device_work_id FROM traceloom_v_context_device_candidate)
 GROUP BY device_work_id;
CREATE UNIQUE INDEX idx_context_device_assignment ON traceloom_context_device_assignment(device_work_id);
CREATE VIEW traceloom_v_context_device_work AS
 SELECT c.* FROM traceloom_v_context_device_candidate c
 JOIN traceloom_context_device_assignment a USING(device_work_id) WHERE a.step_count=1;
CREATE VIEW traceloom_v_context_device_coverage AS
 SELECT d.device_work_id,d.device_id,d.event_id,d.symbol,d.start_ns,d.end_ns,d.dur_us,
 COALESCE(a.step_count,0) AS candidate_steps,
 CASE WHEN a.step_count=1 THEN 'supported_step'
      WHEN a.step_count>1 THEN 'ambiguous_step'
      WHEN EXISTS(SELECT 1 FROM traceloom_runtime_device_relation r
                  WHERE r.device_work_id=d.device_work_id
                  AND r.support_state IN ('supported_exact','supported_deterministic'))
        THEN 'outside_supported_execution_scope'
      ELSE 'no_supported_provider_relation' END AS support_state
 FROM traceloom_device_work d LEFT JOIN traceloom_context_device_assignment a USING(device_work_id);
CREATE VIEW traceloom_v_scheduler_step_context AS
 SELECT s.*,f.capture_state,
 (SELECT COUNT(*) FROM traceloom_context_execution e WHERE e.run_id=s.run_id AND e.step_id=s.step_id) AS recorded_executions,
 (SELECT COUNT(*) FROM traceloom_v_execution_context e WHERE e.run_id=s.run_id AND e.step_id=s.step_id
  AND e.support_state='supported_marker') AS supported_markers
 FROM traceloom_scheduler_step s JOIN traceloom_context_source f USING(source_id);
CREATE VIEW traceloom_v_context_coverage AS
 SELECT e.*,f.capture_state AS execution_capture_state,
 (SELECT COUNT(*) FROM traceloom_context_runtime_call c WHERE c.execution_id=e.execution_id
  AND c.support_state='supported_host_scope') AS supported_runtime_calls,
 (SELECT COUNT(*) FROM traceloom_v_context_device_work d WHERE d.execution_id=e.execution_id) AS linked_device_work,
 'not_complete_step_membership' AS membership_contract
 FROM traceloom_v_execution_context e JOIN traceloom_context_source f ON f.source_id=e.source_id;
)SQL");
}

void catalog(sqlite3 *db) {
  struct Entry {
    const char *name;
    const char *relation;
    const char *grain;
    const char *purpose;
  };
  for (const auto &e : std::vector<Entry>{
           {"scheduler_steps", "traceloom_v_scheduler_step_context", "run/step",
            "Supplied scheduler decisions and after-schedule cache counters"},
           {"scheduler_requests", "traceloom_scheduler_request",
            "run/step/request", "Pseudonymous scheduled request allocations"},
           {"execution_context", "traceloom_v_context_coverage", "execution",
            "Exact marker association and explicit unsupported states"},
           {"context_device_work", "traceloom_v_context_device_work",
            "execution/device work",
            "Supported host scope then provider correlation, not full step "
            "ownership"},
           {"context_device_coverage", "traceloom_v_context_device_coverage",
            "device work", "Global device denominator and unsupported step attribution"},
           {"context_replay_launch", "traceloom_v_context_replay_launch",
            "execution/launch", "Exact launch identity associated with a supplied step"},
           {"context_replay_member", "traceloom_v_context_replay_member",
            "execution/launch/member", "Exact replay body members; parallel lanes may overlap"},
           {"context_anchor", "traceloom_v_context_anchor",
            "anchor/step/device work", "Ordinary and exact replay anchor identities, not time containment"},
           {"context_queue_runtime", "traceloom_context_queue_runtime",
            "execution/runtime/queue", "Explicit task queue source-row identity audit"},
           {"context_sources", "traceloom_context_source", "source",
            "Input identity and closed/dropped/unclosed capture state"},
           {"context_records", "traceloom_context_record", "source/line",
            "Verbatim runtime context evidence including configuration and "
            "summary"}}) {
    const std::string query =
        "SELECT * FROM " + std::string(e.relation) + " LIMIT 20";
    exec(db, "INSERT INTO traceloom_analysis_surface VALUES(" +
                 quote_literal(e.name) + "," + quote_literal(e.relation) + "," +
                 quote_literal(e.grain) + "," + quote_literal(e.purpose) + "," +
                 quote_literal(query) + ")");
  }
  exec(db, "INSERT INTO traceloom_projection_recipe "
           "VALUES('step_execution_context',25,'scheduler_step',"
           "'selected','execution','runtime_context','none','run_id,step_id',"
           "'Select a supplied scheduler step and preserve unmatched execution "
           "states'," +
               quote_literal(
                   "SELECT s.*,e.execution_id,e.support_state AS "
                   "execution_support FROM traceloom_v_scheduler_step_context "
                   "s LEFT JOIN traceloom_v_execution_context e ON "
                   "e.run_id=s.run_id AND e.step_id=s.step_id WHERE "
                   "s.run_id=:run_id AND s.step_id=:step_id") +
               ")");
  exec(db, "INSERT INTO traceloom_projection_recipe "
           "VALUES('step_device_work',26,'scheduler_step',"
           "'selected','device_work','runtime_context','observed_duration','"
           "run_id,step_id',"
           "'Provider-linked device work within supported host submission "
           "scope; not complete step membership'," +
               quote_literal("SELECT * FROM traceloom_v_context_device_work "
                             "WHERE run_id=:run_id AND step_id=:step_id") +
               ")");
  exec(db, "INSERT INTO traceloom_projection_recipe "
           "VALUES('event_scheduler_context',26,'event',"
           "'selected','device_work','runtime_context','observed_duration','"
           "event_id',"
           "'Reverse-audit a device event to supported runtime scheduling "
           "context'," +
               quote_literal("SELECT * FROM traceloom_v_context_device_work "
                             "WHERE event_id=:event_id") +
               ")");
  exec(db, R"SQL(
INSERT INTO traceloom_projection_parameter VALUES
 ('step_execution_context',0,'run_id','TEXT',0,'runtime_run','traceloom_scheduler_step','run_id','Recorded run identity'),
 ('step_execution_context',1,'step_id','TEXT',0,'scheduler_step','traceloom_scheduler_step','step_id','Supplied scheduling decision, not a recovered macro'),
 ('step_device_work',0,'run_id','TEXT',0,'runtime_run','traceloom_scheduler_step','run_id','Recorded run identity'),
 ('step_device_work',1,'step_id','TEXT',0,'scheduler_step','traceloom_scheduler_step','step_id','Recorded scheduling decision'),
 ('event_scheduler_context',0,'event_id','TEXT',0,'normalized_event_id','traceloom_event','event_id','Retained device event identity');
INSERT INTO traceloom_projection_coordinate VALUES
 ('step_execution_context',0,'run_id','runtime_run','Preserved run'),
 ('step_execution_context',1,'step_id','scheduler_step','Preserved step'),
 ('step_device_work',0,'run_id','runtime_run','Preserved run'),
 ('step_device_work',1,'step_id','scheduler_step','Preserved step'),
 ('step_device_work',2,'event_id','normalized_event_id','Exact retained event'),
 ('step_device_work',3,'runtime_call_id','runtime_call','Provider-supported runtime endpoint'),
 ('event_scheduler_context',0,'run_id','runtime_run','Preserved run'),
 ('event_scheduler_context',1,'step_id','scheduler_step','Supplied scheduling decision'),
 ('event_scheduler_context',2,'event_id','normalized_event_id','Exact retained event');
)SQL");
}
} // namespace

void import_scheduler_context(const std::string &path,
                              const std::vector<std::string> &inputs) {
  if (inputs.empty())
    return;
  Db db(open_sqlite_readwrite(path), sqlite3_close);
  {
    SqliteDb index_db(path);
    materialize_runtime_device_indexes(index_db);
  }
  try {
    exec(db.get(), "BEGIN IMMEDIATE");
    exec(db.get(), kSchema);
    std::set<std::string> sources;
    std::size_t index = 0;
    for (const auto &input : inputs) {
      const auto absolute = std::filesystem::canonical(input).string();
      if (!sources.insert(absolute).second)
        throw std::invalid_argument("duplicate context input");
      load_file(db.get(), absolute, index++);
    }
    normalize(db.get());
    bind_markers(db.get());

    exec(db.get(), "COMMIT");
  } catch (...) {
    sqlite3_exec(db.get(), "ROLLBACK", nullptr, nullptr, nullptr);
    throw;
  }
}
void register_scheduler_context_catalog(const std::string& path,
                                        const std::vector<std::string>& inputs) {
  if (inputs.empty()) return;
  Db db(open_sqlite_readwrite(path), sqlite3_close);
  catalog(db.get());
  materialize_scheduler_query_views(db.get());
}

std::map<std::string, std::string> scheduler_event_partitions(const std::string& path) {
  Db db(open_sqlite_readwrite(path), sqlite3_close);
  // Membership is exact and capture must be closed/lossless on both endpoints.
  auto stmt = prepare(db.get(), R"SQL(
SELECT w.event_id, json_array(w.run_id,w.step_id)
FROM traceloom_v_context_device_work w
JOIN traceloom_context_execution e USING(execution_id)
JOIN traceloom_context_source f ON f.source_id=e.source_id
JOIN traceloom_v_scheduler_step_context s ON s.run_id=w.run_id AND s.step_id=w.step_id
WHERE w.event_id IS NOT NULL AND f.capture_state='closed' AND s.capture_state='closed'
GROUP BY w.event_id HAVING COUNT(DISTINCT json_array(w.run_id,w.step_id))=1
)SQL");
  std::map<std::string, std::string> result;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) result.emplace(text(stmt.get(),0),text(stmt.get(),1));
  auto launches = prepare(db.get(), R"SQL(
SELECT 'graph-launch-occurrence-'||d.graph_launch_occurrence_id,
       json_array(w.run_id,w.step_id)
FROM traceloom_v_context_device_work w
JOIN traceloom_device_work d USING(device_work_id)
JOIN traceloom_context_execution e USING(execution_id)
JOIN traceloom_context_source f ON f.source_id=e.source_id
JOIN traceloom_v_scheduler_step_context s ON s.run_id=w.run_id AND s.step_id=w.step_id
WHERE d.work_kind='graph_launch' AND f.capture_state='closed' AND s.capture_state='closed'
GROUP BY d.graph_launch_occurrence_id
HAVING COUNT(DISTINCT json_array(w.run_id,w.step_id))=1
)SQL");
  while (sqlite3_step(launches.get()) == SQLITE_ROW)
    result.emplace(text(launches.get(),0),text(launches.get(),1));
  return result;
}
} // namespace traceloom::compat
#else
namespace traceloom::compat {
void register_scheduler_context_catalog(const std::string&, const std::vector<std::string>&) {}
std::map<std::string, std::string> scheduler_event_partitions(const std::string&) {
  throw std::runtime_error("scheduler partitioning requires SQLite support");
}
void import_scheduler_context(const std::string &,
                              const std::vector<std::string> &inputs) {
  if (!inputs.empty())
    throw std::runtime_error("scheduler context requires SQLite support");
}
} // namespace traceloom::compat
#endif
