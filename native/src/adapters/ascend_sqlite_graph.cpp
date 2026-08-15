#include "ascend_sqlite_internal.h"

#include "traceloom/analysis/exact_periodic_suffix.h"
#include "traceloom/runtime/thread_pool.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace traceloom::ascend_sqlite_detail {
bool event_overlaps(const TraceEventRow& event,
                    std::int64_t start_ns,
                    std::int64_t end_ns) {
  return event.start_ns <= end_ns && event.end_ns >= start_ns;
}

std::vector<GraphTaskView> controls_with_symbol_set(
    const std::vector<GraphTaskView>& controls,
    const std::unordered_set<std::uint32_t>& task_type_symbols) {
  std::vector<GraphTaskView> out;
  for (const GraphTaskView& row : controls) {
    if (symbol_in_set(task_type_symbols, row.task->task_type_symbol_id)) {
      out.push_back(row);
    }
  }
  return out;
}

std::vector<GraphTaskView> controls_in_interval_from_sorted(
    const std::vector<GraphTaskView>& controls,
    std::int64_t start_ns,
    std::int64_t end_ns,
    std::size_t& cursor) {
  while (cursor < controls.size() && controls[cursor].event->end_ns < start_ns) {
    ++cursor;
  }
  std::vector<GraphTaskView> out;
  std::size_t scan = cursor;
  while (scan < controls.size() && controls[scan].event->start_ns <= end_ns) {
    if (event_overlaps(*controls[scan].event, start_ns, end_ns)) {
      out.push_back(controls[scan]);
    }
    ++scan;
  }
  return out;
}

std::uint64_t absolute_timestamp_delta(std::int64_t lhs, std::int64_t rhs) {
  static constexpr std::uint64_t kSignBit = std::uint64_t{1} << 63u;
  const std::uint64_t ordered_lhs = static_cast<std::uint64_t>(lhs) ^ kSignBit;
  const std::uint64_t ordered_rhs = static_cast<std::uint64_t>(rhs) ^ kSignBit;
  return ordered_lhs >= ordered_rhs ? ordered_lhs - ordered_rhs
                                    : ordered_rhs - ordered_lhs;
}

