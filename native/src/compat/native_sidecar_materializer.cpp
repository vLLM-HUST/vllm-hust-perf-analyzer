#include "traceloom/compat/native_sidecar_materializer.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "traceloom/analysis/semantic_task_classifier.h"
#include "traceloom/compat/anchor_cost_breakdown_rows.h"
#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/aux_attribution_rows.h"
#include "traceloom/compat/collective_tag_rows.h"
#include "traceloom/compat/exact_graph_sql_rows.h"
#include "traceloom/compat/idle_evidence_sql_rows.h"
#include "traceloom/compat/idle_explanation_rows.h"
#include "traceloom/compat/native_graph_replay_rows.h"
#include "traceloom/compat/report_tree_rows.h"
#include "traceloom/compat/replay_cost_sql_rows.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/compat/timeline_rows.h"
#include "traceloom/core/sha256.h"
#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/pattern/grammar_state.h"
#include "traceloom/report/report_tree_builder.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
#include <sqlite3.h>
#endif

namespace traceloom::compat {
namespace {

namespace fs = std::filesystem;

class Stopwatch {
 public:
  Stopwatch() : start_(Clock::now()) {}

  double elapsed_ms() const {
    return std::chrono::duration<double, std::milli>(Clock::now() - start_)
        .count();
  }

 private:
  using Clock = std::chrono::steady_clock;
  Clock::time_point start_;
};

std::string basename_or_default(const std::string& path,
                                const std::string& fallback) {
  if (path.empty()) {
    return fallback;
  }
  const std::string::size_type pos = path.find_last_of("/\\");
  if (pos == std::string::npos) {
    return path.empty() ? fallback : path;
  }
  const std::string value = path.substr(pos + 1);
  return value.empty() ? fallback : value;
}

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
struct RawSourceDatabase {
  std::string source_id;
  std::uint32_t source_ordinal = 0;
  std::string source_path;
  std::string embedded_mode;
  std::uint64_t size_bytes = 0;
  std::string sha256;
};

struct RawTableCopy {
  std::string source_id;
  std::string source_path;
  std::string source_table;
  std::string embedded_table_name;
  std::string source_rowid_column;
  std::uint64_t row_count = 0;
};

struct RawPackagingResult {
  std::vector<RawSourceDatabase> sources;
  std::vector<RawTableCopy> tables;
};

std::string quote_identifier(const std::string& value) {
  std::string out = "\"";
  for (const char ch : value) {
    out += ch == '"' ? "\"\"" : std::string(1, ch);
  }
  out += '"';
  return out;
}

std::string quote_literal(const std::string& value) {
  std::string out = "'";
  for (const char ch : value) {
    out += ch == '\'' ? "''" : std::string(1, ch);
  }
  out += '\'';
  return out;
}

std::string readonly_file_uri(const std::string& path) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string uri = "file:";
  for (const unsigned char ch : path) {
    const bool unreserved =
        (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '/' || ch == '-' || ch == '_' ||
        ch == '.' || ch == '~';
    if (unreserved) {
      uri += static_cast<char>(ch);
    } else {
      uri += '%';
      uri += kHex[(ch >> 4) & 0xf];
      uri += kHex[ch & 0xf];
    }
  }
  uri += "?mode=ro";
  return uri;
}

void sqlite_exec(sqlite3* db, const std::string& sql,
                 const std::string& context) {
  char* error = nullptr;
  const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
  if (rc != SQLITE_OK) {
    const std::string message = error != nullptr ? error : sqlite3_errmsg(db);
    sqlite3_free(error);
    throw std::runtime_error(context + ": " + message);
  }
}

std::uint64_t sqlite_scalar_u64(sqlite3* db, const std::string& sql,
                                const std::string& context) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(context + ": " + sqlite3_errmsg(db));
  }
  const int rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    const std::string message = sqlite3_errmsg(db);
    sqlite3_finalize(stmt);
    throw std::runtime_error(context + ": " + message);
  }
  const sqlite3_int64 value = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return value < 0 ? 0 : static_cast<std::uint64_t>(value);
}

std::vector<std::string> sqlite_table_names(sqlite3* db,
                                            const std::string& schema) {
  const std::string sql =
      "SELECT name FROM " + quote_identifier(schema) +
      ".sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' "
      "ORDER BY name";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to inventory profiler tables: " +
                             std::string(sqlite3_errmsg(db)));
  }
  std::vector<std::string> names;
  while (true) {
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      const std::string message = sqlite3_errmsg(db);
      sqlite3_finalize(stmt);
      throw std::runtime_error("failed to inventory profiler tables: " +
                               message);
    }
    const unsigned char* text = sqlite3_column_text(stmt, 0);
    names.emplace_back(text == nullptr
                           ? ""
                           : reinterpret_cast<const char*>(text));
  }
  sqlite3_finalize(stmt);
  return names;
}

