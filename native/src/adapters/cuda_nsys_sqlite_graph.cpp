#include "cuda_nsys_sqlite_internal.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace traceloom::cuda_nsys_sqlite_detail {

struct CudaGraphLaunchEvidence {
  SourceRefId source_ref_id;
  std::uint64_t source_row_id = 0;
  std::int64_t correlation_id = -1;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

struct CudaGraphChildEvidence {
  SourceRefId source_ref_id;
  TaskId task_id;
  TraceEventId trace_event_id;
  std::uint64_t source_row_id = 0;
  std::int64_t correlation_id = -1;
  std::int64_t graph_node_id = -1;
  std::int64_t context_id = -1;
  std::uint32_t device_id = 0;
  std::uint32_t stream_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  bool compute = false;
  bool communication = false;
  std::string exact_label;
  std::string readable_label;
};

struct PreparedCudaGraphLaunch {
  const CudaGraphLaunchEvidence* launch = nullptr;
  std::vector<const CudaGraphChildEvidence*> children;
  ReplayCompositionRegionStatus failure_status =
      ReplayCompositionRegionStatus::kRecognizedCompletePattern;
  std::string identity_signature;
  std::string body_signature;
  std::string readable_body;
  std::uint32_t device_id = 0;
  std::uint32_t stream_count = 0;
  std::uint32_t compute_task_count = 0;
  std::uint32_t communication_task_count = 0;
  std::uint32_t data_move_task_count = 0;
  std::int64_t body_start_ns = 0;
  std::int64_t body_end_ns = 0;
  TaskId first_task_id;
  TaskId last_task_id;
};

SourceRefId source_ref_for_table(const NativeIr& ir,
                                 const std::string& table_name) {
  for (const SourceRefRow& source : ir.source_refs.rows()) {
    if (source.table_name == table_name) {
      return source.id;
    }
  }
  return SourceRefId::invalid();
}

std::map<std::uint64_t, TaskId> tasks_by_source_row(
    const NativeIr& ir, SourceRefId source_ref_id) {
  std::map<std::uint64_t, TaskId> out;
  if (!source_ref_id.valid()) {
    return out;
  }
  for (const TaskRow& task : ir.tasks.rows()) {
    if (task.source_ref_id != source_ref_id ||
        !task.trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
    out.emplace(event.source_row_id, task.id);
  }
  return out;
}

StreamId stream_for_raw_id(const NativeIr& ir, std::uint32_t device_id,
                           std::uint32_t stream_id) {
  for (const StreamRow& stream : ir.streams.rows()) {
    if (stream.device_id == device_id &&
        stream.raw_stream_id == stream_id) {
      return stream.id;
    }
  }
  return StreamId::invalid();
}

bool graph_node_activity_capability_complete(SqliteDb& db) {
  static const std::set<std::string> supported{
      "CUDA_GRAPH_NODE_EVENTS", "CUPTI_ACTIVITY_KIND_KERNEL",
      "CUPTI_ACTIVITY_KIND_MEMCPY"};
  for (const std::string& table : table_names(db)) {
    const ColumnMap columns = table_columns(db, table);
    const std::string graph_node = find_column(columns, "graphNodeId");
    if (graph_node.empty()) {
      continue;
    }
    SqliteStmt count(
        db.get(), "SELECT COUNT(*) FROM " + quote_identifier(table) +
                      " WHERE " + quote_identifier(graph_node) +
                      " IS NOT NULL");
    if (sqlite3_step(count.get()) != SQLITE_ROW) {
      throw std::runtime_error(
          "failed to inspect CUDA graph-node activity capability: " +
          std::string(sqlite3_errmsg(count.db())));
    }
    if (sqlite_i64(count.get(), 0) == 0) {
      continue;
    }
    if (supported.find(table) == supported.end()) {
      return false;
    }
    std::vector<std::string> required;
    if (table == kKernelTable) {
      required = {find_column(columns, "contextId"),
                  find_column(columns, "correlationId")};
    } else if (table == "CUPTI_ACTIVITY_KIND_MEMCPY") {
      required = {find_column(columns, "contextId"),
                  find_column(columns, "correlationId"),
                  find_column(columns, "bytes"),
                  find_column(columns, "copyKind")};
    }
    if (std::any_of(required.begin(), required.end(),
                    [](const std::string& column) {
                      return column.empty();
                    })) {
      return false;
    }
    if (!required.empty()) {
      std::string incomplete =
          quote_identifier(graph_node) + " IS NOT NULL AND (";
      for (std::size_t index = 0; index < required.size(); ++index) {
        if (index != 0) {
          incomplete += " OR ";
        }
        incomplete += quote_identifier(required[index]) + " IS NULL";
      }
      incomplete += ")";
      SqliteStmt incomplete_count(
          db.get(), "SELECT COUNT(*) FROM " + quote_identifier(table) +
                        " WHERE " + incomplete);
      if (sqlite3_step(incomplete_count.get()) != SQLITE_ROW) {
        throw std::runtime_error(
            "failed to inspect CUDA graph-node activity fields: " +
            std::string(sqlite3_errmsg(incomplete_count.db())));
      }
      if (sqlite_i64(incomplete_count.get(), 0) > 0) {
        return false;
      }
    }
  }
  return true;
}

std::vector<CudaGraphLaunchEvidence> load_cuda_graph_launch_evidence(
    SqliteDb& db, const NativeIr& ir,
    const std::unordered_map<std::int64_t, std::string>& strings) {
  constexpr const char* table = "CUPTI_ACTIVITY_KIND_RUNTIME";
  const SourceRefId source_ref = source_ref_for_table(ir, table);
  if (!source_ref.valid()) {
    return {};
  }
  const ColumnMap columns = table_columns(db, table);
  const std::string start = find_column(columns, "start");
  const std::string end = find_column(columns, "end");
  const std::string correlation = find_column(columns, "correlationId");
  const std::string name = find_column(columns, "nameId");
  if (start.empty() || end.empty() || correlation.empty() || name.empty()) {
    return {};
  }
  const std::map<std::uint64_t, TaskId> task_ids =
      tasks_by_source_row(ir, source_ref);
  SqliteStmt stmt(
      db.get(), "SELECT rowid, " + quote_identifier(start) + ", " +
                    quote_identifier(end) + ", " +
                    quote_identifier(correlation) + ", " +
                    quote_identifier(name) + " FROM " +
                    quote_identifier(table) + " WHERE " +
                    quote_identifier(correlation) + " IS NOT NULL ORDER BY " +
                    quote_identifier(start) + ", rowid");
  std::vector<CudaGraphLaunchEvidence> out;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      return out;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error("failed to load CUDA graph launch APIs: " +
                               std::string(sqlite3_errmsg(stmt.db())));
    }
    const std::string api_name = lower_ascii(resolved_name(stmt.get(), 4,
                                                            strings));
    if (api_name.find("cudagraphlaunch") == std::string::npos) {
      continue;
    }
    const std::uint64_t source_row_id =
        static_cast<std::uint64_t>(sqlite_i64(stmt.get(), 0));
    const auto task = task_ids.find(source_row_id);
    if (task == task_ids.end()) {
      continue;
    }
    out.push_back(CudaGraphLaunchEvidence{
        source_ref, source_row_id, sqlite_i64(stmt.get(), 3, -1),
        sqlite_i64(stmt.get(), 1), sqlite_i64(stmt.get(), 2)});
  }
}