void materialize_aclgraph_launch_occurrences(
    NativeIr& ir,
    const StreamIndex& streams,
    const CapturedGraphInstanceIndexes& captured_graph_instances,
    const std::vector<GraphLaunchView>& host_execute_launches,
    SourceRefId host_api_source_ref) {
  const GraphTaskSymbolSets graph_symbols =
      build_graph_task_symbol_sets(ir.symbols);
  std::vector<GraphTaskView> model_executes;
  std::vector<GraphTaskView> notify_waits;
  std::vector<GraphTaskView> notify_records;
  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
    const GraphTaskView view{&task, &event};
    if (symbol_in_set(graph_symbols.model_execute,
                      task.task_type_symbol_id)) {
      model_executes.push_back(view);
    } else if (symbol_in_set(graph_symbols.notify_wait,
                             task.task_type_symbol_id)) {
      notify_waits.push_back(view);
    } else if (symbol_in_set(graph_symbols.notify_record,
                             task.task_type_symbol_id)) {
      notify_records.push_back(view);
    }
  }
  if (model_executes.empty()) {
    return;
  }

  const auto graph_task_less = [](const GraphTaskView& lhs,
                                  const GraphTaskView& rhs) {
    if (lhs.event->start_ns != rhs.event->start_ns) {
      return lhs.event->start_ns < rhs.event->start_ns;
    }
    if (lhs.event->end_ns != rhs.event->end_ns) {
      return lhs.event->end_ns < rhs.event->end_ns;
    }
    if (lhs.event->device_id != rhs.event->device_id) {
      return lhs.event->device_id < rhs.event->device_id;
    }
    return lhs.task->id < rhs.task->id;
  };
  std::sort(model_executes.begin(), model_executes.end(), graph_task_less);
  std::sort(notify_waits.begin(), notify_waits.end(), graph_task_less);
  std::sort(notify_records.begin(), notify_records.end(), graph_task_less);

  struct WorkingLaunch {
    GraphTaskView execute;
    const GraphLaunchView* host_execute = nullptr;
    const GraphTaskView* wait = nullptr;
    const GraphTaskView* record = nullptr;
    GraphLaunchMatchPolicy policy = GraphLaunchMatchPolicy::kUnmatched;
  };
  std::vector<WorkingLaunch> launches;
  launches.reserve(model_executes.size());

  std::unordered_map<std::int64_t, std::vector<const GraphLaunchView*>>
      host_by_connection;
  for (const GraphLaunchView& host : host_execute_launches) {
    host_by_connection[host.connection_id].push_back(&host);
  }
  std::vector<bool> wait_claimed(notify_waits.size(), false);
  for (const GraphTaskView& execute : model_executes) {
    WorkingLaunch launch;
    launch.execute = execute;
    const auto host_found =
        host_by_connection.find(execute.task->raw_connection_id);
    if (host_found != host_by_connection.end()) {
      for (const GraphLaunchView* candidate : host_found->second) {
        if (launch.host_execute == nullptr ||
            absolute_timestamp_delta(candidate->end_ns,
                                     execute.event->start_ns) <
                absolute_timestamp_delta(launch.host_execute->end_ns,
                                         execute.event->start_ns)) {
          launch.host_execute = candidate;
        }
      }
    }

    std::size_t best_wait = notify_waits.size();
    std::uint64_t best_delta = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t index = 0; index < notify_waits.size(); ++index) {
      if (wait_claimed[index]) {
        continue;
      }
      const GraphTaskView& candidate = notify_waits[index];
      if (candidate.event->device_id != execute.event->device_id ||
          candidate.task->raw_connection_id !=
              execute.task->raw_connection_id) {
        continue;
      }
      const std::uint64_t delta = absolute_timestamp_delta(
          candidate.event->start_ns, execute.event->start_ns);
      if (delta < best_delta) {
        best_wait = index;
        best_delta = delta;
      }
    }
    if (best_wait != notify_waits.size()) {
      wait_claimed[best_wait] = true;
      launch.wait = &notify_waits[best_wait];
    }
    launches.push_back(launch);
  }

  struct CompletionCandidate {
    std::size_t launch_index = 0;
    std::size_t record_index = 0;
    std::uint64_t absolute_delta_ns = 0;
  };
  static constexpr std::uint64_t kCompletionAdjacencyThresholdNs = 10'000;
  std::vector<CompletionCandidate> candidates;
  for (std::size_t launch_index = 0; launch_index < launches.size();
       ++launch_index) {
    const WorkingLaunch& launch = launches[launch_index];
    if (launch.wait == nullptr) {
      continue;
    }
    for (std::size_t record_index = 0; record_index < notify_records.size();
         ++record_index) {
      const GraphTaskView& record = notify_records[record_index];
      if (record.event->device_id != launch.execute.event->device_id) {
        continue;
      }
      const std::uint64_t delta = absolute_timestamp_delta(
          launch.wait->event->end_ns, record.event->end_ns);
      if (delta <= kCompletionAdjacencyThresholdNs) {
        candidates.push_back(
            CompletionCandidate{launch_index, record_index, delta});
      }
    }
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const CompletionCandidate& lhs,
               const CompletionCandidate& rhs) {
              if (lhs.absolute_delta_ns != rhs.absolute_delta_ns) {
                return lhs.absolute_delta_ns < rhs.absolute_delta_ns;
              }
              if (lhs.launch_index != rhs.launch_index) {
                return lhs.launch_index < rhs.launch_index;
              }
              return lhs.record_index < rhs.record_index;
            });
  std::vector<bool> record_claimed(notify_records.size(), false);
  for (const CompletionCandidate& candidate : candidates) {
    WorkingLaunch& launch = launches[candidate.launch_index];
    if (launch.record != nullptr || record_claimed[candidate.record_index]) {
      continue;
    }
    launch.record = &notify_records[candidate.record_index];
    launch.policy = GraphLaunchMatchPolicy::kNotifyCompletionAdjacent;
    record_claimed[candidate.record_index] = true;
  }

  std::map<std::uint32_t, std::vector<std::size_t>> unmatched_by_device;
  std::map<std::uint32_t, std::vector<std::size_t>> records_by_device;
  for (std::size_t index = 0; index < launches.size(); ++index) {
    const WorkingLaunch& launch = launches[index];
    if (launch.wait != nullptr && launch.record == nullptr) {
      unmatched_by_device[launch.execute.event->device_id].push_back(index);
    }
  }
  for (std::size_t index = 0; index < notify_records.size(); ++index) {
    if (!record_claimed[index]) {
      records_by_device[notify_records[index].event->device_id].push_back(
          index);
    }
  }
  for (const auto& item : unmatched_by_device) {
    const auto records_found = records_by_device.find(item.first);
    if (records_found == records_by_device.end() ||
        item.second.size() != records_found->second.size()) {
      continue;
    }
    for (std::size_t index = 0; index < item.second.size(); ++index) {
      WorkingLaunch& launch = launches[item.second[index]];
      const std::size_t record_index = records_found->second[index];
      launch.record = &notify_records[record_index];
      launch.policy = GraphLaunchMatchPolicy::kNotifyOrderedFallback;
      record_claimed[record_index] = true;
    }
  }

  ir.graph_launch_occurrences.reserve(launches.size());
  for (const WorkingLaunch& launch : launches) {
    const TraceEventRow& execute_event = *launch.execute.event;
    const auto execute_stream =
        streams.find(stream_key(execute_event.device_id,
                                execute_event.stream_id));
    StreamId model_stream_id = StreamId::invalid();
    CapturedGraphInstanceId captured_graph_instance_id =
        CapturedGraphInstanceId::invalid();
    GraphLaunchInstanceAssociationPolicy instance_association_policy =
        GraphLaunchInstanceAssociationPolicy::kNone;
    TaskId wait_task_id = TaskId::invalid();
    TaskId record_task_id = TaskId::invalid();
    std::int64_t raw_graph_connection_id = -1;
    std::int64_t raw_model_id = -1;
    std::int64_t end_ns = execute_event.end_ns;
    std::int64_t wait_record_delta_ns = -1;
    if (launch.wait != nullptr) {
      wait_task_id = launch.wait->task->id;
      end_ns = std::max(end_ns, launch.wait->event->end_ns);
    }
    if (launch.record != nullptr) {
      record_task_id = launch.record->task->id;
      raw_graph_connection_id = launch.record->task->raw_connection_id;
      raw_model_id = launch.record->task->raw_model_id;
      if (raw_model_id >= 0) {
        const auto instance = captured_graph_instances.by_model_id.find(
            CapturedGraphInstanceKey{execute_event.device_id, raw_model_id});
        if (instance != captured_graph_instances.by_model_id.end()) {
          captured_graph_instance_id = instance->second;
          instance_association_policy =
              GraphLaunchInstanceAssociationPolicy::kRecordModelId;
        }
      }
      end_ns = std::max(end_ns, launch.record->event->end_ns);
      wait_record_delta_ns =
          launch.wait->event->end_ns - launch.record->event->end_ns;
      const auto model_stream = streams.find(stream_key(
          launch.record->event->device_id, launch.record->event->stream_id));
      if (model_stream != streams.end()) {
        model_stream_id = model_stream->second;
      }
      if (!captured_graph_instance_id.valid()) {
        const auto instance = captured_graph_instances.by_model_stream.find(
            CapturedGraphModelStreamKey{launch.record->event->device_id,
                                        launch.record->event->stream_id});
        if (instance != captured_graph_instances.by_model_stream.end() &&
            instance->second.valid()) {
          captured_graph_instance_id = instance->second;
          instance_association_policy =
              GraphLaunchInstanceAssociationPolicy::kRecordModelStream;
        }
      }
    }
    ir.graph_launch_occurrences.append(
        launch.execute.task->source_ref_id, host_api_source_ref,
        execute_event.device_id,
        launch.host_execute == nullptr ? -1 : launch.host_execute->raw_row_id,
        launch.execute.task->raw_connection_id, raw_graph_connection_id,
        raw_model_id,
        execute_stream == streams.end() ? StreamId::invalid()
                                        : execute_stream->second,
        model_stream_id, captured_graph_instance_id, launch.execute.task->id,
        wait_task_id, record_task_id, execute_event.start_ns, end_ns,
        wait_record_delta_ns, launch.policy, instance_association_policy);
  }
}