bool sqlite_table_has_rowid(sqlite3* db, const std::string& schema,
                            const std::string& table) {
  const std::string sql = "SELECT rowid FROM " + quote_identifier(schema) +
                          "." + quote_identifier(table) + " LIMIT 0";
  sqlite3_stmt* stmt = nullptr;
  const int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
  if (stmt != nullptr) {
    sqlite3_finalize(stmt);
  }
  return rc == SQLITE_OK;
}

std::set<std::string> sqlite_table_columns(sqlite3* db,
                                           const std::string& schema,
                                           const std::string& table) {
  const std::string sql = "PRAGMA " + quote_identifier(schema) +
                          ".table_info(" + quote_identifier(table) + ")";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to inventory profiler columns: " +
                             std::string(sqlite3_errmsg(db)));
  }
  std::set<std::string> columns;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char* text = sqlite3_column_text(stmt, 1);
    if (text != nullptr) {
      columns.emplace(reinterpret_cast<const char*>(text));
    }
  }
  sqlite3_finalize(stmt);
  return columns;
}

std::string unique_source_rowid_column(const std::set<std::string>& columns) {
  std::string candidate = "__traceloom_source_rowid__";
  while (columns.count(candidate) != 0) {
    candidate += '_';
  }
  return candidate;
}

sqlite3* open_sqlite_readwrite(const std::string& path) {
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(path.c_str(), &db,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_URI,
                      nullptr) !=
      SQLITE_OK) {
    const std::string message = db ? sqlite3_errmsg(db) : "open failed";
    if (db != nullptr) {
      sqlite3_close(db);
    }
    throw std::runtime_error("failed to open augmented DB: " + message);
  }
  return db;
}

