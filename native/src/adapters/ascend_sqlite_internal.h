#pragma once

#include "sqlite_profile_reader.h"

#include "traceloom/adapters/ascend_sqlite_adapter.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace traceloom::ascend_sqlite_detail {

using SqliteDb = sqlite_profile_detail::ReadOnlyDatabase;
using SqliteStmt = sqlite_profile_detail::Statement;

struct ComputeInfo {
  SymbolId op_name_symbol_id = SymbolId::invalid();
  SymbolId op_type_symbol_id = SymbolId::invalid();
  SymbolId compute_task_type_symbol_id = SymbolId::invalid();
};

struct CommunicationTaskInfo {
  SymbolId comm_name_symbol_id = SymbolId::invalid();
  SymbolId task_type_symbol_id = SymbolId::invalid();
};

struct LinkedTaskStats {
  std::uint32_t linked_task_count = 0;
  std::uint32_t linked_stream_count = 0;
  std::uint64_t primary_stream_id = 0;
  bool has_primary_stream = false;
  SymbolId linked_task_name_symbol_id;
  SymbolId linked_task_type_symbol_id;
};

using StreamIndex = std::unordered_map<std::uint64_t, StreamId>;
using CapturedGraphInstanceKey = std::pair<std::uint32_t, std::int64_t>;
using CapturedGraphInstanceIndex =
    std::map<CapturedGraphInstanceKey, CapturedGraphInstanceId>;
using CapturedGraphModelStreamKey =
    std::pair<std::uint32_t, std::uint64_t>;

struct CapturedGraphInstanceIndexes {
  CapturedGraphInstanceIndex by_model_id;
  std::map<CapturedGraphModelStreamKey, CapturedGraphInstanceId>
      by_model_stream;
};

struct TaskLink {
  std::uint64_t stream_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  SymbolId comm_name_symbol_id;
  SymbolId comm_task_type_symbol_id;
};

using TaskLinkIndex = std::unordered_map<std::uint64_t, std::vector<TaskLink>>;

struct RawTaskRow {
  std::uint64_t row_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::uint32_t device_id = 0;
  std::uint64_t raw_stream_id = 0;
  std::uint64_t raw_task_id = 0;
  std::int64_t raw_global_task_id = -1;
  std::int64_t raw_connection_id = -1;
  std::int64_t raw_context_id = -1;
  std::int64_t raw_task_type_id = -1;
  std::int64_t raw_model_id = -1;
};

struct RowidRange {
  std::int64_t first = 0;
  std::int64_t last = -1;
};

struct GraphTaskView {
  const TaskRow* task = nullptr;
  const TraceEventRow* event = nullptr;
};

struct GraphReplayUnitView {
  std::vector<GraphTaskView> rows;
  bool has_window = false;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::string template_signature;
};

