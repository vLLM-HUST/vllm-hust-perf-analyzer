#include "traceloom/compat/native_sidecar_materializer.h"

#include "augmented_catalog_materializer.h"
#include "sidecar_sqlite_utils.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "traceloom/compat/anchor_cost_breakdown_rows.h"
#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/aux_attribution_rows.h"
#include "traceloom/compat/collective_tag_rows.h"
#include "traceloom/compat/evidence_role_sql_rows.h"
#include "traceloom/compat/event_reconciliation_rows.h"
#include "traceloom/compat/exact_graph_sql_rows.h"
#include "traceloom/compat/native_graph_replay_rows.h"
#include "traceloom/compat/report_tree_rows.h"
#include "traceloom/compat/replay_cost_sql_rows.h"
#include "traceloom/compat/runtime_device_rows.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/compat/timeline_rows.h"
#include "traceloom/compat/symbol_normalization_rows.h"
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

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
using detail::RawPackagingResult;
using detail::RawSourceDatabase;
using detail::RawTableCopy;
using detail::materialize_augmented_catalog;
using detail::open_sqlite_readwrite;
using detail::quote_identifier;
using detail::quote_literal;
using detail::sqlite_exec;
using detail::sqlite_scalar_u64;
#endif

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
    if (!grammar_result.ok() || grammar_state.stage != GrammarStage::kDone) {
      ReportTree fallback = build_report_tree_from_tokens(report_tokens);
      fallback.diagnostics.push_back(Diagnostic{
          DiagnosticSeverity::kWarning, "grammar_recovery_rejected",
          "recursive grammar recovery failed closed with stop reason " +
              std::string(
                  grammar_engine_stop_reason_name(grammar_result.stop_reason))});
      return fallback;
    }
    ReportTree tree = grammar_state.macro_defs.empty()
                          ? build_report_tree_from_tokens(report_tokens)
                          : build_report_tree_from_grammar_state(
                                report_tokens, grammar_state);
    if (grammar_result.stop_reason ==
        GrammarEngineStopReason::kSequenceTooLargeForFullPairDiscovery) {
      tree.diagnostics.push_back(Diagnostic{
          DiagnosticSeverity::kWarning,
          "grammar_partial_sequence_too_large_for_full_pair_discovery",
          "exact run folding was retained, but pair discovery was skipped "
          "because the live sequence exceeded full_discovery_cap"});
    }
    return tree;
  } catch (const std::exception& ex) {
    ReportTree fallback = build_report_tree_from_tokens(report_tokens);
    fallback.diagnostics.push_back(Diagnostic{
        DiagnosticSeverity::kWarning, "grammar_recovery_exception",
        std::string("recursive grammar recovery failed closed: ") + ex.what()});
    return fallback;
  }
}