void sqlite_snapshot(const std::string& source_path,
                     const std::string& destination_path) {
  sqlite3* source = nullptr;
  sqlite3* destination = nullptr;
  if (sqlite3_open_v2(source_path.c_str(), &source, SQLITE_OPEN_READONLY,
                      nullptr) != SQLITE_OK) {
    const std::string message = source ? sqlite3_errmsg(source) : "open failed";
    if (source != nullptr) {
      sqlite3_close(source);
    }
    throw std::runtime_error("failed to open profiler DB for snapshot: " +
                             message);
  }
  if (sqlite3_open_v2(destination_path.c_str(), &destination,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
      SQLITE_OK) {
    const std::string message =
        destination ? sqlite3_errmsg(destination) : "open failed";
    if (destination != nullptr) {
      sqlite3_close(destination);
    }
    sqlite3_close(source);
    throw std::runtime_error("failed to create augmented DB snapshot: " +
                             message);
  }
  sqlite3_backup* backup =
      sqlite3_backup_init(destination, "main", source, "main");
  if (backup == nullptr) {
    const std::string message = sqlite3_errmsg(destination);
    sqlite3_close(destination);
    sqlite3_close(source);
    throw std::runtime_error("failed to initialize augmented DB snapshot: " +
                             message);
  }
  const int step_rc = sqlite3_backup_step(backup, -1);
  const int finish_rc = sqlite3_backup_finish(backup);
  const std::string message = sqlite3_errmsg(destination);
  sqlite3_close(destination);
  sqlite3_close(source);
  if (step_rc != SQLITE_DONE || finish_rc != SQLITE_OK) {
    throw std::runtime_error("failed to copy profiler DB into augmented DB: " +
                             message);
  }
}

RawPackagingResult inventory_snapshot_source(const std::string& source_path,
                                              sqlite3* snapshot_db) {
  RawPackagingResult result;
  RawSourceDatabase source;
  source.source_id = "raw-source-000";
  source.source_path = source_path;
  source.embedded_mode = "sqlite_snapshot";
  source.size_bytes = fs::file_size(source_path);
  source.sha256 = sha256_file_hex(source_path);
  result.sources.push_back(source);
  for (const std::string& table : sqlite_table_names(snapshot_db, "main")) {
    if (table.rfind("traceloom_", 0) == 0) {
      throw std::invalid_argument(
          "input already contains TraceLoom-owned tables: " + table);
    }
    RawTableCopy copy;
    copy.source_id = source.source_id;
    copy.source_path = source_path;
    copy.source_table = table;
    copy.embedded_table_name = table;
    copy.source_rowid_column =
        sqlite_table_has_rowid(snapshot_db, "main", table) ? "rowid" : "";
    copy.row_count = sqlite_scalar_u64(
        snapshot_db, "SELECT COUNT(*) FROM " + quote_identifier(table),
        "failed to count profiler table");
    result.tables.push_back(std::move(copy));
  }
  return result;
}

RawPackagingResult copy_multiple_sqlite_sources(
    const std::vector<std::string>& source_paths,
    const std::string& destination_path) {
  sqlite3* db = open_sqlite_readwrite(destination_path);
  RawPackagingResult result;
  try {
    for (std::size_t index = 0; index < source_paths.size(); ++index) {
      const fs::path source =
          fs::absolute(source_paths[index]).lexically_normal();
      if (!fs::is_regular_file(source)) {
        throw std::invalid_argument("raw source is not a regular SQLite file: " +
                                    source.string());
      }
      std::ostringstream id;
      id << "raw-source-" << std::setw(3) << std::setfill('0') << index;
      std::ostringstream alias;
      alias << "raw_source_" << std::setw(3) << std::setfill('0') << index;
      RawSourceDatabase source_row;
      source_row.source_id = id.str();
      source_row.source_ordinal = static_cast<std::uint32_t>(index);
      source_row.source_path = source.string();
      source_row.embedded_mode = "namespaced_table_copy";
      source_row.size_bytes = fs::file_size(source);
      source_row.sha256 = sha256_file_hex(source.string());
      result.sources.push_back(source_row);

      sqlite_exec(db, "ATTACH DATABASE " +
                          quote_literal(readonly_file_uri(source.string())) +
                          " AS " + quote_identifier(alias.str()),
                  "failed to attach split profiler DB");
      try {
        for (const std::string& table : sqlite_table_names(db, alias.str())) {
          const std::string embedded =
              "traceloom_raw_" + id.str().substr(id.str().size() - 3) +
              "__" + table;
          const bool has_rowid =
              sqlite_table_has_rowid(db, alias.str(), table);
          const std::set<std::string> columns =
              sqlite_table_columns(db, alias.str(), table);
          const std::string rowid_column =
              has_rowid ? unique_source_rowid_column(columns) : std::string();
          std::string select;
          if (has_rowid) {
            select = "SELECT rowid AS " + quote_identifier(rowid_column) +
                     ", * FROM " + quote_identifier(alias.str()) + "." +
                     quote_identifier(table) + " ORDER BY rowid";
          } else {
            select = "SELECT * FROM " + quote_identifier(alias.str()) + "." +
                     quote_identifier(table);
          }
          sqlite_exec(db,
                      "CREATE TABLE " + quote_identifier(embedded) +
                          " AS " + select,
                      "failed to embed split profiler table");
          RawTableCopy table_row;
          table_row.source_id = source_row.source_id;
          table_row.source_path = source.string();
          table_row.source_table = table;
          table_row.embedded_table_name = embedded;
          table_row.source_rowid_column = rowid_column;
          table_row.row_count = sqlite_scalar_u64(
              db, "SELECT COUNT(*) FROM " + quote_identifier(embedded),
              "failed to count embedded profiler table");
          result.tables.push_back(std::move(table_row));
        }
        sqlite_exec(db, "DETACH DATABASE " + quote_identifier(alias.str()),
                    "failed to detach split profiler DB");
      } catch (...) {
        try {
          sqlite_exec(db,
                      "DETACH DATABASE " + quote_identifier(alias.str()),
                      "failed to detach split profiler DB after error");
        } catch (...) {
        }
        throw;
      }
    }
    sqlite3_close(db);
    return result;
  } catch (...) {
    sqlite3_close(db);
    throw;
  }
}

void materialize_augmented_catalog(const std::string& path,
                                   const RawPackagingResult& packaging,
                                   const NativeIr& ir,
                                   const IdleEvidencePipelineResult*
                                       idle_evidence) {
  sqlite3* db = open_sqlite_readwrite(path);
  try {
    sqlite_exec(db, "BEGIN IMMEDIATE", "failed to begin catalog transaction");
    sqlite_exec(
        db,
        "CREATE TABLE traceloom_raw_source_database("
        "source_id TEXT NOT NULL PRIMARY KEY, source_ordinal INTEGER NOT NULL, "
        "source_path TEXT NOT NULL, embedded_mode TEXT NOT NULL, "
        "size_bytes INTEGER NOT NULL, sha256 TEXT NOT NULL)",
        "failed to create raw source catalog");
    sqlite_exec(
        db,
        "CREATE TABLE traceloom_raw_table("
        "source_id TEXT NOT NULL, source_path TEXT NOT NULL, "
        "source_table TEXT NOT NULL, embedded_table_name TEXT NOT NULL, "
        "source_rowid_column TEXT, row_count INTEGER NOT NULL, "
        "PRIMARY KEY(source_id, source_table))",
        "failed to create raw table catalog");
    for (const RawSourceDatabase& source : packaging.sources) {
      sqlite_exec(
          db,
          "INSERT INTO traceloom_raw_source_database VALUES(" +
              quote_literal(source.source_id) + "," +
              std::to_string(source.source_ordinal) + "," +
              quote_literal(source.source_path) + "," +
              quote_literal(source.embedded_mode) + "," +
              std::to_string(source.size_bytes) + "," +
              quote_literal(source.sha256) + ")",
          "failed to insert raw source catalog row");
    }
    for (const RawTableCopy& table : packaging.tables) {
      sqlite_exec(
          db,
          "INSERT INTO traceloom_raw_table VALUES(" +
              quote_literal(table.source_id) + "," +
              quote_literal(table.source_path) + "," +
              quote_literal(table.source_table) + "," +
              quote_literal(table.embedded_table_name) + "," +
              (table.source_rowid_column.empty()
                   ? std::string("NULL")
                   : quote_literal(table.source_rowid_column)) +
              "," + std::to_string(table.row_count) + ")",
          "failed to insert raw table catalog row");
    }
    sqlite_exec(
        db,
        "CREATE INDEX traceloom_raw_table_source_locator_idx ON "
        "traceloom_raw_table(source_path, source_table)",
        "failed to index raw table catalog");
    sqlite_exec(
        db,
        "CREATE VIEW traceloom_v_event_source_locator AS SELECT "
        "es.event_id, es.source_ordinal, es.db_idx, es.device_id, "
        "json_extract(es.raw_json, '$.source_ref_id') AS source_ref_id, "
        "json_extract(es.raw_json, '$.source_path') AS source_path, "
        "es.source_table, es.source_key, es.source_role, "
        "rt.source_id, rt.embedded_table_name, rt.source_rowid_column, "
        "CASE WHEN rt.embedded_table_name IS NOT NULL THEN 'embedded_raw' "
        "WHEN es.source_table IN ('CUDA_GRAPH_REPLAY_UNIT', "
        "'ACLGRAPH_REPLAY_UNIT', 'SYNTHETIC') THEN 'analysis_synthetic' "
        "ELSE 'unresolved' END AS resolution_status "
        "FROM traceloom_event_source es LEFT JOIN traceloom_raw_table rt ON "
        "rt.source_path = json_extract(es.raw_json, '$.source_path') AND "
        "rt.source_table = es.source_table",
        "failed to create event source locator view");
    sqlite_exec(
        db,
        "CREATE TABLE traceloom_analysis_surface("
        "surface_name TEXT NOT NULL PRIMARY KEY, relation_name TEXT NOT NULL, "
        "row_grain TEXT NOT NULL, purpose TEXT NOT NULL, "
        "example_sql TEXT NOT NULL)",
        "failed to create analysis surface catalog");
    sqlite_exec(
        db,
        "CREATE TABLE traceloom_operator_audit("
        "operator_name TEXT NOT NULL, task_type TEXT NOT NULL, "
        "semantic_role TEXT NOT NULL, matched_rule_id TEXT NOT NULL, "
        "occurrence_count INTEGER NOT NULL, total_duration_ns INTEGER NOT NULL, "
        "graph_body_member_count INTEGER NOT NULL, "
        "PRIMARY KEY(operator_name, task_type, semantic_role, "
        "matched_rule_id))",
        "failed to create operator audit table");
    if (idle_evidence != nullptr) {
      const SemanticOperatorCoverageSummary operator_coverage =
          summarize_semantic_operator_coverage(ir,
                                               idle_evidence->classification);
      for (const UnregisteredOperatorSummaryRow& row :
           operator_coverage.unregistered_operators) {
        sqlite_exec(
            db,
            "INSERT INTO traceloom_operator_audit VALUES(" +
                quote_literal(row.operator_name) + "," +
                quote_literal(row.task_type) + "," +
                quote_literal(row.semantic_role) + "," +
                quote_literal(row.matched_rule_id) +
                "," + std::to_string(row.occurrence_count) + "," +
                std::to_string(row.total_duration_ns) + "," +
                std::to_string(row.graph_body_member_count) + ")",
            "failed to insert operator audit row");
      }
      sqlite_exec(
          db,
          "INSERT INTO traceloom_metadata(key, value) VALUES"
          "('operator_audit_status', 'available'),"
          "('unknown_task_count', " +
              quote_literal(
                  std::to_string(operator_coverage.unknown_task_count)) +
              "),('unregistered_operator_occurrence_count', " +
              quote_literal(std::to_string(
                  operator_coverage.unregistered_operator_occurrence_count)) +
              ")",
          "failed to add operator audit metadata");
    } else {
      sqlite_exec(
          db,
          "INSERT INTO traceloom_metadata(key, value) VALUES"
          "('operator_audit_status', 'provider_taxonomy_not_enabled')",
          "failed to add unavailable operator audit metadata");
    }
    const std::vector<std::vector<std::string>> surfaces = {
        {"tree_map", "traceloom_v_tree_node", "one structural node",
         "read the hierarchical cost map from coarse loops to leaves",
         "SELECT * FROM traceloom_v_tree_node ORDER BY db_idx, device_id, "
         "view_name, display_order;"},
        {"tree_occurrence", "traceloom_tree_node_occurrence",
         "one structural node occurrence",
         "compare repeated instances without losing hierarchy",
         "SELECT * FROM traceloom_tree_node_occurrence ORDER BY db_idx, "
         "device_id, view_name, node_id, occurrence_idx;"},
        {"node_cost", "traceloom_v_node_cost", "one structural node cost",
         "rank and compare overlap-safe cost lenses by structural node",
         "SELECT * FROM traceloom_v_node_cost ORDER BY total_us DESC, "
         "db_idx, device_id, view_name, node_id;"},
        {"normalized_event", "traceloom_event", "one normalized event",
         "inspect fine-grained timing and operator evidence",
         "SELECT * FROM traceloom_event ORDER BY db_idx, device_id, step_idx;"},
        {"event_source", "traceloom_v_event_source_locator",
         "one event-to-raw-source link",
         "resolve normalized evidence to the embedded profiler table",
         "SELECT * FROM traceloom_v_event_source_locator ORDER BY db_idx, "
         "device_id, event_id, source_ordinal;"},
        {"exact_graph_member", "traceloom_v_node_graph_body_member",
         "one exact graph body member in one tree occurrence",
         "drill through replay structure to exact member cost and provenance",
         "SELECT * FROM traceloom_v_node_graph_body_member ORDER BY db_idx, "
         "device_id, node_id, occurrence_idx, node_member_order, "
         "lane_ordinal, task_ordinal;"},
        {"replay_hotspot", "traceloom_replay_cost_aggregate",
         "one role-collapsed replay member distribution",
         "rank stable replay-internal cost distributions",
         "SELECT * FROM traceloom_replay_cost_aggregate ORDER BY "
         "duration_median_ns DESC, aggregate_id;"},
        {"analysis_issue", "traceloom_replay_cost_issue",
         "one typed replay analysis issue",
         "audit unsupported or unrecognized replay analysis",
         "SELECT * FROM traceloom_replay_cost_issue ORDER BY db_idx, "
         "device_id, replay_unit_id, issue_id;"},
        {"reconstruction_status",
         "traceloom_aclgraph_reconstruction_region",
         "one graph reconstruction region",
         "audit recognized and unrecognized graph reconstruction evidence",
         "SELECT * FROM traceloom_aclgraph_reconstruction_region ORDER BY "
         "db_idx, device_id, region_order;"},
        {"idle_evidence", "traceloom_idle_explanation",
         "one profiler-visible productive-gap explanation",
         "inspect typed visible-gap evidence without claiming causality",
         "SELECT * FROM traceloom_idle_explanation ORDER BY device_id, "
         "start_ns, end_ns, idle_explanation_id;"},
        {"operator_audit", "traceloom_operator_audit",
         "one unregistered operator identity",
         "surface concrete operator identities lacking exact registration",
         "SELECT * FROM traceloom_operator_audit ORDER BY "
         "graph_body_member_count DESC, occurrence_count DESC, "
         "operator_name;"},
        {"raw_table", "traceloom_raw_table", "one embedded profiler table",
         "discover collision-free raw evidence storage",
         "SELECT * FROM traceloom_raw_table ORDER BY source_id, source_table;"},
    };
    for (const auto& surface : surfaces) {
      sqlite_exec(db,
                  "INSERT INTO traceloom_analysis_surface VALUES(" +
                      quote_literal(surface[0]) + "," +
                      quote_literal(surface[1]) + "," +
                      quote_literal(surface[2]) + "," +
                      quote_literal(surface[3]) + "," +
                      quote_literal(surface[4]) + ")",
                  "failed to insert analysis surface row");
    }
    sqlite_exec(db,
                "INSERT INTO traceloom_metadata(key, value) VALUES"
                "('raw_source_database_count', " +
                    quote_literal(std::to_string(packaging.sources.size())) +
                    "),('raw_table_count', " +
                    quote_literal(std::to_string(packaging.tables.size())) +
                    ")",
                "failed to add augmented catalog metadata");
    sqlite_exec(db, "COMMIT", "failed to commit catalog transaction");
    sqlite3_close(db);
  } catch (...) {
    try {
      sqlite_exec(db, "ROLLBACK", "failed to roll back catalog transaction");
    } catch (...) {
    }
    sqlite3_close(db);
    throw;
  }
}
#endif

ReportTree build_sidecar_report_tree(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options,
    const std::vector<ReportToken>& report_tokens) {
  if (!options.materialize_grammar_report_tree || report_tokens.empty()) {
    return build_report_tree_from_tokens(report_tokens);
  }

  try {
    GrammarStateConfig grammar_state_config;
    grammar_state_config.target_nodes_per_chunk =
        options.grammar_target_nodes_per_chunk;
    grammar_state_config.worker_count = options.grammar_worker_count;
    grammar_state_config.full_discovery_cap =
        options.grammar_full_discovery_cap;

    GlobalGrammarState grammar_state =
        build_initial_grammar_state(ir, grammar_state_config);
    GrammarEngineConfig grammar_engine_config;
    grammar_engine_config.full_discovery_cap =
        grammar_state.metadata.full_discovery_cap;
    const GrammarEngineResult grammar_result =
        run_grammar_state_machine(grammar_state, grammar_engine_config);
    if (options.timing_diagnostics) {
      std::cerr << "timing loop_tree_grammar_stop_reason="
                << grammar_engine_stop_reason_name(grammar_result.stop_reason)
                << "\n";
      std::cerr << "timing loop_tree_grammar_steps="
                << grammar_result.steps.size() << "\n";
      std::cerr << "timing loop_tree_grammar_live_nodes="
                << grammar_state.live_node_count << "\n";
      std::cerr << "timing loop_tree_grammar_macro_defs="
                << grammar_state.macro_defs.size() << "\n";
      if (!grammar_result.steps.empty()) {
        const GrammarEngineStep& last_step = grammar_result.steps.back();
        std::cerr << "timing loop_tree_grammar_last_before_nodes="
                  << last_step.before_live_node_count << "\n";
        std::cerr << "timing loop_tree_grammar_last_after_nodes="
                  << last_step.after_live_node_count << "\n";
        std::cerr << "timing loop_tree_grammar_last_gain="
                  << last_step.gain << "\n";
        std::cerr << "timing loop_tree_grammar_last_replace_count="
                  << last_step.replace_count << "\n";
      }
    }
    if (!grammar_result.ok() || grammar_state.stage != GrammarStage::kDone ||
        grammar_state.macro_defs.empty()) {
      return build_report_tree_from_tokens(report_tokens);
    }
    return build_report_tree_from_grammar_state(report_tokens, grammar_state);
  } catch (const std::exception&) {
    return build_report_tree_from_tokens(report_tokens);
  }
}

}  // namespace