struct ReplayUnitWindow {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

struct GraphLaunchView {
  std::int64_t raw_row_id = -1;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::int64_t connection_id = 0;
  std::uint64_t raw_global_tid = 0;
};

struct HostBlockingSyncView {
  std::int64_t raw_row_id = -1;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::uint64_t raw_global_tid = 0;
  std::string api_name;
};

struct GraphLaunchActivityView {
  std::uint64_t raw_global_tid = 0;
  std::vector<std::int64_t> host_execute_row_ids;
  std::int64_t first_host_api_row_id = -1;
  std::int64_t last_host_api_row_id = -1;
  std::int64_t boundary_host_api_row_id = -1;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::string boundary_api_name;
  GraphLaunchActivityBoundaryPolicy boundary_policy =
      GraphLaunchActivityBoundaryPolicy::kHostThreadTail;
};

struct CaptureSlotSignature {
  std::uint32_t slot_index = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::string host_api_signature;
};

struct CaptureModelStreamEvidence {
  std::uint64_t source_row_id = 0;
  std::int64_t raw_original_stream_id = -1;
  std::uint64_t raw_model_stream_id = 0;
};

struct CaptureModelGroupEvidence {
  std::uint32_t device_id = 0;
  std::int64_t raw_model_id = -1;
  std::int64_t raw_timestamp = -1;
  std::vector<CaptureModelStreamEvidence> streams;
};

struct CaptureInterval {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

struct CaptureTokenCandidate {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::string token;
};

struct AclGraphCannApiMetadata {
  std::vector<GraphLaunchView> execute_launches;
  std::vector<GraphLaunchActivityView> launch_activities;
  std::vector<CaptureSlotSignature> capture_slots;
};

struct AclGraphCaptureInfo {
  std::unordered_map<std::uint32_t, std::unordered_set<std::uint64_t>>
      model_streams_by_device;
  std::uint32_t capture_group_size = 0;
  std::vector<CaptureModelGroupEvidence> model_groups;
  std::vector<CaptureSlotSignature> capture_slots;
  std::string replay_unit_signature;
};

struct GraphTaskSymbolSets {
  std::unordered_set<std::uint32_t> graph_control;
  std::unordered_set<std::uint32_t> notify_wait;
  std::unordered_set<std::uint32_t> notify_record;
  std::unordered_set<std::uint32_t> model_execute;
};

bool file_exists(const std::string& path);
std::string quote_identifier(const std::string& value);
bool sqlite_table_has_rows(const std::string& path,
                           const std::string& table_name);
GraphReplayUnitView replay_unit_for_rows(std::vector<GraphTaskView> rows);
std::int64_t sqlite_i64(sqlite3_stmt* stmt,
                        int column,
                        std::int64_t fallback = -1);
std::uint32_t sqlite_u32(sqlite3_stmt* stmt, int column);
std::uint64_t sqlite_u64(sqlite3_stmt* stmt, int column);
std::int64_t normalize_raw_model_id(std::int64_t value);
std::string sqlite_text(sqlite3_stmt* stmt, int column);
std::string decode_string_id(
    const std::unordered_map<std::int64_t, std::string>& string_ids,
    std::int64_t raw_id);
bool table_has_column(SqliteDb& db,
                      const std::string& table_name,
                      const std::string& column_name);
bool table_has_columns(SqliteDb& db,
                       const std::string& table_name,
                       std::initializer_list<const char*> columns);

std::string lower_ascii(std::string value);
std::string normalize_key(std::string value);
bool graph_task_key(const std::string& key);
bool graph_body_excluded_task_key(const std::string& key);
bool graph_body_infrastructure_task_key(const std::string& key);
std::string symbol_value_or_empty(const NativeIr& ir, SymbolId id);
bool graph_body_task_is_communication(const TaskRow& task);
std::string graph_body_family(const std::string& label);
std::string canonical_graph_body_label(const std::string& family,
                                       const std::string& fallback);
std::string graph_body_token(const NativeIr& ir, const TaskRow& task);
std::uint64_t stable_hash64(const std::string& text);
std::string body_signature(const NativeIr& ir,
                           const std::vector<GraphTaskView>& rows);
std::uint32_t infer_replay_unit_count(
    const std::map<std::string, std::uint32_t>& body_counts,
    const std::map<std::string, std::uint32_t>& control_counts,
    std::uint32_t capture_group_size);
std::vector<std::int64_t> valid_inner_boundaries(
    std::vector<std::int64_t> boundaries);
std::vector<GraphReplayUnitView> split_rows_by_boundaries(
    const std::vector<GraphTaskView>& rows,
    const std::vector<std::int64_t>& boundaries);
std::vector<GraphReplayUnitView> split_rows_by_execute_waves(
    const std::vector<GraphTaskView>& rows,
    const std::vector<GraphLaunchView>& launches,
    std::uint32_t capture_group_size);
std::vector<GraphLaunchView> device_backed_execute_launches(
    const std::vector<GraphLaunchView>& launches,
    const std::vector<GraphTaskView>& model_executes,
    std::uint32_t capture_group_size);
std::vector<std::int64_t> control_boundaries(
    const std::vector<GraphTaskView>& controls,
    std::uint32_t expected_count);
std::vector<GraphReplayUnitView> split_activity(
    const NativeIr& ir,
    const std::vector<GraphTaskView>& rows,
    const std::vector<GraphTaskView>& notify_waits,
    const std::vector<GraphTaskView>& model_execs,
    std::uint32_t capture_group_size);

std::unordered_map<std::int64_t, std::string> load_string_ids(SqliteDb& db,
                                                              NativeIr& ir);
SymbolId intern_optional_string_id(
    NativeIr& ir,
    const std::unordered_map<std::int64_t, std::string>& string_ids,
    sqlite3_stmt* stmt,
    int column);
std::unordered_map<std::int64_t, ComputeInfo> load_compute_info(
    SqliteDb& db,
    NativeIr& ir,
    const std::unordered_map<std::int64_t, std::string>& string_ids);
std::unordered_map<std::int64_t, CommunicationTaskInfo>
load_communication_task_info(
    SqliteDb& db,
    NativeIr& ir,
    const std::unordered_map<std::int64_t, std::string>& string_ids,
    bool has_task_type_column);
std::string build_capture_replay_unit_signature(
    const std::vector<CaptureSlotSignature>& slots,
    std::uint32_t capture_group_size);
std::string capture_host_api_token(const std::string& name);
std::vector<CaptureInterval> build_capture_intervals(
    std::vector<CaptureInterval> begin_markers,
    std::vector<CaptureInterval> end_markers);
std::vector<CaptureSlotSignature> build_aclgraph_capture_slots(
    const std::vector<CaptureInterval>& intervals,
    std::vector<CaptureTokenCandidate> token_candidates);
std::vector<GraphLaunchActivityView> build_graph_launch_activities(
    const std::vector<GraphLaunchView>& execute_launches,
    const std::vector<HostBlockingSyncView>& blocking_syncs);
AclGraphCannApiMetadata load_aclgraph_cann_api_metadata(
    SqliteDb& db,
    const std::unordered_map<std::int64_t, std::string>& string_ids);
void load_ascend_runtime_calls(
    SqliteDb& db,
    NativeIr& ir,
    const std::unordered_map<std::int64_t, std::string>& string_ids,
    SourceRefId source_ref);
std::string stream_info_db_path_for_msprof(const std::string& db_path);
bool aclgraph_capture_stream_schema_usable(
    const std::string& stream_info_path);
AclGraphCaptureInfo load_aclgraph_capture_info(
    const std::string& stream_info_path);
CapturedGraphInstanceIndexes materialize_aclgraph_capture_instances(
    NativeIr& ir,
    const AclGraphCaptureInfo& capture_info,
    SourceRefId capture_stream_source_ref,
    SourceRefId cann_api_source_ref);

std::uint64_t stream_key(std::uint32_t device_id, std::uint64_t stream_id);
std::uint64_t connection_key(std::uint32_t device_id,
                             std::int64_t connection_id);
bool symbol_in_set(const std::unordered_set<std::uint32_t>& symbols,
                   SymbolId symbol_id);
GraphTaskSymbolSets build_graph_task_symbol_sets(const SymbolTable& symbols);
std::unordered_set<std::uint64_t> flatten_model_stream_keys(
    const std::unordered_map<std::uint32_t,
                             std::unordered_set<std::uint64_t>>&
        model_streams_by_device);
StreamId find_or_append_stream(StreamIndex& streams,
                               NativeIr& ir,
                               SourceRefId source_ref,
                               std::uint32_t device_id,
                               std::uint64_t raw_stream_id);
bool raw_task_row_less(const RawTaskRow& lhs, const RawTaskRow& rhs);
void load_task_rows(
    SqliteDb& db,
    const std::string& db_path,
    std::size_t thread_count,
    NativeIr& ir,
    StreamIndex& streams,
    TaskLinkIndex& task_links,
    const std::unordered_map<std::int64_t, std::string>& string_ids,
    const std::unordered_map<std::int64_t, ComputeInfo>& compute_info,
    const std::unordered_map<std::int64_t, CommunicationTaskInfo>&
        communication_task_info,
    SourceRefId task_table_ref);
void load_communication_op_rows(
    SqliteDb& db,
    NativeIr& ir,
    StreamIndex& streams,
    const TaskLinkIndex& task_links,
    const std::unordered_map<std::int64_t, std::string>& string_ids,
    SourceRefId comm_table_ref,
    bool has_op_type_column);

void materialize_aclgraph_launch_occurrences(
    NativeIr& ir,
    const StreamIndex& streams,
    const CapturedGraphInstanceIndexes& captured_graph_instances,
    const std::vector<GraphLaunchView>& host_execute_launches,
    SourceRefId host_api_source_ref);
std::vector<GraphTaskView> controls_with_symbol_set(
    const std::vector<GraphTaskView>& controls,
    const std::unordered_set<std::uint32_t>& task_type_symbols);
std::vector<GraphTaskView> controls_in_interval_from_sorted(
    const std::vector<GraphTaskView>& controls,
    std::int64_t start_ns,
    std::int64_t end_ns,
    std::size_t& cursor);
void materialize_graph_launch_activities(
    NativeIr& ir,
    const std::vector<GraphLaunchActivityView>& activities,
    SourceRefId host_api_source_ref);
std::set<GraphLaunchOccurrenceId> materialize_graph_launch_bodies(
    NativeIr& ir,
    bool compute_identity_source,
    bool communication_identity_source);

void materialize_replay_composition_candidates(
    NativeIr& ir,
    const std::set<GraphLaunchOccurrenceId>&
        missing_body_capability_launches);
std::set<std::uint32_t> materialize_exact_aclgraph_replay_units(
    NativeIr& ir,
    const std::string& source_kind,
    const std::string& source_path);
void materialize_aclgraph_replay_units(
    NativeIr& ir,
    const std::unordered_map<std::uint32_t,
                             std::unordered_set<std::uint64_t>>&
        model_streams_by_device,
    const std::vector<GraphLaunchView>& execute_launches,
    SourceRefId source_ref,
    std::uint32_t capture_group_size,
    const std::string& capture_replay_unit_signature);

std::vector<AscendSplitSQLiteTableInfo> inventory_split_profile_impl(
    const std::string& profile_dir);
NativeIr load_split_profile(const AscendSQLiteAdapterOptions& options);

}  // namespace traceloom::ascend_sqlite_detail
