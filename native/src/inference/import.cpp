#include <filesystem>
#include <fstream>

#include "internal.h"
#include "traceloom/core/sha256.h"
#include "traceloom/inference/trace.h"

namespace traceloom::inference {
namespace fs = std::filesystem;
ImportReceipt import_ndjson(const std::string& input,
                            const std::string& database,
                            const ImportOptions& options) {
  using namespace detail;
  require(options.max_events > 0 && options.max_events <= 100000,
          "invalid retained event limit");
  require(fs::is_regular_file(input),
          "inference input must be a regular NDJSON file");
  require(fs::weakly_canonical(input) != fs::weakly_canonical(database) &&
              (!fs::exists(database) || !fs::equivalent(input, database)),
          "input/output paths collide");
  constexpr std::size_t file_limit = 64 * 1024 * 1024,
                        line_limit = 16 * 1024 - 1;
  // Freeze byte length so a continuously appending producer cannot extend a
  // transaction.
  const auto bytes = fs::file_size(input);
  require(bytes <= file_limit, "inference snapshot exceeds 64 MiB");
  if (!fs::path(database).parent_path().empty())
    fs::create_directories(fs::path(database).parent_path());
  const bool existed = fs::exists(database);
  auto db = open(database);
  if (!existed)
    fs::permissions(database, fs::perms::owner_read | fs::perms::owner_write);
  exec(db.get(), "BEGIN IMMEDIATE");
  ImportReceipt receipt;
  try {
    initialize(db.get(), options.include_summaries);
    auto insert = prepare(db.get(), R"SQL(INSERT INTO traceloom_inference_event
      SELECT json_extract(?1,'$.trace_id'),json_extract(?1,'$.event_id'),
      json_extract(?1,'$.span_id'),json_extract(?1,'$.producer_id'),
      json_extract(?1,'$.sequence'),json_extract(?1,'$.event_type'),?1,?2,?3)SQL");
    auto existing =
        prepare(db.get(), R"SQL(SELECT payload FROM traceloom_inference_event
      WHERE trace_id=json_extract(?1,'$.trace_id') AND event_id=json_extract(?1,'$.event_id'))SQL");
    std::ifstream in(input, std::ios::binary);
    require(bool(in), "cannot read inference input");
    std::string line;
    std::size_t number = 1;
    for (std::uintmax_t i = 0; i < bytes; ++i) {
      char c;
      require(bool(in.get(c)), "inference input changed during snapshot read");
      if (c != '\n') {
        require(line.size() < line_limit, "inference line exceeds 16 KiB");
        line += c;
        continue;
      }
      try {
        const auto payload =
            normalize(db.get(), line, options.include_summaries,
                      receipt.discarded_attributes);
        bind_text(existing.get(), 1, payload);
        int rc = sqlite3_step(existing.get());
        if (rc == SQLITE_ROW) {
          require(text(existing.get(), 0) == payload,
                  "conflicting inference event identity");
          ++receipt.duplicates;
        } else {
          require(rc == SQLITE_DONE, "cannot read event identity");
          bind_text(insert.get(), 1, payload);
          sqlite3_bind_int64(insert.get(), 2,
                             static_cast<sqlite3_int64>(number));
          bind_text(insert.get(), 3, sha256_hex(payload));
          require(sqlite3_step(insert.get()) == SQLITE_DONE,
                  "conflicting lifecycle or producer sequence");
          sqlite3_reset(insert.get());
          sqlite3_clear_bindings(insert.get());
          ++receipt.inserted;
        }
        sqlite3_reset(existing.get());
        sqlite3_clear_bindings(existing.get());
      } catch (const std::exception& e) {
        throw std::runtime_error("inference line " + std::to_string(number) +
                                 ": " + e.what());
      }
      line.clear();
      ++number;
    }
    receipt.pending_tail_bytes = line.size();
    auto count = prepare(db.get(),
                         "SELECT count(*),coalesce(sum(length(CAST(payload AS "
                         "BLOB))),0) FROM traceloom_inference_event");
    require(sqlite3_step(count.get()) == SQLITE_ROW &&
                sqlite3_column_int64(count.get(), 0) <=
                    static_cast<sqlite3_int64>(options.max_events) &&
                sqlite3_column_int64(count.get(), 1) <=
                    static_cast<sqlite3_int64>(file_limit),
            "retained inference event/byte limit exceeded");
    count.reset();
    auto identity =
        prepare(db.get(), R"SQL(SELECT count(*) FROM traceloom_inference_event e
      JOIN traceloom_inference_event s ON e.trace_id=s.trace_id AND e.span_id=s.span_id AND s.event_type='span_start'
      WHERE e.event_type IN ('span_end','span_event') AND (
       e.producer_id!=s.producer_id OR json_extract(e.payload,'$.clock_id')!=json_extract(s.payload,'$.clock_id')
       OR (json_type(e.payload,'$.parent_span_id') IS NOT NULL AND json_extract(e.payload,'$.parent_span_id') IS NOT json_extract(s.payload,'$.parent_span_id'))
       OR (json_type(e.payload,'$.kind') IS NOT NULL AND json_extract(e.payload,'$.kind') IS NOT json_extract(s.payload,'$.kind'))
       OR (json_type(e.payload,'$.name') IS NOT NULL AND e.event_type='span_end' AND json_extract(e.payload,'$.name') IS NOT json_extract(s.payload,'$.name'))
       OR (json_extract(e.payload,'$.links')!='[]' AND json_extract(e.payload,'$.links')!=json_extract(s.payload,'$.links'))))SQL");
    require(sqlite3_step(identity.get()) == SQLITE_ROW &&
                sqlite3_column_int64(identity.get(), 0) == 0,
            "inconsistent span identity or clock");
    identity.reset();
    auto snapshot = load(db.get());
    exec(db.get(), "DELETE FROM traceloom_inference_span_metric");
    auto metric =
        prepare(db.get(),
                "INSERT INTO "
                "traceloom_inference_span_metric(trace_id,span_id,depth,"
                "missing_parent,observed_path_ns) VALUES(?,?,?,?,?)");
    for (const auto& s : snapshot.spans) {
      bind_text(metric.get(), 1, s.trace);
      bind_text(metric.get(), 2, s.id);
      sqlite3_bind_int64(metric.get(), 3, static_cast<sqlite3_int64>(s.depth));
      sqlite3_bind_int(metric.get(), 4, s.missing_parent ? 1 : 0);
      if (s.path_observed)
        sqlite3_bind_int64(metric.get(), 5, s.path_ns);
      else
        sqlite3_bind_null(metric.get(), 5);
      require(sqlite3_step(metric.get()) == SQLITE_DONE,
              "cannot materialize inference metrics");
      sqlite3_reset(metric.get());
      sqlite3_clear_bindings(metric.get());
    }
    metric.reset();
    // A completion receipt cannot contradict an observed root terminal state.
    auto terminal =
        prepare(db.get(), R"SQL(SELECT count(*) FROM traceloom_inference_event e
      JOIN traceloom_v_inference_span s ON e.trace_id=s.trace_id AND e.span_id=s.span_id
      WHERE e.event_type='trace_end' AND
      (s.parent_span_id IS NOT NULL OR (s.has_start AND s.has_end AND
       s.status!=json_extract(e.payload,'$.status'))))SQL");
    require(sqlite3_step(terminal.get()) == SQLITE_ROW &&
                sqlite3_column_int64(terminal.get(), 0) == 0,
            "trace completion contradicts root span");
    terminal.reset();
    insert.reset();
    existing.reset();
    exec(db.get(), "COMMIT");
  } catch (...) {
    sqlite3_exec(db.get(), "ROLLBACK", nullptr, nullptr, nullptr);
    throw;
  }
  return receipt;
}
bool is_inference_database(const std::string& path) {
  try {
    auto db = detail::open(path, true);
    detail::check_version(db.get());
    return true;
  } catch (...) {
    return false;
  }
}
}  // namespace traceloom::inference