NodeCoverageSqlRows build_native_loop_tree_node_coverage_rows(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
  const Stopwatch tokens_watch;
  const std::vector<ReportToken> report_tokens =
      build_report_tokens_from_native_ir(ir);
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_tokens_ms=" << tokens_watch.elapsed_ms()
              << "\n";
  }
  const Stopwatch aux_watch;
  const AuxAttributionSqlRows aux_rows =
      options.materialize_aux_attribution
          ? build_aux_attribution_sql_rows(ir, options.db_idx)
          : AuxAttributionSqlRows{};
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_aux_rows_ms=" << aux_watch.elapsed_ms()
              << "\n";
  }
  const Stopwatch tree_watch;
  const ReportTree report_tree =
      build_sidecar_report_tree(ir, options, report_tokens);
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_report_tree_ms="
              << tree_watch.elapsed_ms() << "\n";
  }
  const Stopwatch coverage_watch;
  NodeCoverageSqlRows rows = build_report_tree_node_coverage_sql_rows(
      report_tree, report_tokens, aux_rows, options.db_idx);
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_coverage_rows_ms="
              << coverage_watch.elapsed_ms() << "\n";
  }
  return rows;
}

void write_basic_native_compatibility_sidecar(
    const std::string& sqlite_path,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options,
    const IdleEvidencePipelineResult* idle_evidence) {
  std::vector<MetadataSqlRow> metadata{
      {"traceloom_schema_version", "augmented_db_v1"},
      {"native_compatibility_materializer", "basic_native_ir_v1"},
      {"source_kind", options.source_kind},
      {"source_path", options.source_path},
      {"artifact_kind", options.artifact_kind},
      {"source_embedded", options.source_embedded ? "true" : "false"},
      {"source_sha256", options.source_sha256},
      {"source_size_bytes", std::to_string(options.source_size_bytes)},
      {"trace_event_count", std::to_string(ir.trace_events.size())},
      {"anchor_count", std::to_string(ir.anchors.size())},
      {"graph_template_count", std::to_string(ir.graph_templates.size())},
      {"replay_unit_count", std::to_string(ir.replay_units.size())},
      {"replay_composition_region_count",
       std::to_string(ir.replay_composition_regions.size())},
      {"unrecognized_replay_composition_region_count",
       std::to_string(std::count_if(
           ir.replay_composition_regions.rows().begin(),
           ir.replay_composition_regions.rows().end(),
           [](const ReplayCompositionRegionRow& region) {
             return region.status != ReplayCompositionRegionStatus::
                                         kRecognizedCompletePattern;
           }))},
  };

  replace_metadata_rows(sqlite_path, metadata);
  const EventSqlRows event_rows = build_timeline_sql_rows(ir, options.db_idx);
  replace_timeline_rows(sqlite_path,
                        split_timeline_event_sql_rows(event_rows));
  replace_event_source_rows(sqlite_path,
                            split_source_lineage_sql_rows(event_rows));
  replace_graph_replay_evidence_rows(
      sqlite_path,
      build_native_graph_replay_evidence_sql_rows(
          ir, options.source_kind, options.db_idx));
  replace_exact_graph_rows(
      sqlite_path,
      build_exact_graph_sql_rows(ir, options.source_kind, options.db_idx));
  replace_replay_cost_rows(sqlite_path, ir, options.db_idx);
  const std::vector<AnchorSqlRow> anchor_rows =
      build_anchor_sequence_sql_rows(ir, options.db_idx);
  replace_anchor_rows(sqlite_path, anchor_rows);
  const AuxAttributionSqlRows aux_rows =
      options.materialize_aux_attribution
          ? build_aux_attribution_sql_rows(ir, options.db_idx)
          : AuxAttributionSqlRows{};
  replace_aux_attribution_rows(sqlite_path, aux_rows);
  replace_anchor_cost_breakdown_rows(
      sqlite_path, build_native_anchor_cost_breakdown_sql_rows(ir, aux_rows));
  const std::vector<ReportToken> report_tokens =
      build_report_tokens_from_native_ir(ir);
  const ReportTree report_tree =
      build_sidecar_report_tree(ir, options, report_tokens);
  const NodeCoverageSqlRows node_rows =
      build_report_tree_node_coverage_sql_rows(report_tree, report_tokens,
                                               aux_rows, options.db_idx);
  replace_loop_tree_rows(sqlite_path, split_loop_tree_sql_rows(node_rows));
  const NodeAnchorCoverageSqlRows coverage_rows =
      split_node_anchor_coverage_sql_rows(node_rows);
  replace_node_anchor_coverage_rows(sqlite_path, coverage_rows);
  if (idle_evidence != nullptr) {
    const IdleExplanationAttributionRows attribution =
        build_idle_explanation_attribution_rows(
            report_tokens, idle_evidence->idle_explanations, node_rows,
            options.db_idx);
    IdleEvidenceSqlRowOptions idle_options;
    idle_options.db_idx = options.db_idx;
    idle_options.source_kind = options.source_kind;
    idle_options.source_path = options.source_path;
    replace_idle_evidence_rows(
        sqlite_path,
        build_idle_evidence_sql_rows(ir, *idle_evidence, attribution,
                                     idle_options));
  } else {
    // Replacement semantics matter when an existing sidecar is regenerated
    // for a provider whose idle taxonomy is not enabled.
    replace_idle_evidence_rows(sqlite_path, IdleEvidenceSqlRows{});
  }
  if (options.materialize_collective_tags) {
    CollectiveTagMemberInput member;
    member.db_name = options.collective_db_name.empty()
                         ? basename_or_default(sqlite_path, "native_sidecar.db")
                         : options.collective_db_name;
    member.db_idx = options.db_idx;
    member.events = split_timeline_event_sql_rows(event_rows);
    member.anchors = anchor_rows;
    member.loop_tree = split_loop_tree_sql_rows(node_rows);
    member.node_anchor_coverage = coverage_rows;

    CollectiveTagOptions tag_options;
    tag_options.run_name =
        options.collective_run_name.empty()
            ? basename_or_default(options.source_path, "traceloom_run")
            : options.collective_run_name;
    tag_options.expected_world_size = options.collective_expected_world_size;
    const CollectiveTagSqlRows collective_rows =
        build_collective_tag_sql_rows({member}, tag_options);
    replace_collective_global_link_rows(sqlite_path,
                                        collective_rows.local_links);
  }

  const SemanticTreeSqlRows semantic_rows = build_report_tree_semantic_sql_rows(
      report_tree, report_tokens, aux_rows, options.db_idx);
  replace_semantic_tree_catalog_rows(
      sqlite_path, split_semantic_tree_catalog_sql_rows(semantic_rows));
  replace_semantic_graph_rows(sqlite_path,
                              split_semantic_graph_sql_rows(semantic_rows));
  if (options.materialize_report_views) {
    materialize_report_compatibility_views(sqlite_path);
  }
}