// Projects the report-relevant IR tables onto a single device. The token
// table is filtered to the device with dense TokenIds and a renumbered
// sequence_index (the grammar state machine requires both). Anchor, event,
// task, communication-op, symbol, source-ref, and semantic replay tables are
// copied unchanged so original ids stay valid everywhere; per-device tokens
// reference original anchor ids, which keeps compat anchor ids and anchor
// indices consistent with the global sidecar tables. Protected intervals are
// kept only when their whole token span belongs to the device; a span that
// crosses devices fails closed because cross-device replay units are not
// supported by the structural report.
NativeIr project_ir_for_device(const NativeIr& ir, std::uint32_t device_id) {
  NativeIr out;
  out.symbols = ir.symbols;
  out.source_refs = ir.source_refs;
  out.trace_events = ir.trace_events;
  out.tasks = ir.tasks;
  out.communication_ops = ir.communication_ops;
  out.anchors = ir.anchors;
  out.graph_templates = ir.graph_templates;
  out.replay_composition_candidates = ir.replay_composition_candidates;
  out.replay_composition_regions = ir.replay_composition_regions;
  out.replay_units = ir.replay_units;

  std::vector<std::uint32_t> token_devices(ir.tokens.size(), 0);
  for (std::size_t index = 0; index < ir.tokens.size(); ++index) {
    token_devices[index] = ir.tokens.rows()[index].device_id;
  }
  std::unordered_map<TokenId::value_type, TokenId::value_type> token_remap;
  token_remap.reserve(ir.tokens.size());
  for (const TokenRow& token : ir.tokens.rows()) {
    if (token.anchor_id.value() >= ir.anchors.size()) {
      throw std::invalid_argument("TokenRow anchor_id is out of range");
    }
    const AnchorRow& anchor = ir.anchors.row(token.anchor_id);
    if (anchor.device_id != token.device_id) {
      throw std::invalid_argument(
          "TokenRow device_id disagrees with its AnchorRow device_id");
    }
    if (anchor.device_id != device_id) {
      continue;
    }
    token_remap.emplace(token.id.value(), out.tokens.size());
    out.tokens.append(token.anchor_id, token.symbol_id, device_id,
                      static_cast<std::uint32_t>(out.tokens.size()),
                      token.start_ns, token.end_ns);
  }
  if (out.tokens.empty()) {
    throw std::invalid_argument(
        "device " + std::to_string(device_id) +
        " has no report tokens to project");
  }

  for (const ProtectedIntervalRow& interval : ir.protected_intervals.rows()) {
    if (interval.first_token_id.value() > interval.last_token_id.value()) {
      throw std::invalid_argument(
          "protected interval has an inverted token span");
    }
    if (interval.last_token_id.value() >= token_devices.size()) {
      throw std::invalid_argument(
          "protected interval references an out-of-range token");
    }
    const std::uint32_t interval_device =
        token_devices[interval.first_token_id.value()];
    if (interval_device !=
        token_devices[interval.last_token_id.value()]) {
      throw std::invalid_argument(
          "protected interval spans devices; cross-device replay units are "
          "unsupported in the structural report");
    }
    if (interval_device != device_id) {
      continue;
    }
    for (TokenId::value_type token_id = interval.first_token_id.value();
         token_id <= interval.last_token_id.value(); ++token_id) {
      if (token_devices[token_id] != interval_device) {
        throw std::invalid_argument(
            "protected interval spans devices; cross-device replay units are "
            "unsupported in the structural report");
      }
    }
    const auto first_found = token_remap.find(interval.first_token_id.value());
    const auto last_found = token_remap.find(interval.last_token_id.value());
    if (first_found == token_remap.end() || last_found == token_remap.end()) {
      throw std::invalid_argument(
          "protected interval token span is not present in its device "
          "projection");
    }
    out.protected_intervals.append(
        interval.kind, interval.boundary_policy, TokenId(first_found->second),
        TokenId(last_found->second), interval.first_anchor_id,
        interval.last_anchor_id, interval.evidence_source_ref_id);
  }
  return out;
}

}  // namespace

std::vector<NativeDeviceReportTree> build_native_device_report_trees(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
  const Stopwatch tokens_watch;
  const std::vector<NativeReportDevicePartition> partitions =
      partition_report_tokens_by_device(ir, options.evidence_role_config);
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_tokens_ms=" << tokens_watch.elapsed_ms()
              << "\n";
    std::cerr << "timing loop_tree_device_count=" << partitions.size()
              << "\n";
  }
  std::vector<NativeDeviceReportTree> device_trees;
  device_trees.reserve(partitions.size());
  for (const NativeReportDevicePartition& partition : partitions) {
    const Stopwatch tree_watch;
    NativeDeviceReportTree device;
    device.device_id = partition.device_id;
    device.tokens = partition.tokens;
    if (partitions.size() == 1) {
      device.tree = build_sidecar_report_tree(ir, options, partition.tokens);
    } else {
      const NativeIr projection =
          project_ir_for_device(ir, partition.device_id);
      device.tree =
          build_sidecar_report_tree(projection, options, partition.tokens);
    }
    if (options.timing_diagnostics) {
      std::cerr << "timing loop_tree_report_tree_ms_device="
                << partition.device_id << " " << tree_watch.elapsed_ms()
                << "\n";
    }
    device_trees.push_back(std::move(device));
  }
  return device_trees;
}

NodeCoverageSqlRows build_native_loop_tree_node_coverage_rows(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
  const std::vector<NativeDeviceReportTree> device_trees =
      build_native_device_report_trees(ir, options);
  const Stopwatch aux_watch;
  const AuxAttributionSqlRows aux_rows =
      options.materialize_aux_attribution
          ? build_aux_attribution_sql_rows(
                ir, options.evidence_role_config, options.db_idx)
          : AuxAttributionSqlRows{};
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_aux_rows_ms=" << aux_watch.elapsed_ms()
              << "\n";
  }
  const Stopwatch coverage_watch;
  const bool scope_node_ids = device_trees.size() > 1;
  NodeCoverageSqlRows rows;
  for (const NativeDeviceReportTree& device : device_trees) {
    NodeCoverageSqlRows device_rows = build_report_tree_node_coverage_sql_rows(
        device.tree, device.tokens, aux_rows, options.db_idx,
        "native_report_tree", scope_node_ids);
    rows.nodes.insert(rows.nodes.end(), device_rows.nodes.begin(),
                      device_rows.nodes.end());
    rows.edges.insert(rows.edges.end(), device_rows.edges.begin(),
                      device_rows.edges.end());
    rows.loop_nodes.insert(rows.loop_nodes.end(),
                           device_rows.loop_nodes.begin(),
                           device_rows.loop_nodes.end());
    rows.node_anchors.insert(rows.node_anchors.end(),
                             device_rows.node_anchors.begin(),
                             device_rows.node_anchors.end());
    rows.anchor_primary_nodes.insert(
        rows.anchor_primary_nodes.end(),
        device_rows.anchor_primary_nodes.begin(),
        device_rows.anchor_primary_nodes.end());
  }
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_coverage_rows_ms="
              << coverage_watch.elapsed_ms() << "\n";
  }
  return rows;
}