void load_cuda_graph_kernel_children(
    SqliteDb& db, const NativeIr& ir,
    std::vector<CudaGraphChildEvidence>& out) {
  const SourceRefId source_ref = source_ref_for_table(ir, kKernelTable);
  if (!source_ref.valid()) {
    return;
  }
  const ColumnMap columns = table_columns(db, kKernelTable);
  const std::string context = find_column(columns, "contextId");
  const std::string correlation = find_column(columns, "correlationId");
  const std::string graph_node = find_column(columns, "graphNodeId");
  if (context.empty() || correlation.empty() || graph_node.empty()) {
    return;
  }
  const std::map<std::uint64_t, TaskId> task_ids =
      tasks_by_source_row(ir, source_ref);
  SqliteStmt stmt(
      db.get(), "SELECT rowid, start, end, deviceId, streamId, " +
                    quote_identifier(context) + ", " +
                    quote_identifier(correlation) + ", " +
                    quote_identifier(graph_node) + " FROM " +
                    quote_identifier(kKernelTable) + " WHERE " +
                    quote_identifier(correlation) + " IS NOT NULL AND " +
                    quote_identifier(graph_node) + " IS NOT NULL ORDER BY " +
                    "start, end, rowid");
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      return;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error("failed to load CUDA graph kernel children: " +
                               std::string(sqlite3_errmsg(stmt.db())));
    }
    const std::uint64_t source_row_id =
        static_cast<std::uint64_t>(sqlite_i64(stmt.get(), 0));
    const auto task = task_ids.find(source_row_id);
    if (task == task_ids.end()) {
      continue;
    }
    const TaskRow& task_row = ir.tasks.row(task->second);
    const std::string raw_label = task_row.op_name_symbol_id.valid()
                                      ? ir.symbols.value(
                                            task_row.op_name_symbol_id)
                                      : "cuda_kernel_" +
                                            std::to_string(source_row_id);
    const bool communication = task_row.comm_name_symbol_id.valid();
    out.push_back(CudaGraphChildEvidence{
        source_ref,
        task->second,
        task_row.trace_event_id,
        source_row_id,
        sqlite_i64(stmt.get(), 6, -1),
        sqlite_i64(stmt.get(), 7, -1),
        sqlite_i64(stmt.get(), 5, -1),
        checked_u32(sqlite_i64(stmt.get(), 3, -1), "deviceId",
                    source_row_id),
        checked_u32(sqlite_i64(stmt.get(), 4, -1), "streamId",
                    source_row_id),
        sqlite_i64(stmt.get(), 1),
        sqlite_i64(stmt.get(), 2),
        !communication,
        communication,
        std::string(communication ? "communication\t" : "kernel\t") +
            raw_label,
        raw_label});
  }
}