void materialize_graph_launch_activities(
    NativeIr& ir,
    const std::vector<GraphLaunchActivityView>& activities,
    SourceRefId host_api_source_ref) {
  if (!host_api_source_ref.valid()) {
    return;
  }
  std::unordered_map<std::int64_t,
                     std::vector<GraphLaunchOccurrenceId>>
      launches_by_host_row;
  for (const GraphLaunchOccurrenceRow& launch :
       ir.graph_launch_occurrences.rows()) {
    if (launch.raw_host_api_row_id >= 0) {
      launches_by_host_row[launch.raw_host_api_row_id].push_back(launch.id);
    }
  }

  for (const GraphLaunchActivityView& activity : activities) {
    std::uint32_t matched_launch_count = 0;
    for (std::int64_t row_id : activity.host_execute_row_ids) {
      const auto found = launches_by_host_row.find(row_id);
      if (found != launches_by_host_row.end()) {
        matched_launch_count +=
            static_cast<std::uint32_t>(found->second.size());
      }
    }
    const GraphLaunchActivityId activity_id =
        ir.graph_launch_activities.append(
            host_api_source_ref, activity.raw_global_tid,
            activity.first_host_api_row_id, activity.last_host_api_row_id,
            activity.boundary_host_api_row_id,
            activity.boundary_api_name.empty()
                ? SymbolId::invalid()
                : ir.symbols.intern(activity.boundary_api_name),
            activity.start_ns, activity.end_ns,
            static_cast<std::uint32_t>(
                activity.host_execute_row_ids.size()),
            matched_launch_count, activity.boundary_policy);
    for (std::size_t order = 0;
         order < activity.host_execute_row_ids.size(); ++order) {
      const auto found =
          launches_by_host_row.find(activity.host_execute_row_ids[order]);
      if (found == launches_by_host_row.end()) {
        continue;
      }
      for (GraphLaunchOccurrenceId launch_id : found->second) {
        ir.graph_launch_activity_members.append(
            activity_id, launch_id, static_cast<std::uint32_t>(order));
      }
    }
  }
}

