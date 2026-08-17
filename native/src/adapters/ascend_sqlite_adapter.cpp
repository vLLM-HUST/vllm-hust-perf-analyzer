#include "traceloom/adapters/ascend_sqlite_adapter.h"

#include "ascend_sqlite_internal.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace traceloom {
namespace {
class Stopwatch {
 public:
  Stopwatch() : start_(Clock::now()) {}

  double elapsed_ms() const {
    const auto elapsed = Clock::now() - start_;
    return std::chrono::duration<double, std::milli>(elapsed).count();
  }

 private:
  using Clock = std::chrono::steady_clock;
  Clock::time_point start_;
};

template <typename Fn>
double time_stage(Fn&& fn) {
  const Stopwatch stopwatch;
  fn();
  return stopwatch.elapsed_ms();
}

struct AscendLoadTiming {
  double sqlite_open_ms = 0.0;
  double inventory_ms = 0.0;
  double string_ids_ms = 0.0;
  double runtime_calls_ms = 0.0;
  double compute_info_ms = 0.0;
  double cann_api_metadata_ms = 0.0;
  double communication_task_info_ms = 0.0;
  double task_rows_ms = 0.0;
  double communication_op_rows_ms = 0.0;
  double stream_info_capture_ms = 0.0;
  double cann_api_capture_slots_ms = 0.0;
  double aclgraph_capture_instances_ms = 0.0;
  double aclgraph_launch_occurrences_ms = 0.0;
  double aclgraph_launch_bodies_ms = 0.0;
  double aclgraph_launch_activities_ms = 0.0;
  double replay_composition_candidates_ms = 0.0;
  double aclgraph_replay_units_ms = 0.0;
};

void print_load_timing(const AscendLoadTiming& timing) {
  std::cerr << "timing load_sqlite_open_ms=" << timing.sqlite_open_ms << "\n";
  std::cerr << "timing load_inventory_ms=" << timing.inventory_ms << "\n";
  std::cerr << "timing load_string_ids_ms=" << timing.string_ids_ms << "\n";
  std::cerr << "timing load_runtime_calls_ms=" << timing.runtime_calls_ms
            << "\n";
  std::cerr << "timing load_compute_info_ms=" << timing.compute_info_ms
            << "\n";
  std::cerr << "timing load_cann_api_metadata_ms="
            << timing.cann_api_metadata_ms << "\n";
  std::cerr << "timing load_communication_task_info_ms="
            << timing.communication_task_info_ms << "\n";
  std::cerr << "timing load_task_rows_ms=" << timing.task_rows_ms << "\n";
  std::cerr << "timing load_communication_op_rows_ms="
            << timing.communication_op_rows_ms << "\n";
  std::cerr << "timing load_stream_info_capture_ms="
            << timing.stream_info_capture_ms << "\n";
  std::cerr << "timing load_cann_api_capture_slots_ms="
            << timing.cann_api_capture_slots_ms << "\n";
  std::cerr << "timing load_aclgraph_capture_instances_ms="
            << timing.aclgraph_capture_instances_ms << "\n";
  std::cerr << "timing load_aclgraph_launch_occurrences_ms="
            << timing.aclgraph_launch_occurrences_ms << "\n";
  std::cerr << "timing load_aclgraph_launch_bodies_ms="
            << timing.aclgraph_launch_bodies_ms << "\n";
  std::cerr << "timing load_aclgraph_launch_activities_ms="
            << timing.aclgraph_launch_activities_ms << "\n";
  std::cerr << "timing load_replay_composition_candidates_ms="
            << timing.replay_composition_candidates_ms << "\n";
  std::cerr << "timing load_aclgraph_replay_units_ms="
            << timing.aclgraph_replay_units_ms << "\n";
}

}  // namespace

using namespace ascend_sqlite_detail;

bool ascend_sqlite_has_usable_task_table(const std::string& db_path) {
  if (!file_exists(db_path) || !sqlite_table_has_rows(db_path, "TASK")) {
    return false;
  }
  try {
    SqliteDb db(db_path);
    return table_has_columns(db, "TASK",
                             {"startNs", "endNs", "deviceId", "streamId",
                              "taskId", "globalTaskId", "connectionId",
                              "taskType"});
  } catch (const std::exception&) {
    return false;
  }
}