void load_cuda_graph_memcpy_children(
    SqliteDb& db, const NativeIr& ir,
    std::vector<CudaGraphChildEvidence>& out) {
  constexpr const char* table = "CUPTI_ACTIVITY_KIND_MEMCPY";
  const SourceRefId source_ref = source_ref_for_table(ir, table);
  if (!source_ref.valid()) {
    return;
  }
  const ColumnMap columns = table_columns(db, table);
  const std::string context = find_column(columns, "contextId");
  const std::string correlation = find_column(columns, "correlationId");
  const std::string graph_node = find_column(columns, "graphNodeId");
  const std::string bytes = find_column(columns, "bytes");
  const std::string copy_kind = find_column(columns, "copyKind");
  if (context.empty() || correlation.empty() || graph_node.empty() ||
      bytes.empty() || copy_kind.empty()) {
    return;
  }
  const std::map<std::uint64_t, TaskId> task_ids =
      tasks_by_source_row(ir, source_ref);
  SqliteStmt stmt(
      db.get(), "SELECT rowid, start, end, deviceId, streamId, " +
                    quote_identifier(context) + ", " +
                    quote_identifier(correlation) + ", " +
                    quote_identifier(graph_node) + ", " +
                    quote_identifier(bytes) + ", " +
                    quote_identifier(copy_kind) + " FROM " +
                    quote_identifier(table) + " WHERE " +
                    quote_identifier(correlation) + " IS NOT NULL AND " +
                    quote_identifier(graph_node) + " IS NOT NULL ORDER BY " +
                    "start, end, rowid");
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      return;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error("failed to load CUDA graph memcpy children: " +
                               std::string(sqlite3_errmsg(stmt.db())));
    }
    const std::uint64_t source_row_id =
        static_cast<std::uint64_t>(sqlite_i64(stmt.get(), 0));
    const auto task = task_ids.find(source_row_id);
    if (task == task_ids.end()) {
      continue;
    }
    const std::string label =
        "CudaMemcpy kind=" + std::to_string(sqlite_i64(stmt.get(), 9)) +
        " bytes=" + std::to_string(sqlite_i64(stmt.get(), 8));
    const TaskRow& task_row = ir.tasks.row(task->second);
    out.push_back(CudaGraphChildEvidence{
        source_ref,
        task->second,
        task_row.trace_event_id,
        source_row_id,
        sqlite_i64(stmt.get(), 6, -1),
        sqlite_i64(stmt.get(), 7, -1),
        sqlite_i64(stmt.get(), 5, -1),
        checked_u32(sqlite_i64(stmt.get(), 3, -1), "deviceId",
                    source_row_id),
        checked_u32(sqlite_i64(stmt.get(), 4, -1), "streamId",
                    source_row_id),
        sqlite_i64(stmt.get(), 1),
        sqlite_i64(stmt.get(), 2),
        false,
        false,
        "memcpy\t" + label,
        label});
  }
}