void write_basic_native_compatibility_sidecar(
    const std::string& sqlite_path,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
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
      {"runtime_call_count", std::to_string(ir.runtime_calls.size())},
      {"anchor_count", std::to_string(ir.anchors.size())},
      {"graph_template_count", std::to_string(ir.graph_templates.size())},
      {"replay_unit_count", std::to_string(ir.replay_units.size())},
      {"event_reconciliation_policy_id",
       ir.event_reconciliation.policy.policy_id},
      {"event_reconciliation_policy_version",
       ir.event_reconciliation.policy.policy_version},
      {"event_reconciliation_manifest_sha256",
       ir.event_reconciliation.policy.manifest_sha256},
      {"event_reconciliation_rule_count",
       std::to_string(ir.event_reconciliation.policy.rules.size())},
      {"event_reconciliation_decision_count",
       std::to_string(ir.event_reconciliation.decisions.size())},
      {"event_reconciliation_member_count",
       std::to_string(ir.event_reconciliation.members.size())},
      {"event_reconciliation_reconciled_count",
       std::to_string(std::count_if(
           ir.event_reconciliation.decisions.begin(),
           ir.event_reconciliation.decisions.end(),
           [](const EventReconciliationDecisionRow& decision) {
             return decision.status ==
                    EventReconciliationStatus::kReconciled;
           }))},
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
  if (!options.evidence_role_policy_id.empty()) {
    metadata.push_back(
        {"evidence_role_policy_id", options.evidence_role_policy_id});
    metadata.push_back({"evidence_role_policy_version",
                        options.evidence_role_policy_version});
    metadata.push_back({"evidence_role_manifest_sha256",
                        options.evidence_role_manifest_sha256});
  }
  const SymbolNormalizationSqlRows symbol_normalization_rows =
      build_symbol_normalization_sql_rows(ir, options.db_idx);
  metadata.push_back(
      {"symbol_normalization_policy_id",
       symbol_normalization_rows.policies.front().policy_id});
  metadata.push_back(
      {"symbol_normalization_policy_version",
       symbol_normalization_rows.policies.front().policy_version});
  metadata.push_back(
      {"symbol_normalization_source_manifest",
       symbol_normalization_rows.policies.front().source_manifest});
  metadata.push_back(
      {"symbol_normalization_manifest_sha256",
       symbol_normalization_rows.policies.front().manifest_sha256});
  metadata.push_back(
      {"symbol_normalization_rule_count",
       std::to_string(symbol_normalization_rows.rules.size())});
  metadata.push_back(
      {"symbol_normalization_decision_count",
       std::to_string(symbol_normalization_rows.decisions.size())});

  {
    const Stopwatch runtime_rows_watch;
    RuntimeDeviceSqlRows runtime_rows =
        build_runtime_device_sql_rows(ir, options.db_idx);
    if (options.timing_diagnostics) {
      std::cerr << "timing runtime_device_rows_ms="
                << runtime_rows_watch.elapsed_ms() << "\n";
    }
    metadata.push_back({"device_work_count",
                        std::to_string(runtime_rows.device_works.size())});
    metadata.push_back({"runtime_device_relation_count",
                        std::to_string(runtime_rows.relations.size())});
    metadata.push_back({"anchor_runtime_relation_count",
                        std::to_string(runtime_rows.anchor_relations.size())});
    metadata.push_back({"anchor_host_interval_count",
                        std::to_string(runtime_rows.host_intervals.size())});
    metadata.push_back({"anchor_host_activity_count",
                        std::to_string(runtime_rows.host_activities.size())});
    metadata.push_back({"anchor_host_api_summary_count",
                        std::to_string(runtime_rows.host_api_summaries.size())});
    const Stopwatch runtime_write_watch;
    replace_metadata_rows(sqlite_path, metadata);
    replace_runtime_device_rows(sqlite_path, runtime_rows);
    if (options.timing_diagnostics) {
      std::cerr << "timing runtime_device_write_ms="
                << runtime_write_watch.elapsed_ms() << "\n";
    }
  }
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
  replace_event_reconciliation_rows(sqlite_path, ir, options.db_idx);
  replace_symbol_normalization_rows(sqlite_path, symbol_normalization_rows);
  const AuxAttributionSqlRows aux_rows =
      options.materialize_aux_attribution
          ? build_aux_attribution_sql_rows(
                ir, options.evidence_role_config, options.db_idx)
          : AuxAttributionSqlRows{};
  replace_aux_attribution_rows(sqlite_path, aux_rows);
  replace_anchor_cost_breakdown_rows(
      sqlite_path, build_native_anchor_cost_breakdown_sql_rows(ir, aux_rows));
  const std::vector<NativeDeviceReportTree> device_trees =
      build_native_device_report_trees(ir, options);
  const bool scope_node_ids = device_trees.size() > 1;
  NodeCoverageSqlRows node_rows;
  SemanticTreeSqlRows semantic_rows;
  for (const NativeDeviceReportTree& device : device_trees) {
    const NodeCoverageSqlRows device_node_rows =
        build_report_tree_node_coverage_sql_rows(
            device.tree, device.tokens, aux_rows, options.db_idx,
            "native_report_tree", scope_node_ids);
    node_rows.nodes.insert(node_rows.nodes.end(),
                           device_node_rows.nodes.begin(),
                           device_node_rows.nodes.end());
    node_rows.edges.insert(node_rows.edges.end(),
                           device_node_rows.edges.begin(),
                           device_node_rows.edges.end());
    node_rows.loop_nodes.insert(node_rows.loop_nodes.end(),
                                device_node_rows.loop_nodes.begin(),
                                device_node_rows.loop_nodes.end());
    node_rows.node_anchors.insert(node_rows.node_anchors.end(),
                                  device_node_rows.node_anchors.begin(),
                                  device_node_rows.node_anchors.end());
    node_rows.anchor_primary_nodes.insert(
        node_rows.anchor_primary_nodes.end(),
        device_node_rows.anchor_primary_nodes.begin(),
        device_node_rows.anchor_primary_nodes.end());

    const std::string tree_id =
        device_trees.size() == 1
            ? "native-report-tree"
            : "native-report-tree-d" + std::to_string(device.device_id);
    const SemanticTreeSqlRows device_semantic_rows =
        build_report_tree_semantic_sql_rows(
            device.tree, device.tokens, aux_rows, options.db_idx, tree_id,
            "anchor_tree", scope_node_ids);
    semantic_rows.trees.insert(semantic_rows.trees.end(),
                               device_semantic_rows.trees.begin(),
                               device_semantic_rows.trees.end());
    semantic_rows.nodes.insert(semantic_rows.nodes.end(),
                               device_semantic_rows.nodes.begin(),
                               device_semantic_rows.nodes.end());
    semantic_rows.edges.insert(semantic_rows.edges.end(),
                               device_semantic_rows.edges.begin(),
                               device_semantic_rows.edges.end());
  }
  replace_loop_tree_rows(sqlite_path, split_loop_tree_sql_rows(node_rows));
  const NodeAnchorCoverageSqlRows coverage_rows =
      split_node_anchor_coverage_sql_rows(node_rows);
  replace_node_anchor_coverage_rows(sqlite_path, coverage_rows);
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

  replace_semantic_tree_catalog_rows(
      sqlite_path, split_semantic_tree_catalog_sql_rows(semantic_rows));
  replace_semantic_graph_rows(sqlite_path,
                              split_semantic_graph_sql_rows(semantic_rows));
  if (options.materialize_report_views) {
    const Stopwatch report_views_watch;
    materialize_report_compatibility_views(sqlite_path);
    if (options.timing_diagnostics) {
      std::cerr << "timing report_views_ms="
                << report_views_watch.elapsed_ms() << "\n";
    }
  }
  replace_evidence_role_sql_rows(sqlite_path, ir, options.evidence_role_config,
                                 options.db_idx,
                                 options.materialize_aux_attribution);
}

void write_queryable_database_timeline(
    const std::string& output_path,
    const std::string& source_sqlite_path,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
  write_queryable_database_timeline(
      output_path, std::vector<std::string>{source_sqlite_path}, ir, options);
}

void write_queryable_database_timeline(
    const std::string& output_path,
    const std::vector<std::string>& source_sqlite_paths,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
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
        temporary.string(), ir, augmented_options);
    materialize_augmented_catalog(temporary.string(), packaging, ir);
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
  throw std::runtime_error(
      "self-contained augmented DB requires SQLite support");
#endif
}

}  // namespace traceloom::compat