bool looks_like_ascend_split_sqlite_profile(const std::string& profile_dir) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path root(profile_dir);
  if (!fs::is_directory(root, ec)) {
    return false;
  }
  fs::directory_iterator iterator(
      root, fs::directory_options::skip_permission_denied, ec);
  const fs::directory_iterator end;
  while (!ec && iterator != end) {
    const fs::path device_dir = iterator->path();
    const std::string name = device_dir.filename().string();
    if (iterator->is_directory(ec) && name.rfind("device_", 0) == 0) {
      const fs::path task_db = device_dir / "sqlite" / "ascend_task.db";
      if (sqlite_table_has_rows(task_db.string(), "AscendTask")) {
        return true;
      }
    }
    iterator.increment(ec);
  }
  return false;
}

AscendProfileEvidenceState classify_ascend_profile_evidence(
    const std::string& source_path) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path source(source_path);
  const bool source_is_directory = fs::is_directory(source, ec);
  fs::path profile_root = source_is_directory ? source : source.parent_path();
  bool source_has_profile_container = source_is_directory;
  if (!source_is_directory) {
    for (fs::path candidate = source.parent_path(); !candidate.empty();
         candidate = candidate.parent_path()) {
      const bool is_host_side_database =
          source.parent_path() == candidate / "host" / "sqlite";
      const bool is_monolithic_at_profile_root =
          source.parent_path() == candidate &&
          fs::is_directory(candidate / "host" / "sqlite", ec);
      if (is_host_side_database || is_monolithic_at_profile_root) {
        profile_root = candidate;
        source_has_profile_container = true;
        break;
      }
      if (candidate == candidate.root_path()) break;
    }
  }
  std::vector<std::string> missing;
  for (const fs::path& relative : {
           fs::path("host/sqlite/runtime.db"),
           fs::path("host/sqlite/stream_info.db"),
       }) {
    if (!fs::is_regular_file(profile_root / relative, ec)) {
      missing.push_back(relative.generic_string());
    }
  }
  bool has_device_task_db = false;
  for (fs::directory_iterator iterator(
           profile_root, fs::directory_options::skip_permission_denied, ec),
       end;
       !ec && iterator != end; iterator.increment(ec)) {
    const fs::path device_dir = iterator->path();
    if (iterator->is_directory(ec) &&
        device_dir.filename().string().rfind("device_", 0) == 0 &&
        fs::is_regular_file(device_dir / "sqlite" / "ascend_task.db", ec)) {
      has_device_task_db = true;
      break;
    }
  }
  if (!has_device_task_db) {
    missing.push_back("device_*/sqlite/ascend_task.db");
  }

  AscendProfileEvidenceState result;
  result.contract_version = "ascend_full_profile_v1";
  result.input_scope = missing.empty()
                           ? "full_profile_directory"
                           : (source_has_profile_container
                                  ? "partial_profile_directory"
                                  : "monolithic_db_only");
  result.evidence_state = missing.empty() ? "profile_directory_complete"
                                          : "evidence_incomplete";
  for (std::size_t index = 0; index < missing.size(); ++index) {
    if (index != 0) result.missing_components += ",";
    result.missing_components += missing[index];
  }
  return result;
}

std::vector<AscendSplitSQLiteTableInfo>
inventory_ascend_split_sqlite_profile(const std::string& profile_dir) {
  return inventory_split_profile_impl(profile_dir);
}

AscendSQLiteAdapter::AscendSQLiteAdapter(AscendSQLiteAdapterOptions options)
    : options_(std::move(options)) {}

AscendSQLiteAdapter::AscendSQLiteAdapter(std::string db_path,
                                         std::string source_kind)
    : options_(AscendSQLiteAdapterOptions{std::move(db_path),
                                          std::move(source_kind)}) {}

