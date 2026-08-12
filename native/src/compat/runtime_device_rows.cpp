#include "traceloom/compat/runtime_device_rows.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/timeline_rows.h"

namespace traceloom::compat {
namespace {

double ns_to_us(std::int64_t ns) {
  return static_cast<double>(ns) / 1000.0;
}

std::string nullable_i64(std::int64_t value) {
  return value < 0 ? std::string() : std::to_string(value);
}

std::string symbol_value_or_empty(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

const char* provider_name(RuntimeCallProvider provider) {
  switch (provider) {
    case RuntimeCallProvider::kCuda:
      return "cuda";
    case RuntimeCallProvider::kAscend:
      return "ascend";
    case RuntimeCallProvider::kUnknown:
      return "unknown";
  }
  return "unknown";
}

const char* clock_domain_name(RuntimeCallClockDomain domain) {
  switch (domain) {
    case RuntimeCallClockDomain::kProfilerHost:
      return "profiler_host";
    case RuntimeCallClockDomain::kUnknown:
      return "unknown";
  }
  return "unknown";
}

const char* match_policy_name(RuntimeCallMatchPolicy policy) {
  switch (policy) {
    case RuntimeCallMatchPolicy::kCudaCorrelationId:
      return "cuda_correlation_id";
    case RuntimeCallMatchPolicy::kAscendConnectionId:
      return "ascend_connection_id";
    case RuntimeCallMatchPolicy::kUnsupported:
      return "unsupported";
  }
  return "unsupported";
}

RuntimeCallProvider task_provider(const SourceRefRow& source) {
  if (source.table_name.rfind("CUPTI_ACTIVITY_KIND_", 0) == 0) {
    return RuntimeCallProvider::kCuda;
  }
  if (source.table_name == "TASK" || source.table_name == "AscendTask") {
    return RuntimeCallProvider::kAscend;
  }
  return RuntimeCallProvider::kUnknown;
}

RuntimeCallProvider graph_provider(const GraphLaunchOccurrenceRow& launch,
                                   const SourceRefRow& source) {
  if (launch.match_policy == GraphLaunchMatchPolicy::kCudaRuntimeCorrelation ||
      launch.instance_association_policy ==
          GraphLaunchInstanceAssociationPolicy::kCudaGraphNodeSet ||
      source.table_name.rfind("CUPTI_", 0) == 0 ||
      source.table_name.find("CUDA") != std::string::npos) {
    return RuntimeCallProvider::kCuda;
  }
  if (launch.match_policy ==
          GraphLaunchMatchPolicy::kNotifyCompletionAdjacent ||
      launch.match_policy == GraphLaunchMatchPolicy::kNotifyOrderedFallback) {
    return RuntimeCallProvider::kAscend;
  }
  if (source.table_name == "CANN_API" || source.table_name == "ApiData" ||
      source.table_name.find("ACLGRAPH") != std::string::npos) {
    return RuntimeCallProvider::kAscend;
  }
  return RuntimeCallProvider::kUnknown;
}

bool host_only_task_source(const SourceRefRow& source) {
  return source.table_name == "CUPTI_ACTIVITY_KIND_RUNTIME" ||
         source.table_name == "CANN_API" || source.table_name == "ApiData";
}

std::string runtime_call_id(RuntimeCallId id) {
  return "runtime-call-" + std::to_string(id.value());
}

std::string task_id(TaskId id) {
  return "task-" + std::to_string(id.value());
}

std::string event_work_id(TraceEventId id) {
  return "device-work-event-" + std::to_string(id.value());
}

std::string graph_work_id(GraphLaunchOccurrenceId id) {
  return "device-work-graph-launch-" + std::to_string(id.value());
}

std::string json_escape(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    if (ch == '\\' || ch == '"') out.push_back('\\');
    out.push_back(ch);
  }
  return out;
}

std::string source_json(const SourceRefRow& source, const char* kind) {
  std::ostringstream out;
  out << "{\"source_ref_id\":" << source.id.value()
      << ",\"source_path\":\"" << json_escape(source.source_path) << "\""
      << ",\"relation_object\":\"" << kind << "\"}";
  return out.str();
}

struct WorkRef {
  std::size_t row_index = 0;
  std::uint32_t device_id = 0;
};

using CorrelationKey = std::pair<RuntimeCallProvider, std::int64_t>;

using ActivityGroupKey =
    std::tuple<std::string, std::string, std::string, std::string>;

struct ActivityGroup {
  std::vector<const RuntimeCallSqlRow*> calls;
  std::vector<std::int64_t> starts;
  std::vector<std::int64_t> prefix_max_ends;
};

ActivityGroupKey activity_group_key(const RuntimeCallSqlRow& call,
                                    const std::string& scope_policy) {
  if (scope_policy == "same_thread") {
    return {call.provider, call.clock_domain, call.process_id, call.thread_id};
  }
  if (scope_policy == "same_process") {
    return {call.provider, call.clock_domain, call.process_id, {}};
  }
  return {call.provider, call.clock_domain, {}, {}};
}

ActivityGroupKey activity_group_key(const AnchorHostIntervalSqlRow& interval) {
  if (interval.scope_policy == "same_thread") {
    return {interval.provider, interval.clock_domain, interval.process_id,
            interval.thread_id};
  }
  if (interval.scope_policy == "same_process") {
    return {interval.provider, interval.clock_domain, interval.process_id, {}};
  }
  return {interval.provider, interval.clock_domain, {}, {}};
}

void finalize_activity_group(ActivityGroup& group) {
  std::sort(group.calls.begin(), group.calls.end(),
            [](const RuntimeCallSqlRow* lhs, const RuntimeCallSqlRow* rhs) {
              return std::tie(lhs->start_ns, lhs->end_ns,
                              lhs->runtime_call_id) <
                     std::tie(rhs->start_ns, rhs->end_ns,
                              rhs->runtime_call_id);
            });
  group.starts.reserve(group.calls.size());
  group.prefix_max_ends.reserve(group.calls.size());
  std::int64_t max_end = std::numeric_limits<std::int64_t>::min();
  for (const RuntimeCallSqlRow* call : group.calls) {
    group.starts.push_back(call->start_ns);
    max_end = std::max(max_end, call->end_ns);
    group.prefix_max_ends.push_back(max_end);
  }
}

std::string cardinality(std::size_t runtime_count,
                        std::size_t device_count) {
  if (runtime_count == 0 || device_count == 0) {
    return "open";
  }
  if (runtime_count == 1 && device_count == 1) {
    return "one_to_one";
  }
  if (runtime_count == 1) {
    return "one_to_many";
  }
  if (device_count == 1) {
    return "many_to_one";
  }
  return "many_to_many";
}

void append_relation(RuntimeDeviceSqlRows& rows, std::uint32_t db_idx,
                     std::string runtime_id, std::string work_id,
                     std::string relation_kind, std::string match_policy,
                     std::string evidence_level, std::string support_state,
                     std::size_t runtime_count, std::size_t device_count,
                     std::int64_t correlation_id, std::string raw_json = {}) {
  const std::size_t index = rows.relations.size();
  rows.relations.push_back(RuntimeDeviceRelationSqlRow{
      "runtime-device-relation-" + std::to_string(index),
      db_idx,
      std::move(runtime_id),
      std::move(work_id),
      std::move(relation_kind),
      std::move(match_policy),
      std::move(evidence_level),
      std::move(support_state),
      cardinality(runtime_count, device_count),
      static_cast<std::uint32_t>(runtime_count),
      static_cast<std::uint32_t>(device_count),
      nullable_i64(correlation_id),
      std::move(raw_json)});
}

const char* graph_match_policy_name(GraphLaunchMatchPolicy policy) {
  switch (policy) {
    case GraphLaunchMatchPolicy::kCudaRuntimeCorrelation:
      return "cuda_graph_launch_correlation";
    case GraphLaunchMatchPolicy::kNotifyCompletionAdjacent:
      return "ascend_notify_completion_adjacent";
    case GraphLaunchMatchPolicy::kNotifyOrderedFallback:
      return "ascend_notify_ordered_fallback";
    case GraphLaunchMatchPolicy::kUnmatched:
      return "unmatched_graph_launch";
  }
  return "unmatched_graph_launch";
}

}  // namespace

RuntimeDeviceSqlRows build_runtime_device_sql_rows(const NativeIr& ir,
                                                   std::uint32_t db_idx) {
  RuntimeDeviceSqlRows rows;
  rows.runtime_calls.reserve(ir.runtime_calls.size());

  std::map<CorrelationKey, std::vector<const RuntimeCallRow*>> calls_by_key;
  std::map<std::pair<SourceRefId::value_type, std::uint64_t>,
           std::vector<const RuntimeCallRow*>>
      calls_by_source;
  for (const RuntimeCallRow& call : ir.runtime_calls.rows()) {
    if (!call.source_ref_id.valid() ||
        call.source_ref_id.value() >= ir.source_refs.size()) {
      throw std::invalid_argument("RuntimeCallRow source_ref_id is out of range");
    }
    const SourceRefRow& source = ir.source_refs.row(call.source_ref_id);
    RuntimeCallSqlRow row;
    row.runtime_call_id = runtime_call_id(call.id);
    row.db_idx = db_idx;
    row.provider = provider_name(call.provider);
    row.clock_domain = clock_domain_name(call.clock_domain);
    row.source_table = source.table_name;
    row.source_key = std::to_string(call.source_row_id);
    row.start_ns = call.start_ns;
    row.end_ns = call.end_ns;
    row.dur_us = ns_to_us(call.end_ns - call.start_ns);
    row.api_name = symbol_value_or_empty(ir, call.api_name_symbol_id);
    row.api_type = symbol_value_or_empty(ir, call.api_type_symbol_id);
    row.process_id = nullable_i64(call.raw_process_id);
    row.thread_id = nullable_i64(call.raw_thread_id);
    row.global_tid = nullable_i64(call.raw_global_tid);
    row.context_id = nullable_i64(call.raw_context_id);
    row.device_id = call.has_device_id ? std::to_string(call.device_id)
                                       : std::string();
    row.correlation_id = nullable_i64(call.raw_correlation_id);
    row.match_policy = match_policy_name(call.match_policy);
    row.raw_json = source_json(source, "runtime_call");
    rows.runtime_calls.push_back(std::move(row));
    calls_by_source[{call.source_ref_id.value(), call.source_row_id}].push_back(
        &call);
    if (call.raw_correlation_id >= 0 &&
        call.match_policy != RuntimeCallMatchPolicy::kUnsupported) {
      calls_by_key[{call.provider, call.raw_correlation_id}].push_back(&call);
    }
  }

  std::map<CorrelationKey, std::vector<WorkRef>> works_by_key;
  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.source_ref_id.valid() ||
        task.source_ref_id.value() >= ir.source_refs.size()) {
      throw std::invalid_argument("TaskRow source_ref_id is out of range");
    }
    if (!task.trace_event_id.valid() ||
        task.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument("TaskRow trace_event_id is out of range");
    }
    const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
    const SourceRefRow& source = ir.source_refs.row(task.source_ref_id);
    const RuntimeCallProvider provider = task_provider(source);
    if (provider == RuntimeCallProvider::kUnknown ||
        host_only_task_source(source)) {
      continue;
    }
    DeviceWorkSqlRow work;
    work.device_work_id = event_work_id(event.id);
    work.db_idx = db_idx;
    work.provider = provider_name(provider);
    work.device_id = event.device_id;
    work.work_kind = "event";
    work.event_id = trace_event_compat_id(event.id);
    work.task_id = task_id(task.id);
    work.source_table = source.table_name;
    work.source_key = std::to_string(event.source_row_id);
    work.start_ns = event.start_ns;
    work.end_ns = event.end_ns;
    work.dur_us = ns_to_us(event.end_ns - event.start_ns);
    work.symbol = symbol_value_or_empty(
        ir, task.op_name_symbol_id.valid() ? task.op_name_symbol_id
                                          : event.raw_name_symbol_id);
    work.raw_json = source_json(source, "device_event");
    const std::size_t row_index = rows.device_works.size();
    rows.device_works.push_back(std::move(work));
    if (task.raw_connection_id >= 0) {
      works_by_key[{provider, task.raw_connection_id}].push_back(
          WorkRef{row_index, event.device_id});
    } else {
      append_relation(rows, db_idx, {}, rows.device_works.back().device_work_id,
                      "provider_correlation", "missing_provider_identifier",
                      "open", "missing_device_identifier", 0, 1, -1);
    }
  }

  std::set<CorrelationKey> keys;
  for (const auto& item : calls_by_key) keys.insert(item.first);
  for (const auto& item : works_by_key) keys.insert(item.first);
  for (const CorrelationKey& key : keys) {
    const auto call_it = calls_by_key.find(key);
    const auto work_it = works_by_key.find(key);
    const std::vector<const RuntimeCallRow*> calls =
        call_it == calls_by_key.end()
            ? std::vector<const RuntimeCallRow*>{}
            : call_it->second;
    const std::vector<WorkRef> works =
        work_it == works_by_key.end() ? std::vector<WorkRef>{}
                                      : work_it->second;
    if (calls.empty()) {
      for (const WorkRef& work : works) {
        append_relation(rows, db_idx, {},
                        rows.device_works[work.row_index].device_work_id,
                        "provider_correlation",
                        key.first == RuntimeCallProvider::kCuda
                            ? "cuda_correlation_id"
                            : "ascend_connection_id",
                        "open", "unmatched_device_work", 0, works.size(),
                        key.second);
      }
      continue;
    }
    if (works.empty()) {
      for (const RuntimeCallRow* call : calls) {
        append_relation(rows, db_idx, runtime_call_id(call->id), {},
                        "provider_correlation", match_policy_name(call->match_policy),
                        "open", "unmatched_runtime_call", calls.size(), 0,
                        key.second);
      }
      continue;
    }

    std::set<std::uint32_t> devices;
    for (const WorkRef& work : works) devices.insert(work.device_id);
    for (const RuntimeCallRow* call : calls) {
      for (const WorkRef& work : works) {
        std::string support = "supported_exact";
        std::string evidence = "direct_provider_identifier";
        if (calls.size() > 1) {
          support = "ambiguous_runtime_candidates";
          evidence = "candidate_provider_identifier";
        } else if (call->has_device_id && call->device_id != work.device_id) {
          support = "device_scope_mismatch";
          evidence = "rejected_provider_identifier";
        } else if (!call->has_device_id && devices.size() > 1) {
          support = "ambiguous_device_scope";
          evidence = "candidate_provider_identifier";
        }
        append_relation(rows, db_idx, runtime_call_id(call->id),
                        rows.device_works[work.row_index].device_work_id,
                        "provider_correlation", match_policy_name(call->match_policy),
                        evidence, support, calls.size(), works.size(), key.second);
      }
    }
  }

  // Calls without a usable provider identifier remain explicit rather than
  // disappearing and being mistaken for an observed call with no work.
  for (const RuntimeCallRow& call : ir.runtime_calls.rows()) {
    if (call.raw_correlation_id < 0 ||
        call.match_policy == RuntimeCallMatchPolicy::kUnsupported) {
      append_relation(rows, db_idx, runtime_call_id(call.id), {},
                      "provider_correlation", match_policy_name(call.match_policy),
                      "open", call.raw_correlation_id < 0
                                  ? "missing_runtime_identifier"
                                  : "unsupported_provider_schema",
                      1, 0, call.raw_correlation_id);
    }
  }

  // Exact graph launches are composite device-work objects. This relation is
  // independent of the per-member edges above and lets graph anchors map back
  // to their observed runtime launch directly.
  for (const GraphLaunchOccurrenceRow& launch :
       ir.graph_launch_occurrences.rows()) {
    if (!launch.source_ref_id.valid() ||
        launch.source_ref_id.value() >= ir.source_refs.size()) {
      throw std::invalid_argument(
          "GraphLaunchOccurrenceRow source_ref_id is out of range");
    }
    const SourceRefRow& source = ir.source_refs.row(launch.source_ref_id);
    const SourceRefRow* work_source = &source;
    DeviceWorkSqlRow work;
    work.device_work_id = graph_work_id(launch.id);
    work.db_idx = db_idx;
    work.provider = provider_name(graph_provider(launch, source));
    work.device_id = launch.device_id;
    work.work_kind = "graph_launch";
    work.graph_launch_occurrence_id = launch.id.value();
    // A graph-launch DeviceWork row locates the device-side execution
    // evidence, not its host endpoint. Ascend retains that row through the
    // model-execute task; CUDA represents the launch itself in the runtime
    // source table and therefore legitimately uses the host API row.
    if (launch.model_execute_task_id.valid()) {
      if (launch.model_execute_task_id.value() >= ir.tasks.size()) {
        throw std::invalid_argument(
            "GraphLaunchOccurrenceRow model_execute_task_id is out of range");
      }
      const TaskRow& execute_task =
          ir.tasks.row(launch.model_execute_task_id);
      if (!execute_task.source_ref_id.valid() ||
          execute_task.source_ref_id.value() >= ir.source_refs.size()) {
        throw std::invalid_argument(
            "graph launch model-execute task has invalid source_ref_id");
      }
      work_source = &ir.source_refs.row(execute_task.source_ref_id);
      if (!execute_task.trace_event_id.valid() ||
          execute_task.trace_event_id.value() >= ir.trace_events.size()) {
        throw std::invalid_argument(
            "graph launch model-execute task has invalid trace_event_id");
      }
      const TraceEventRow& execute_event =
          ir.trace_events.row(execute_task.trace_event_id);
      work.source_key = std::to_string(execute_event.source_row_id);
    } else {
      work.source_key = launch.raw_host_api_row_id >= 0
                            ? std::to_string(launch.raw_host_api_row_id)
                            : std::to_string(launch.id.value());
    }
    work.start_ns = launch.start_ns;
    work.end_ns = launch.end_ns;
    work.dur_us = ns_to_us(launch.end_ns - launch.start_ns);
    work.symbol = "graph_launch";
    work.source_table = work_source->table_name;
    work.raw_json = source_json(*work_source, "graph_launch");
    rows.device_works.push_back(work);

    std::vector<const RuntimeCallRow*> calls;
    if (launch.host_api_source_ref_id.valid() &&
        launch.host_api_source_ref_id.value() >= ir.source_refs.size()) {
      throw std::invalid_argument(
          "GraphLaunchOccurrenceRow host_api_source_ref_id is out of range");
    }
    if (launch.host_api_source_ref_id.valid() &&
        launch.raw_host_api_row_id >= 0) {
      const auto found = calls_by_source.find(
          {launch.host_api_source_ref_id.value(),
           static_cast<std::uint64_t>(launch.raw_host_api_row_id)});
      if (found != calls_by_source.end()) calls = found->second;
    }
    const std::string policy = graph_match_policy_name(launch.match_policy);
    if (calls.empty() ||
        launch.match_policy == GraphLaunchMatchPolicy::kUnmatched) {
      append_relation(rows, db_idx, {}, work.device_work_id, "graph_launch",
                      policy, "open", "unmatched_graph_launch", 0, 1,
                      launch.raw_launch_connection_id);
    } else if (calls.size() > 1) {
      for (const RuntimeCallRow* call : calls) {
        append_relation(rows, db_idx, runtime_call_id(call->id),
                        work.device_work_id, "graph_launch", policy,
                        "candidate_adapter_relation",
                        "ambiguous_runtime_candidates", calls.size(), 1,
                        launch.raw_launch_connection_id);
      }
    } else {
      const bool direct = launch.match_policy ==
                          GraphLaunchMatchPolicy::kCudaRuntimeCorrelation;
      append_relation(rows, db_idx, runtime_call_id(calls.front()->id),
                      work.device_work_id, "graph_launch", policy,
                      direct ? "exact_provider_identifier"
                             : "validated_adapter_relation",
                      direct ? "supported_exact" : "supported_deterministic",
                      1, 1, launch.raw_launch_connection_id);
    }
  }

  std::map<std::string, std::vector<const RuntimeDeviceRelationSqlRow*>>
      relations_by_work;
  std::map<std::string, const RuntimeDeviceRelationSqlRow*> relation_by_id;
  for (const RuntimeDeviceRelationSqlRow& relation : rows.relations) {
    relation_by_id.emplace(relation.relation_id, &relation);
    if (!relation.device_work_id.empty()) {
      relations_by_work[relation.device_work_id].push_back(&relation);
    }
  }

  std::map<ReplayUnitLaunchMemberId::value_type, GraphLaunchOccurrenceId>
      occurrence_by_launch_member;
  for (const ReplayUnitLaunchMemberRow& member :
       ir.replay_unit_launch_members.rows()) {
    if (member.graph_launch_occurrence_id.valid()) {
      occurrence_by_launch_member.emplace(member.id.value(),
                                          member.graph_launch_occurrence_id);
    }
  }

  for (const AnchorRow& anchor : ir.anchors.rows()) {
    std::vector<std::pair<std::string, std::string>> endpoints;
    if (anchor.trace_event_id.valid()) {
      endpoints.emplace_back("event", event_work_id(anchor.trace_event_id));
    }
    if (anchor.replay_unit_launch_member_id.valid()) {
      const auto found = occurrence_by_launch_member.find(
          anchor.replay_unit_launch_member_id.value());
      if (found != occurrence_by_launch_member.end()) {
        endpoints.emplace_back("graph_launch", graph_work_id(found->second));
      }
    }
    std::set<std::string> emitted_relations;
    for (const auto& endpoint : endpoints) {
      const auto found = relations_by_work.find(endpoint.second);
      if (found == relations_by_work.end()) continue;
      for (const RuntimeDeviceRelationSqlRow* relation : found->second) {
        if (!emitted_relations.insert(relation->relation_id).second) continue;
        rows.anchor_relations.push_back(AnchorRuntimeRelationSqlRow{
            anchor_compat_id(anchor.id), relation->relation_id,
            relation->runtime_call_id, relation->device_work_id,
            endpoint.first});
      }
    }
  }

  std::map<std::string, const RuntimeCallSqlRow*> call_by_id;
  for (const RuntimeCallSqlRow& call : rows.runtime_calls) {
    call_by_id.emplace(call.runtime_call_id, &call);
  }
  std::map<std::string, std::set<std::string>> supported_calls;
  for (const AnchorRuntimeRelationSqlRow& link : rows.anchor_relations) {
    const auto relation_found = relation_by_id.find(link.relation_id);
    if (relation_found == relation_by_id.end()) continue;
    const RuntimeDeviceRelationSqlRow& relation = *relation_found->second;
    if ((relation.support_state == "supported_exact" ||
         relation.support_state == "supported_deterministic") &&
        !link.runtime_call_id.empty()) {
      supported_calls[link.anchor_id].insert(link.runtime_call_id);
    }
  }

  std::map<std::uint32_t, std::vector<const AnchorRow*>> anchors_by_device;
  for (const AnchorRow& anchor : ir.anchors.rows()) {
    anchors_by_device[anchor.device_id].push_back(&anchor);
  }
  for (const auto& device_entry : anchors_by_device) {
    const std::vector<const AnchorRow*>& anchors = device_entry.second;
    for (std::size_t index = 1; index < anchors.size(); ++index) {
      const AnchorRow& left = *anchors[index - 1];
      const AnchorRow& right = *anchors[index];
      const std::set<std::string>& left_ids =
          supported_calls[anchor_compat_id(left.id)];
      const std::set<std::string>& right_ids =
          supported_calls[anchor_compat_id(right.id)];
      const RuntimeCallSqlRow* left_call = nullptr;
      const RuntimeCallSqlRow* right_call = nullptr;
      if (left_ids.size() == 1) left_call = call_by_id.at(*left_ids.begin());
      if (right_ids.size() == 1) right_call = call_by_id.at(*right_ids.begin());

      AnchorHostIntervalSqlRow interval;
      interval.interval_id = "anchor-host-interval-" +
                             anchor_compat_id(left.id) + "-" +
                             anchor_compat_id(right.id);
      interval.db_idx = db_idx;
      interval.device_id = device_entry.first;
      interval.left_anchor_id = anchor_compat_id(left.id);
      interval.right_anchor_id = anchor_compat_id(right.id);
      interval.left_endpoint_count = left_ids.size();
      interval.right_endpoint_count = right_ids.size();
      if (left_call != nullptr) {
        interval.left_runtime_call_id = left_call->runtime_call_id;
        interval.provider = left_call->provider;
        interval.clock_domain = left_call->clock_domain;
        interval.host_start_ns = std::to_string(left_call->end_ns);
      }
      if (right_call != nullptr) {
        interval.right_runtime_call_id = right_call->runtime_call_id;
        interval.host_end_ns = std::to_string(right_call->start_ns);
      }

      const bool process_compatible_for_thread =
          left_call != nullptr && right_call != nullptr &&
          ((left_call->process_id.empty() && right_call->process_id.empty()) ||
           (!left_call->process_id.empty() &&
            left_call->process_id == right_call->process_id));
      const bool compatible_thread =
          left_call != nullptr && right_call != nullptr &&
          !left_call->thread_id.empty() &&
          left_call->thread_id == right_call->thread_id &&
          process_compatible_for_thread;
      const bool compatible_process =
          left_call != nullptr && right_call != nullptr &&
          !left_call->process_id.empty() &&
          left_call->process_id == right_call->process_id;
      interval.scope_policy = compatible_thread
                                  ? "same_thread"
                                  : (compatible_process
                                         ? "same_process"
                                         : "provider_clock_domain");
      if (compatible_process) interval.process_id = left_call->process_id;
      if (compatible_thread) interval.thread_id = left_call->thread_id;

      if (left_ids.empty() || right_ids.empty()) {
        interval.support_state = "missing_endpoint";
      } else if (left_ids.size() > 1 || right_ids.size() > 1) {
        interval.support_state = "ambiguous_endpoint";
      } else if (left_call->provider != right_call->provider ||
                 left_call->clock_domain != right_call->clock_domain) {
        interval.support_state = "incompatible_host_domain";
      } else if (left_call->end_ns > right_call->start_ns) {
        interval.support_state = "nonmonotonic_host_order";
      } else {
        interval.support_state = "supported_ordered";
      }
      rows.host_intervals.push_back(std::move(interval));
    }
  }

  std::map<ActivityGroupKey, ActivityGroup> activity_groups;
  for (const RuntimeCallSqlRow& call : rows.runtime_calls) {
    activity_groups[activity_group_key(call, "provider_clock_domain")]
        .calls.push_back(&call);
    if (!call.process_id.empty()) {
      activity_groups[activity_group_key(call, "same_process")]
          .calls.push_back(&call);
    }
    if (!call.thread_id.empty()) {
      activity_groups[{call.provider, call.clock_domain, {}, call.thread_id}]
          .calls.push_back(&call);
      if (!call.process_id.empty()) {
        activity_groups[activity_group_key(call, "same_thread")]
            .calls.push_back(&call);
      }
    }
  }
  for (auto& entry : activity_groups) finalize_activity_group(entry.second);

  for (const AnchorHostIntervalSqlRow& interval : rows.host_intervals) {
    if (interval.support_state != "supported_ordered") continue;
    const std::int64_t host_start = std::stoll(interval.host_start_ns);
    const std::int64_t host_end = std::stoll(interval.host_end_ns);
    const auto group_found = activity_groups.find(activity_group_key(interval));
    if (group_found == activity_groups.end()) continue;
    const ActivityGroup& group = group_found->second;
    const auto hi_it =
        std::lower_bound(group.starts.begin(), group.starts.end(), host_end);
    const std::size_t hi =
        static_cast<std::size_t>(hi_it - group.starts.begin());
    const auto lo_it = std::upper_bound(group.prefix_max_ends.begin(),
                                        group.prefix_max_ends.begin() + hi,
                                        host_start);
    const std::size_t lo =
        static_cast<std::size_t>(lo_it - group.prefix_max_ends.begin());
    std::uint32_t observed_order = 0;
    for (std::size_t index = lo; index < hi; ++index) {
      const RuntimeCallSqlRow& call = *group.calls[index];
      if (call.end_ns <= host_start) continue;
      rows.host_activities.push_back(AnchorHostActivitySqlRow{
          interval.interval_id, call.runtime_call_id, observed_order++});
    }
  }

  return rows;
}

}  // namespace traceloom::compat