void prepare_cuda_graph_body(PreparedCudaGraphLaunch& prepared) {
  if (prepared.children.empty()) {
    prepared.failure_status = ReplayCompositionRegionStatus::
        kUnrecognizedMissingBodyEvidence;
    return;
  }
  std::sort(prepared.children.begin(), prepared.children.end(),
            [](const CudaGraphChildEvidence* lhs,
               const CudaGraphChildEvidence* rhs) {
              return std::tie(lhs->start_ns, lhs->end_ns, lhs->device_id,
                              lhs->stream_id, lhs->source_row_id) <
                     std::tie(rhs->start_ns, rhs->end_ns, rhs->device_id,
                              rhs->stream_id, rhs->source_row_id);
            });
  prepared.device_id = prepared.children.front()->device_id;
  const std::int64_t context_id = prepared.children.front()->context_id;
  prepared.body_start_ns = prepared.children.front()->start_ns;
  prepared.body_end_ns = prepared.children.front()->end_ns;
  prepared.first_task_id = prepared.children.front()->task_id;
  prepared.last_task_id = prepared.children.front()->task_id;
  std::set<std::uint32_t> stream_ids;
  std::set<std::int64_t> graph_node_ids;
  for (const CudaGraphChildEvidence* child : prepared.children) {
    if (child->device_id != prepared.device_id ||
        child->context_id != context_id || child->graph_node_id < 0 ||
        !child->task_id.valid() || !child->trace_event_id.valid()) {
      prepared.failure_status = ReplayCompositionRegionStatus::
          kUnrecognizedAmbiguousLaunchEvidence;
      return;
    }
    graph_node_ids.insert(child->graph_node_id);
    stream_ids.insert(child->stream_id);
    prepared.body_start_ns =
        std::min(prepared.body_start_ns, child->start_ns);
    prepared.body_end_ns = std::max(prepared.body_end_ns, child->end_ns);
    prepared.last_task_id = child->task_id;
    if (child->communication) {
      ++prepared.communication_task_count;
    } else if (child->compute) {
      ++prepared.compute_task_count;
    } else {
      ++prepared.data_move_task_count;
    }
  }
  prepared.stream_count = static_cast<std::uint32_t>(stream_ids.size());
  prepared.identity_signature =
      "cuda_graph_raw_node_set_v1\ndevice=" +
      std::to_string(prepared.device_id) + "\ncontext=" +
      std::to_string(context_id) + "\n";
  for (std::int64_t graph_node_id : graph_node_ids) {
    prepared.identity_signature += std::to_string(graph_node_id) + "\n";
  }

  std::map<std::int64_t, std::uint32_t> node_ordinals;
  for (std::int64_t graph_node_id : graph_node_ids) {
    node_ordinals.emplace(
        graph_node_id, static_cast<std::uint32_t>(node_ordinals.size()));
  }

  std::map<std::uint32_t, std::vector<const CudaGraphChildEvidence*>> lanes;
  for (const CudaGraphChildEvidence* child : prepared.children) {
    lanes[child->stream_id].push_back(child);
  }
  std::vector<std::pair<std::string, std::string>> lane_sequences;
  for (const auto& lane : lanes) {
    std::string exact;
    std::string readable;
    for (const CudaGraphChildEvidence* child : lane.second) {
      exact += child->exact_label + "\tnode=" +
               std::to_string(node_ordinals.at(child->graph_node_id)) + "\n";
      if (!readable.empty()) {
        readable += "\n";
      }
      readable += child->readable_label;
    }
    lane_sequences.emplace_back(std::move(exact), std::move(readable));
  }
  std::sort(lane_sequences.begin(), lane_sequences.end());
  prepared.body_signature =
      "cuda_graph_observed_stream_set_v1\nstream_count=" +
      std::to_string(lane_sequences.size()) + "\n";
  for (std::size_t lane = 0; lane < lane_sequences.size(); ++lane) {
    prepared.body_signature +=
        "lane_begin\n" + lane_sequences[lane].first + "lane_end\n";
    if (!prepared.readable_body.empty()) {
      prepared.readable_body += "\n";
    }
    prepared.readable_body += "lane " + std::to_string(lane) + ":\n" +
                              lane_sequences[lane].second;
  }
}