std::set<GraphLaunchOccurrenceId> materialize_graph_launch_bodies(
    NativeIr& ir,
    bool compute_identity_source,
    bool communication_identity_source) {
  std::unordered_map<std::uint64_t, std::vector<GraphTaskView>>
      tasks_by_stream;
  std::unordered_map<std::uint64_t, std::vector<GraphTaskView>>
      normalized_tasks_by_stream;
  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
    tasks_by_stream[stream_key(event.device_id, event.stream_id)].push_back(
        GraphTaskView{&task, &event});
    if (!task.op_type_symbol_id.valid() && !task.op_name_symbol_id.valid() &&
        !task.comm_name_symbol_id.valid()) {
      continue;
    }
    normalized_tasks_by_stream[stream_key(event.device_id, event.stream_id)]
        .push_back(GraphTaskView{&task, &event});
  }
  const auto task_order = [](const GraphTaskView& lhs,
                             const GraphTaskView& rhs) {
    if (lhs.event->start_ns != rhs.event->start_ns) {
      return lhs.event->start_ns < rhs.event->start_ns;
    }
    if (lhs.event->end_ns != rhs.event->end_ns) {
      return lhs.event->end_ns < rhs.event->end_ns;
    }
    return lhs.task->id < rhs.task->id;
  };
  for (auto& item : normalized_tasks_by_stream) {
    std::sort(item.second.begin(), item.second.end(), task_order);
  }
  for (auto& item : tasks_by_stream) {
    std::sort(item.second.begin(), item.second.end(), task_order);
  }

  struct StreamBody {
    std::uint64_t raw_stream_id = 0;
    std::vector<const GraphTaskView*> tasks;
    std::string exact_sequence;
    std::string readable_sequence;
  };

  std::map<std::string, ReplayBodyTemplateId> templates_by_topology;
  std::set<GraphLaunchOccurrenceId> missing_body_capability_launches;
  for (const GraphLaunchOccurrenceRow& launch :
       ir.graph_launch_occurrences.rows()) {
    if (launch.raw_graph_connection_id >= 0 &&
        !launch.captured_graph_instance_id.valid()) {
      missing_body_capability_launches.insert(launch.id);
    }
    if (!launch.model_stream_id.valid()) {
      continue;
    }

    ReplayBodyTopologyPolicy topology_policy =
        ReplayBodyTopologyPolicy::kSingleModelStream;
    std::vector<std::uint64_t> body_stream_ids;
    if (launch.captured_graph_instance_id.valid()) {
      if (launch.captured_graph_instance_id.value() >=
          ir.captured_graph_instances.size()) {
        throw std::invalid_argument(
            "graph launch references invalid captured graph instance");
      }
      const CapturedGraphInstanceRow& instance =
          ir.captured_graph_instances.row(launch.captured_graph_instance_id);
      for (const CapturedGraphStreamRow& stream :
           ir.captured_graph_streams.rows()) {
        if (stream.captured_graph_instance_id == instance.id) {
          body_stream_ids.push_back(stream.raw_model_stream_id);
        }
      }
      std::sort(body_stream_ids.begin(), body_stream_ids.end());
      body_stream_ids.erase(
          std::unique(body_stream_ids.begin(), body_stream_ids.end()),
          body_stream_ids.end());
      if (body_stream_ids.empty() ||
          body_stream_ids.size() != instance.model_stream_count) {
        continue;
      }
      const StreamRow& launch_model_stream =
          ir.streams.row(launch.model_stream_id);
      if (!std::binary_search(body_stream_ids.begin(), body_stream_ids.end(),
                              launch_model_stream.raw_stream_id)) {
        continue;
      }
      topology_policy =
          ReplayBodyTopologyPolicy::kCapturedStreamSetUnordered;
    } else {
      body_stream_ids.push_back(
          ir.streams.row(launch.model_stream_id).raw_stream_id);
    }

    std::vector<StreamBody> stream_bodies;
    stream_bodies.reserve(body_stream_ids.size());
    std::vector<const GraphTaskView*> body_tasks;
    bool launch_identity_coverage = true;
    for (std::uint64_t raw_stream_id : body_stream_ids) {
      StreamBody stream_body;
      stream_body.raw_stream_id = raw_stream_id;
      if (!compute_identity_source || !communication_identity_source) {
        const auto all_tasks = tasks_by_stream.find(
            stream_key(launch.device_id, raw_stream_id));
        if (all_tasks != tasks_by_stream.end()) {
          auto task = std::lower_bound(
              all_tasks->second.begin(), all_tasks->second.end(),
              launch.start_ns,
              [](const GraphTaskView& row, std::int64_t start_ns) {
                return row.event->start_ns < start_ns;
              });
          for (; task != all_tasks->second.end() &&
                 task->event->start_ns <= launch.end_ns;
               ++task) {
            if (task->event->end_ns > launch.end_ns ||
                (launch.raw_model_id >= 0 &&
                 task->task->raw_model_id != launch.raw_model_id) ||
                task->task->id == launch.model_execute_task_id ||
                task->task->id == launch.notify_wait_task_id ||
                task->task->id == launch.notify_record_task_id ||
                task->task->op_type_symbol_id.valid() ||
                task->task->op_name_symbol_id.valid() ||
                task->task->comm_name_symbol_id.valid()) {
              continue;
            }
            const std::string task_key = normalize_key(symbol_value_or_empty(
                ir, task->task->task_type_symbol_id));
            if (!graph_body_infrastructure_task_key(task_key)) {
              launch_identity_coverage = false;
              break;
            }
          }
        }
      }
      const auto found = normalized_tasks_by_stream.find(
          stream_key(launch.device_id, raw_stream_id));
      if (found != normalized_tasks_by_stream.end()) {
        const std::vector<GraphTaskView>& tasks = found->second;
        auto task = std::lower_bound(
            tasks.begin(), tasks.end(), launch.start_ns,
            [](const GraphTaskView& row, std::int64_t start_ns) {
              return row.event->start_ns < start_ns;
            });
        for (; task != tasks.end() && task->event->start_ns <= launch.end_ns;
             ++task) {
          if (task->event->end_ns > launch.end_ns ||
              (launch.raw_model_id >= 0 &&
               task->task->raw_model_id != launch.raw_model_id)) {
            continue;
          }
          stream_body.tasks.push_back(&*task);
          body_tasks.push_back(&*task);
        }
      }
      for (const GraphTaskView* row : stream_body.tasks) {
        const TaskRow& task = *row->task;
        const bool is_communication =
            graph_body_task_is_communication(task);
        std::string op;
        if (is_communication) {
          op = ir.symbols.value(task.comm_name_symbol_id);
          const std::size_t suffix = op.find("__");
          if (suffix != std::string::npos) {
            op.resize(suffix);
          }
          op = "comm:" + op;
          if (task.communication_task_type_symbol_id.valid()) {
            op += "/" +
                  ir.symbols.value(task.communication_task_type_symbol_id);
          }
        } else {
          op = task.op_type_symbol_id.valid()
                   ? ir.symbols.value(task.op_type_symbol_id)
                   : ir.symbols.value(task.op_name_symbol_id);
        }
        if (!stream_body.readable_sequence.empty()) {
          stream_body.readable_sequence += "\n";
        }
        stream_body.readable_sequence += op;
        stream_body.exact_sequence += is_communication ? "communication\t"
                                                       : "compute\t";
        stream_body.exact_sequence += op;
        stream_body.exact_sequence += "\t";
        if (task.compute_task_type_symbol_id.valid()) {
          stream_body.exact_sequence +=
              ir.symbols.value(task.compute_task_type_symbol_id);
        } else if (task.communication_task_type_symbol_id.valid()) {
          stream_body.exact_sequence +=
              ir.symbols.value(task.communication_task_type_symbol_id);
        }
        stream_body.exact_sequence += "\t";
        if (task.task_type_symbol_id.valid()) {
          stream_body.exact_sequence +=
              ir.symbols.value(task.task_type_symbol_id);
        }
        stream_body.exact_sequence += "\n";
      }
      stream_bodies.push_back(std::move(stream_body));
    }
    if (!launch_identity_coverage) {
      missing_body_capability_launches.insert(launch.id);
      continue;
    }
    if (body_tasks.empty()) {
      continue;
    }

    std::sort(stream_bodies.begin(), stream_bodies.end(),
              [](const StreamBody& lhs, const StreamBody& rhs) {
                if (lhs.exact_sequence != rhs.exact_sequence) {
                  return lhs.exact_sequence < rhs.exact_sequence;
                }
                return lhs.raw_stream_id < rhs.raw_stream_id;
              });
    std::string exact_topology =
        topology_policy ==
                ReplayBodyTopologyPolicy::kCapturedStreamSetUnordered
            ? "captured_stream_set_unordered\n"
            : "single_model_stream\n";
    exact_topology += "stream_count=" +
                      std::to_string(stream_bodies.size()) + "\n";
    std::string readable_topology;
    for (std::size_t lane = 0; lane < stream_bodies.size(); ++lane) {
      exact_topology += "lane_begin\n";
      exact_topology += stream_bodies[lane].exact_sequence;
      exact_topology += "lane_end\n";
      if (stream_bodies.size() == 1) {
        readable_topology = stream_bodies[lane].readable_sequence;
        continue;
      }
      if (!readable_topology.empty()) {
        readable_topology += "\n";
      }
      readable_topology += "lane " + std::to_string(lane) + ":\n";
      readable_topology += stream_bodies[lane].readable_sequence.empty()
                               ? "<no normalized compute>"
                               : stream_bodies[lane].readable_sequence;
    }
    std::sort(body_tasks.begin(), body_tasks.end(),
              [&](const GraphTaskView* lhs, const GraphTaskView* rhs) {
                return task_order(*lhs, *rhs);
              });
    const std::uint32_t communication_task_count =
        static_cast<std::uint32_t>(std::count_if(
            body_tasks.begin(), body_tasks.end(),
            [](const GraphTaskView* row) {
              return graph_body_task_is_communication(*row->task);
            }));
    const std::uint32_t compute_task_count =
        static_cast<std::uint32_t>(body_tasks.size()) -
        communication_task_count;

    ReplayBodyTemplateId template_id = ReplayBodyTemplateId::invalid();
    const auto existing = templates_by_topology.find(exact_topology);
    if (existing == templates_by_topology.end()) {
      template_id = ir.replay_body_templates.append(
          body_tasks.front()->task->source_ref_id,
          stable_hash64(exact_topology), ir.symbols.intern(readable_topology),
          compute_task_count, communication_task_count,
          static_cast<std::uint32_t>(stream_bodies.size()), topology_policy);
      templates_by_topology.emplace(std::move(exact_topology), template_id);
    } else {
      template_id = existing->second;
    }
    const GraphLaunchBodyId body_id = ir.graph_launch_bodies.append(
        launch.id, template_id, body_tasks.front()->task->id,
        body_tasks.back()->task->id,
        compute_task_count, communication_task_count,
        static_cast<std::uint32_t>(stream_bodies.size()));
    for (std::size_t lane = 0; lane < stream_bodies.size(); ++lane) {
      const StreamBody& stream_body = stream_bodies[lane];
      for (std::size_t task_ordinal = 0;
           task_ordinal < stream_body.tasks.size(); ++task_ordinal) {
        const TaskRow& task = *stream_body.tasks[task_ordinal]->task;
        const bool is_communication =
            graph_body_task_is_communication(task);
        ir.graph_launch_body_members.append(
            body_id, task.id, static_cast<std::uint32_t>(lane),
            static_cast<std::uint32_t>(task_ordinal),
            is_communication
                ? GraphLaunchBodyMemberRow::Kind::kCommunication
                : GraphLaunchBodyMemberRow::Kind::kCompute);
      }
    }
  }
  return missing_body_capability_launches;
}

}  // namespace traceloom::ascend_sqlite_detail