NativeIr AscendSQLiteAdapter::load() const {
  if (options_.db_path.empty()) {
    throw std::invalid_argument("Ascend SQLite DB path is empty");
  }
  std::error_code path_error;
  if (std::filesystem::is_directory(options_.db_path, path_error)) {
    return load_split_profile(options_);
  }
  if (!file_exists(options_.db_path)) {
    throw std::invalid_argument("Ascend SQLite DB does not exist: " +
                                options_.db_path);
  }

  AscendLoadTiming timing;
  const Stopwatch sqlite_open_watch;
  SqliteDb db(options_.db_path);
  timing.sqlite_open_ms = sqlite_open_watch.elapsed_ms();
  NativeIr ir;

  static constexpr const char* kInventorySql =
      "SELECT name FROM sqlite_master "
      "WHERE type IN ('table', 'view') "
      "ORDER BY name";

  bool saw_schema_object = false;
  std::unordered_map<std::string, SourceRefId> table_refs;
  timing.inventory_ms = time_stage([&]() {
    SqliteStmt stmt(db.get(), kInventorySql);
    while (true) {
      const int rc = sqlite3_step(stmt.get());
      if (rc == SQLITE_ROW) {
        const std::string table_name = sqlite_text(stmt.get(), 0);
        const SourceRefId source_ref = ir.source_refs.append(
            options_.source_kind, options_.db_path, table_name, 0);
        table_refs.emplace(table_name, source_ref);
        saw_schema_object = true;
        continue;
      }
      if (rc == SQLITE_DONE) {
        break;
      }

      const std::string message = sqlite3_errmsg(stmt.db());
      throw std::runtime_error("failed to read Ascend SQLite inventory: " +
                               message);
    }
  });

  if (!saw_schema_object) {
    ir.source_refs.append(options_.source_kind, options_.db_path,
                          "sqlite_schema", 0);
  }

  const auto has_table = [&](const char* table_name) {
    return table_refs.find(table_name) != table_refs.end();
  };
  if (has_table("TASK") &&
      !table_has_columns(db, "TASK",
                         {"startNs", "endNs", "deviceId", "streamId",
                          "taskId", "globalTaskId", "connectionId",
                          "taskType"})) {
    throw std::invalid_argument(
        "Ascend SQLite profile has an incompatible TASK schema: " +
        options_.db_path);
  }
  const bool string_ids_usable =
      has_table("STRING_IDS") &&
      table_has_columns(db, "STRING_IDS", {"id", "value"});
  const bool compute_info_usable =
      has_table("COMPUTE_TASK_INFO") &&
      table_has_columns(db, "COMPUTE_TASK_INFO",
                        {"globalTaskId", "name", "opType", "taskType"});
  const bool communication_task_info_usable =
      has_table("COMMUNICATION_TASK_INFO") &&
      table_has_columns(db, "COMMUNICATION_TASK_INFO",
                        {"globalTaskId", "name"});
  const bool cann_api_usable =
      has_table("CANN_API") &&
      table_has_columns(db, "CANN_API",
                        {"startNs", "endNs", "connectionId", "name"});
  const bool communication_op_usable =
      has_table("COMMUNICATION_OP") &&
      table_has_columns(db, "COMMUNICATION_OP",
                        {"startNs", "endNs", "deviceId", "connectionId",
                         "opName", "opId"});

  std::unordered_map<std::int64_t, std::string> string_ids;
  timing.string_ids_ms = time_stage([&]() {
    if (string_ids_usable) {
      string_ids = load_string_ids(db, ir);
    }
  });
  timing.runtime_calls_ms = time_stage([&]() {
    if (cann_api_usable) {
      load_ascend_runtime_calls(db, ir, string_ids,
                                table_refs.at("CANN_API"));
    }
  });
  std::unordered_map<std::int64_t, ComputeInfo> compute_info;
  timing.compute_info_ms = time_stage([&]() {
    if (compute_info_usable) {
      compute_info = load_compute_info(db, ir, string_ids);
    }
  });
  AclGraphCannApiMetadata cann_api_metadata;
  timing.cann_api_metadata_ms = time_stage([&]() {
    if (cann_api_usable) {
      cann_api_metadata = load_aclgraph_cann_api_metadata(db, string_ids);
    }
  });
  std::unordered_map<std::int64_t, CommunicationTaskInfo>
      communication_task_info;
  timing.communication_task_info_ms = time_stage([&]() {
    if (communication_task_info_usable) {
      communication_task_info = load_communication_task_info(
          db, ir, string_ids,
          table_has_column(db, "COMMUNICATION_TASK_INFO", "taskType"));
    }
  });
  StreamIndex streams;
  TaskLinkIndex task_links;
  timing.task_rows_ms = time_stage([&]() {
    if (has_table("TASK")) {
      load_task_rows(db, options_.db_path, options_.thread_count, ir, streams,
                     task_links, string_ids, compute_info,
                     communication_task_info, table_refs.at("TASK"));
    }
  });
  timing.communication_op_rows_ms = time_stage([&]() {
    if (communication_op_usable) {
      load_communication_op_rows(db, ir, streams, task_links, string_ids,
                                 table_refs.at("COMMUNICATION_OP"),
                                 table_has_column(db, "COMMUNICATION_OP",
                                                  "opType"));
    }
  });
  const std::string stream_info_path =
      stream_info_db_path_for_msprof(options_.db_path);
  const bool capture_stream_usable =
      aclgraph_capture_stream_schema_usable(stream_info_path);
  AclGraphCaptureInfo capture_info;
  timing.stream_info_capture_ms = time_stage([&]() {
    if (capture_stream_usable) {
      capture_info = load_aclgraph_capture_info(stream_info_path);
    }
  });
  timing.cann_api_capture_slots_ms = time_stage([&]() {
    if (cann_api_usable) {
      capture_info.capture_slots = std::move(cann_api_metadata.capture_slots);
      capture_info.replay_unit_signature = build_capture_replay_unit_signature(
          capture_info.capture_slots, capture_info.capture_group_size);
    }
  });
  CapturedGraphInstanceIndexes captured_graph_instances;
  timing.aclgraph_capture_instances_ms = time_stage([&]() {
    if (!capture_info.model_groups.empty()) {
      const SourceRefId capture_stream_source_ref = ir.source_refs.append(
          options_.source_kind, stream_info_path, "CaptureStreamInfo", 0);
      captured_graph_instances = materialize_aclgraph_capture_instances(
          ir, capture_info, capture_stream_source_ref,
          !cann_api_usable
              ? SourceRefId::invalid()
              : table_refs.at("CANN_API"));
    }
  });
  timing.aclgraph_launch_occurrences_ms = time_stage([&]() {
    if (has_table("TASK")) {
      materialize_aclgraph_launch_occurrences(
          ir, streams, captured_graph_instances,
          cann_api_metadata.execute_launches,
          !cann_api_usable
              ? SourceRefId::invalid()
              : table_refs.at("CANN_API"));
    }
  });
  std::set<GraphLaunchOccurrenceId> missing_body_capability_launches;
  timing.aclgraph_launch_bodies_ms = time_stage([&]() {
    missing_body_capability_launches = materialize_graph_launch_bodies(
        ir, compute_info_usable, communication_task_info_usable);
  });
  timing.aclgraph_launch_activities_ms = time_stage([&]() {
    const auto host_api_source = table_refs.find("CANN_API");
    if (cann_api_usable && host_api_source != table_refs.end()) {
      materialize_graph_launch_activities(
          ir, cann_api_metadata.launch_activities, host_api_source->second);
    }
  });
  timing.replay_composition_candidates_ms =
      time_stage([&]() {
        materialize_replay_composition_candidates(
            ir, missing_body_capability_launches);
      });
  timing.aclgraph_replay_units_ms = time_stage([&]() {
    const std::set<std::uint32_t> exact_claimed_devices =
        materialize_exact_aclgraph_replay_units(
            ir, options_.source_kind, options_.db_path);
    auto legacy_model_streams = capture_info.model_streams_by_device;
    for (std::uint32_t device_id : exact_claimed_devices) {
      legacy_model_streams.erase(device_id);
    }
    for (GraphLaunchOccurrenceId launch_id :
         missing_body_capability_launches) {
      const GraphLaunchOccurrenceRow& launch =
          ir.graph_launch_occurrences.row(launch_id);
      legacy_model_streams.erase(launch.device_id);
    }
    if (!legacy_model_streams.empty()) {
      const SourceRefId replay_source_ref = ir.source_refs.append(
          options_.source_kind, stream_info_path, "ACLGRAPH_REPLAY_UNIT", 0);
      materialize_aclgraph_replay_units(
          ir, legacy_model_streams,
          cann_api_metadata.execute_launches, replay_source_ref,
          capture_info.capture_group_size,
          capture_info.replay_unit_signature);
    }
  });

  if (options_.timing_diagnostics) {
    print_load_timing(timing);
  }

  return ir;
}

}  // namespace traceloom