std::map<std::int64_t, std::int64_t> load_cuda_graph_node_original_mapping(
    SqliteDb& db) {
  // CUDA_GRAPH_NODE_EVENTS.originalGraphNodeId is retained for exact node
  // identity, but the column is optional in older/variant schemas. When
  // either mapping column is unavailable, return an empty mapping: exact
  // graphNodeId/correlation reconstruction keeps working and the original
  // identity stays unmapped (-1) rather than failing or guessing. A raw
  // graphNodeId is mapped only when exactly one non-null original exists;
  // missing or ambiguous mappings stay unmapped (-1).
  const ColumnMap node_columns = table_columns(db, "CUDA_GRAPH_NODE_EVENTS");
  const std::string raw_graph_node_column =
      find_column(node_columns, "graphNodeId");
  const std::string original_graph_node_column =
      find_column(node_columns, "originalGraphNodeId");
  if (raw_graph_node_column.empty() || original_graph_node_column.empty()) {
    return {};
  }
  std::map<std::int64_t, std::set<std::int64_t>> originals_by_graph_node;
  SqliteStmt stmt(
      db.get(),
      "SELECT " + raw_graph_node_column + ", " + original_graph_node_column +
          " FROM CUDA_GRAPH_NODE_EVENTS WHERE " +
          original_graph_node_column + " IS NOT NULL ORDER BY rowid");
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error(
          "failed to load CUDA graph node original identity: " +
          std::string(sqlite3_errmsg(stmt.db())));
    }
    originals_by_graph_node[sqlite_i64(stmt.get(), 0)].insert(
        sqlite_i64(stmt.get(), 1));
  }
  std::map<std::int64_t, std::int64_t> mapping;
  for (const auto& item : originals_by_graph_node) {
    if (item.second.size() == 1) {
      mapping.emplace(item.first, *item.second.begin());
    }
  }
  return mapping;
}