void write_queryable_database_timeline(
    const std::string& output_path,
    const std::string& source_sqlite_path,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options,
    const IdleEvidencePipelineResult* idle_evidence) {
  write_queryable_database_timeline(
      output_path, std::vector<std::string>{source_sqlite_path}, ir, options,
      idle_evidence);
}

void write_queryable_database_timeline(
    const std::string& output_path,
    const std::vector<std::string>& source_sqlite_paths,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options,
    const IdleEvidencePipelineResult* idle_evidence) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  if (source_sqlite_paths.empty()) {
    throw std::invalid_argument(
        "self-contained augmented DB requires at least one SQLite source");
  }
  std::vector<std::string> sources;
  sources.reserve(source_sqlite_paths.size());
  for (const std::string& source_path : source_sqlite_paths) {
    const fs::path source = fs::absolute(source_path).lexically_normal();
    if (!fs::is_regular_file(source)) {
      throw std::invalid_argument(
          "self-contained augmented DB source is not a regular SQLite file: " +
          source.string());
    }
    sources.push_back(source.string());
  }
  std::sort(sources.begin(), sources.end());
  if (std::adjacent_find(sources.begin(), sources.end()) != sources.end()) {
    throw std::invalid_argument(
        "self-contained augmented DB received duplicate SQLite sources");
  }
  const fs::path output = fs::absolute(output_path).lexically_normal();
  for (const std::string& source_path : sources) {
    const fs::path source(source_path);
    if (source == output ||
        (fs::exists(output) && fs::equivalent(source, output))) {
      throw std::invalid_argument(
          "augmented DB output must differ from every input profiler DB");
    }
  }
  if (output.has_parent_path()) {
    fs::create_directories(output.parent_path());
  }
  const std::string suffix = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const fs::path temporary = output.string() + ".tmp." + suffix;
  try {
    RawPackagingResult packaging;
    if (sources.size() == 1) {
      sqlite_snapshot(sources.front(), temporary.string());
      sqlite3* snapshot_db = open_sqlite_readwrite(temporary.string());
      try {
        packaging = inventory_snapshot_source(sources.front(), snapshot_db);
        sqlite3_close(snapshot_db);
      } catch (...) {
        sqlite3_close(snapshot_db);
        throw;
      }
    } else {
      packaging = copy_multiple_sqlite_sources(sources, temporary.string());
    }
    NativeCompatibilitySidecarOptions augmented_options = options;
    if (augmented_options.source_path.empty()) {
      augmented_options.source_path =
          sources.size() == 1 ? sources.front() : "multiple_sqlite_sources";
    }
    if (augmented_options.collective_db_name.empty()) {
      augmented_options.collective_db_name =
          basename_or_default(augmented_options.source_path, "analysis") +
          ".traceloom.db";
    }
    augmented_options.artifact_kind = "queryable_database_timeline";
    augmented_options.source_embedded = true;
    augmented_options.source_size_bytes = 0;
    for (const RawSourceDatabase& source : packaging.sources) {
      augmented_options.source_size_bytes += source.size_bytes;
    }
    augmented_options.source_sha256 =
        packaging.sources.size() == 1 ? packaging.sources.front().sha256
                                      : std::string();
    write_basic_native_compatibility_sidecar(
        temporary.string(), ir, augmented_options, idle_evidence);
    materialize_augmented_catalog(temporary.string(), packaging, ir,
                                  idle_evidence);
    std::error_code ec;
    fs::rename(temporary, output, ec);
    if (ec) {
      throw std::runtime_error("failed to publish augmented DB output: " +
                               ec.message());
    }
  } catch (...) {
    std::error_code ignored;
    fs::remove(temporary, ignored);
    throw;
  }
#else
  (void)output_path;
  (void)source_sqlite_paths;
  (void)ir;
  (void)options;
  (void)idle_evidence;
  throw std::runtime_error(
      "self-contained augmented DB requires SQLite support");
#endif
}

}  // namespace traceloom::compat