void materialize_cuda_graph_node_replays(
    SqliteDb& db, NativeIr& ir, const CudaNsightSQLiteAdapterOptions& options,
    const std::unordered_map<std::int64_t, std::string>& strings) {
  const std::vector<std::string> tables = table_names(db);
  if (!has_name(tables, "CUDA_GRAPH_NODE_EVENTS") ||
      table_row_count(db, "CUDA_GRAPH_NODE_EVENTS") == 0) {
    return;
  }
  const bool body_capability_complete =
      graph_node_activity_capability_complete(db);
  const std::vector<CudaGraphLaunchEvidence> launches =
      load_cuda_graph_launch_evidence(db, ir, strings);
  if (launches.empty()) {
    return;
  }
  const std::map<std::int64_t, std::int64_t> original_graph_node_ids =
      load_cuda_graph_node_original_mapping(db);
  std::vector<CudaGraphChildEvidence> children;
  load_cuda_graph_kernel_children(db, ir, children);
  load_cuda_graph_memcpy_children(db, ir, children);

  std::map<std::int64_t, std::vector<const CudaGraphLaunchEvidence*>>
      launches_by_correlation;
  std::map<std::int64_t, std::vector<const CudaGraphChildEvidence*>>
      children_by_correlation;
  for (const CudaGraphLaunchEvidence& launch : launches) {
    launches_by_correlation[launch.correlation_id].push_back(&launch);
  }
  for (const CudaGraphChildEvidence& child : children) {
    children_by_correlation[child.correlation_id].push_back(&child);
  }

  std::vector<PreparedCudaGraphLaunch> prepared_launches;
  prepared_launches.reserve(launches.size());
  for (const CudaGraphLaunchEvidence& launch : launches) {
    PreparedCudaGraphLaunch prepared;
    prepared.launch = &launch;
    if (!body_capability_complete) {
      prepared.failure_status = ReplayCompositionRegionStatus::
          kUnrecognizedMissingBodyCapability;
    } else if (launches_by_correlation[launch.correlation_id].size() != 1) {
      prepared.failure_status = ReplayCompositionRegionStatus::
          kUnrecognizedAmbiguousLaunchEvidence;
    } else {
      prepared.children = children_by_correlation[launch.correlation_id];
      prepare_cuda_graph_body(prepared);
    }
    prepared_launches.push_back(std::move(prepared));
  }

  std::map<std::string, std::size_t> repeat_counts;
  std::map<std::string, std::set<std::string>> bodies_by_identity;
  for (const PreparedCudaGraphLaunch& prepared : prepared_launches) {
    if (prepared.failure_status ==
        ReplayCompositionRegionStatus::kRecognizedCompletePattern) {
      ++repeat_counts[prepared.identity_signature + prepared.body_signature];
      bodies_by_identity[prepared.identity_signature].insert(
          prepared.body_signature);
    }
  }
  static constexpr std::size_t kMinimumExactBodyRepeats = 2;
  std::map<std::string, ReplayBodyTemplateId> body_templates;
  std::map<std::string, GraphTemplateId> graph_templates;
  SourceRefId replay_source_ref = SourceRefId::invalid();

  for (PreparedCudaGraphLaunch& prepared : prepared_launches) {
    const CudaGraphLaunchEvidence& launch = *prepared.launch;
    const bool has_body = !prepared.body_signature.empty();
    if (prepared.failure_status ==
        ReplayCompositionRegionStatus::kRecognizedCompletePattern) {
      if (bodies_by_identity[prepared.identity_signature].size() != 1) {
        prepared.failure_status =
            ReplayCompositionRegionStatus::kUnrecognizedBodyMismatch;
      } else if (repeat_counts[prepared.identity_signature +
                               prepared.body_signature] <
                 kMinimumExactBodyRepeats) {
        prepared.failure_status = ReplayCompositionRegionStatus::
            kUnrecognizedInsufficientRepeatEvidence;
      }
    }

    ReplayBodyTemplateId body_template = ReplayBodyTemplateId::invalid();
    if (has_body) {
      const auto found = body_templates.find(prepared.body_signature);
      if (found == body_templates.end()) {
        body_template = ir.replay_body_templates.append(
            prepared.children.front()->source_ref_id,
            stable_hash64(prepared.body_signature),
            ir.symbols.intern(prepared.readable_body),
            prepared.compute_task_count, prepared.communication_task_count,
            prepared.stream_count,
            ReplayBodyTopologyPolicy::kObservedStreamSetUnordered,
            prepared.data_move_task_count);
        body_templates.emplace(prepared.body_signature, body_template);
      } else {
        body_template = found->second;
      }
    }

    const bool direct_correlation = has_body &&
        launches_by_correlation[launch.correlation_id].size() == 1;
    const std::uint32_t device_id =
        has_body ? prepared.device_id : 0;
    const StreamId execute_stream =
        has_body ? stream_for_raw_id(ir, device_id,
                                     prepared.children.front()->stream_id)
                 : StreamId::invalid();
    const std::int64_t start_ns =
        has_body ? prepared.body_start_ns : launch.start_ns;
    const std::int64_t end_ns =
        has_body ? prepared.body_end_ns : launch.end_ns;
    const GraphLaunchOccurrenceId occurrence =
        ir.graph_launch_occurrences.append(
            launch.source_ref_id, launch.source_ref_id, device_id,
            static_cast<std::int64_t>(launch.source_row_id),
            launch.correlation_id, -1, -1, execute_stream,
            StreamId::invalid(), CapturedGraphInstanceId::invalid(),
            TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), start_ns,
            end_ns, -1,
            direct_correlation
                ? GraphLaunchMatchPolicy::kCudaRuntimeCorrelation
                : GraphLaunchMatchPolicy::kUnmatched,
            has_body
                ? GraphLaunchInstanceAssociationPolicy::kCudaGraphNodeSet
                : GraphLaunchInstanceAssociationPolicy::kNone);
    if (has_body) {
      const GraphLaunchBodyId body_id = ir.graph_launch_bodies.append(
          occurrence, body_template, prepared.first_task_id,
          prepared.last_task_id, prepared.compute_task_count,
          prepared.communication_task_count, prepared.stream_count,
          prepared.data_move_task_count);
      std::map<std::uint32_t, std::uint32_t> lane_ordinals;
      std::map<std::uint32_t, std::uint32_t> task_ordinals;
      for (const CudaGraphChildEvidence* child : prepared.children) {
        if (lane_ordinals.find(child->stream_id) == lane_ordinals.end()) {
          lane_ordinals.emplace(
              child->stream_id,
              static_cast<std::uint32_t>(lane_ordinals.size()));
        }
      }
      // Numeric stream order is occurrence-stable and the raw stream remains
      // available through the referenced TraceEventRow. Template identity is
      // independently canonicalized by prepare_cuda_graph_body().
      std::uint32_t lane = 0;
      for (auto& item : lane_ordinals) {
        item.second = lane++;
      }
      for (const CudaGraphChildEvidence* child : prepared.children) {
        const std::uint32_t lane_ordinal = lane_ordinals.at(child->stream_id);
        const auto original = original_graph_node_ids.find(
            child->graph_node_id);
        ir.graph_launch_body_members.append(
            body_id, child->task_id, lane_ordinal,
            task_ordinals[lane_ordinal]++,
            child->communication
                ? GraphLaunchBodyMemberRow::Kind::kCommunication
                : (child->compute
                       ? GraphLaunchBodyMemberRow::Kind::kCompute
                       : GraphLaunchBodyMemberRow::Kind::kDataMove),
            child->graph_node_id,
            original == original_graph_node_ids.end()
                ? -1
                : original->second);
      }
    }

    const bool recognized =
        prepared.failure_status ==
        ReplayCompositionRegionStatus::kRecognizedCompletePattern;
    const ReplayCompositionCandidateId candidate =
        ir.replay_composition_candidates.append(
            launch.source_ref_id, device_id, occurrence, occurrence, 1, 0,
            has_body ? 1 : 0, recognized ? 1 : 0, 0,
            stable_hash64(prepared.identity_signature +
                          prepared.body_signature),
            has_body ? ReplayCompositionIdentityPolicy::kCudaGraphNodeSet
                     : ReplayCompositionIdentityPolicy::kUnavailable,
            ReplayCompositionOrderPolicy::kHostSubmissionOrder,
            recognized ? ReplayCompositionShapePolicy::kSingleGraph
                       : ReplayCompositionShapePolicy::kUnclassified,
            ReplayCompositionBoundaryPolicy::kDirectObservedGraphLaunch);
    ReplayCompositionSlotId slot = ReplayCompositionSlotId::invalid();
    if (has_body) {
      slot = ir.replay_composition_slots.append(
          candidate, 0, CapturedGraphInstanceId::invalid(),
          GraphSlotTemplateId::invalid(), body_template,
          ReplayCompositionSlotRole::kCudaGraph, -1);
    }
    const ReplayCompositionRegionId region =
        ir.replay_composition_regions.append(
            candidate, 0, occurrence, occurrence, start_ns, end_ns, 1, 1,
            prepared.failure_status);
    ir.replay_composition_region_members.append(
        region, 0, occurrence, has_body ? 0 : -1);
    if (!recognized) {
      continue;
    }

    if (!replay_source_ref.valid()) {
      replay_source_ref = ir.source_refs.append(
          options.source_kind, options.db_path, "CUDA_GRAPH_REPLAY_UNIT", 0);
    }
    GraphTemplateId graph_template = GraphTemplateId::invalid();
    const auto graph_found = graph_templates.find(prepared.body_signature);
    if (graph_found == graph_templates.end()) {
      graph_template = ir.graph_templates.append(
          replay_source_ref, stable_hash64(prepared.body_signature), 1);
      graph_templates.emplace(prepared.body_signature, graph_template);
    } else {
      graph_template = graph_found->second;
    }
    const std::uint32_t raw_stream_id =
        prepared.children.front()->stream_id;
    const std::string symbol =
        "CUDAGraph ExactT" + std::to_string(graph_template.value() + 1);
    const TraceEventId event = ir.trace_events.append(
        replay_source_ref, region.value() + 1, device_id, raw_stream_id,
        start_ns, end_ns, ir.symbols.intern(symbol));
    const ReplayUnitId unit = ir.replay_units.append(
        graph_template, replay_source_ref, AnchorId::invalid(),
        AnchorId::invalid(), event, region);
    ir.replay_unit_launch_members.append(unit, 0, occurrence, slot);
  }
}

}  // namespace traceloom::cuda_nsys_sqlite_detail
