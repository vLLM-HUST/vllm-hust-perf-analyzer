#include "traceloom/compat/structural_projection_rows.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "traceloom/analysis/event_cost_attribution.h"

namespace traceloom::compat {
namespace {
double ns_to_us(std::int64_t ns) {
  return static_cast<double>(ns) / 1000.0;
}

std::string symbol_value_or_empty(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

StructuralAnchorKind structural_anchor_kind_for_anchor_kind(AnchorKind kind) {
  switch (kind) {
    case AnchorKind::kDeviceEvent:
      return StructuralAnchorKind::kExec;
    case AnchorKind::kCommunication:
      return StructuralAnchorKind::kCollective;
    case AnchorKind::kGraphH:
      return StructuralAnchorKind::kGraphH;
    case AnchorKind::kGraphL:
      return StructuralAnchorKind::kGraphL;
    case AnchorKind::kGraphT:
      return StructuralAnchorKind::kGraphT;
    case AnchorKind::kGraphReplayUnit:
      return StructuralAnchorKind::kGraphTemplate;
    case AnchorKind::kSynchronization:
    case AnchorKind::kUnknown:
      return StructuralAnchorKind::kUnknown;
  }
  return StructuralAnchorKind::kUnknown;
}

bool is_comm_token(const StructuralProjectionToken& token) {
  return token.anchor_kind == StructuralAnchorKind::kCollective;
}

struct PreludeCost {
  double exec_aux_us = 0.0;
  double comm_us = 0.0;
  double idle_us = 0.0;
  double aux_event_count = 0.0;
  double aux_us = 0.0;
};

struct DeviceEventIndex {
  std::vector<const TraceEventRow*> events;
  std::vector<std::int64_t> prefix_max_end_ns;
};
std::string lower_ascii(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

std::unordered_set<TraceEventId::value_type> anchored_trace_event_ids(
    const NativeIr& ir) {
  std::unordered_set<TraceEventId::value_type> out;
  for (const AnchorRow& anchor : ir.anchors.rows()) {
    if (!anchor.trace_event_id.valid()) {
      continue;
    }
    if (anchor.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument("AnchorRow trace_event_id is out of range");
    }
    out.insert(anchor.trace_event_id.value());
  }
  return out;
}

std::unordered_map<TraceEventId::value_type, const TaskRow*> tasks_by_event(
    const NativeIr& ir) {
  std::unordered_map<TraceEventId::value_type, const TaskRow*> out;
  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.trace_event_id.valid() ||
        task.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument("TaskRow trace_event_id is out of range");
    }
    out.emplace(task.trace_event_id.value(), &task);
  }
  return out;
}

std::unordered_set<TraceEventId::value_type> communication_event_ids(
    const NativeIr& ir) {
  std::unordered_set<TraceEventId::value_type> out;
  for (const CommunicationOpRow& comm : ir.communication_ops.rows()) {
    if (!comm.trace_event_id.valid() ||
        comm.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument(
          "CommunicationOpRow trace_event_id is out of range");
    }
    out.insert(comm.trace_event_id.value());
  }
  return out;
}

bool is_wait_event(const NativeIr& ir,
                   const TraceEventRow& event,
                   const TaskRow* task) {
  std::string blob = symbol_value_or_empty(ir, event.raw_name_symbol_id);
  if (task != nullptr) {
    blob += " " + symbol_value_or_empty(ir, task->task_type_symbol_id);
    blob += " " + symbol_value_or_empty(ir, task->op_name_symbol_id);
    blob += " " + symbol_value_or_empty(ir, task->op_type_symbol_id);
  }
  blob = lower_ascii(std::move(blob));
  return blob.find("event_wait") != std::string::npos ||
         blob.find("wait") != std::string::npos;
}

PreludeCost compute_interval_cost(
    const NativeIr& ir,
    const DeviceEventIndex& event_index,
    const std::unordered_set<TraceEventId::value_type>& anchored_events,
    const std::unordered_map<TraceEventId::value_type, const TaskRow*>&
        task_index,
    const std::unordered_set<TraceEventId::value_type>& comm_events,
    const EventCostAttributionMask* cost_mask,
    std::int64_t interval_start,
    std::int64_t interval_end) {
  PreludeCost cost;
  if (interval_end <= interval_start) {
    return cost;
  }

  const auto upper = std::lower_bound(
      event_index.events.begin(), event_index.events.end(), interval_end,
      [](const TraceEventRow* event, std::int64_t value) {
        return event->start_ns < value;
      });
  const std::size_t upper_index =
      static_cast<std::size_t>(upper - event_index.events.begin());
  const auto lower = std::upper_bound(
      event_index.prefix_max_end_ns.begin(),
      event_index.prefix_max_end_ns.begin() + upper_index, interval_start);
  const std::size_t lower_index = static_cast<std::size_t>(
      lower - event_index.prefix_max_end_ns.begin());

  struct Boundary {
    std::int64_t time_ns = 0;
    int comm_delta = 0;
    int exec_delta = 0;
  };
  std::vector<Boundary> boundaries;
  boundaries.reserve((upper_index - lower_index) * 2);
  for (std::size_t event_index_value = lower_index;
       event_index_value < upper_index; ++event_index_value) {
    const TraceEventRow& event = *event_index.events[event_index_value];
    if (anchored_events.find(event.id.value()) != anchored_events.end()) {
      continue;
    }
    if (cost_mask != nullptr && !cost_mask->includes(event.id)) {
      continue;
    }
    const std::int64_t overlap_start =
        std::max(interval_start, event.start_ns);
    const std::int64_t overlap_end = std::min(interval_end, event.end_ns);
    if (overlap_end <= overlap_start) {
      continue;
    }

    const double duration_us = ns_to_us(overlap_end - overlap_start);
    const auto task_found = task_index.find(event.id.value());
    const TaskRow* task =
        task_found == task_index.end() ? nullptr : task_found->second;
    cost.aux_event_count += 1.0;
    cost.aux_us += duration_us;

    const bool is_comm =
        comm_events.find(event.id.value()) != comm_events.end() ||
        (task != nullptr && task->comm_name_symbol_id.valid());
    const bool is_exec = !is_comm && !is_wait_event(ir, event, task);
    if (is_comm) {
      boundaries.push_back({overlap_start, 1, 0});
      boundaries.push_back({overlap_end, -1, 0});
    } else if (is_exec) {
      boundaries.push_back({overlap_start, 0, 1});
      boundaries.push_back({overlap_end, 0, -1});
    }
  }

  std::sort(boundaries.begin(), boundaries.end(),
            [](const Boundary& lhs, const Boundary& rhs) {
              return lhs.time_ns < rhs.time_ns;
            });
  int active_comm = 0;
  int active_exec = 0;
  std::int64_t cursor_ns = interval_start;
  std::size_t boundary_index = 0;
  while (boundary_index < boundaries.size()) {
    const std::int64_t boundary_ns = boundaries[boundary_index].time_ns;
    const double segment_us = ns_to_us(boundary_ns - cursor_ns);
    if (active_comm > 0) {
      cost.comm_us += segment_us;
    } else if (active_exec > 0) {
      cost.exec_aux_us += segment_us;
    }
    while (boundary_index < boundaries.size() &&
           boundaries[boundary_index].time_ns == boundary_ns) {
      active_comm += boundaries[boundary_index].comm_delta;
      active_exec += boundaries[boundary_index].exec_delta;
      ++boundary_index;
    }
    cursor_ns = boundary_ns;
  }

  const double gap_us = ns_to_us(interval_end - interval_start);
  // Wait-only and profiler-silent time share the idle bucket.  Wait events are
  // still retained in aux_us as an evidence overlay.
  cost.idle_us =
      std::max(0.0, gap_us - cost.comm_us - cost.exec_aux_us);
  return cost;
}

std::unordered_map<std::uint32_t, DeviceEventIndex> build_event_index(
    const NativeIr& ir) {
  std::unordered_map<std::uint32_t, DeviceEventIndex> out;
  for (const TraceEventRow& event : ir.trace_events.rows()) {
    out[event.device_id].events.push_back(&event);
  }
  for (auto& item : out) {
    DeviceEventIndex& index = item.second;
    std::sort(index.events.begin(), index.events.end(),
              [](const TraceEventRow* lhs, const TraceEventRow* rhs) {
                if (lhs->start_ns != rhs->start_ns) {
                  return lhs->start_ns < rhs->start_ns;
                }
                if (lhs->end_ns != rhs->end_ns) {
                  return lhs->end_ns < rhs->end_ns;
                }
                return lhs->id < rhs->id;
              });
    index.prefix_max_end_ns.reserve(index.events.size());
    std::int64_t max_end_ns = 0;
    bool has_end = false;
    for (const TraceEventRow* event : index.events) {
      max_end_ns = has_end ? std::max(max_end_ns, event->end_ns)
                           : event->end_ns;
      has_end = true;
      index.prefix_max_end_ns.push_back(max_end_ns);
    }
  }
  return out;
}

std::vector<double> compute_timeline_anchor_costs(
    const std::vector<StructuralProjectionToken>& tokens) {
  struct Boundary {
    std::int64_t time_ns = 0;
    std::size_t token_index = 0;
    bool starts = false;
  };

  std::unordered_map<std::uint32_t, std::vector<Boundary>> by_device;
  for (std::size_t token_index = 0; token_index < tokens.size();
       ++token_index) {
    const StructuralProjectionToken& token = tokens[token_index];
    if (token.end_ns < token.start_ns) {
      throw std::invalid_argument("StructuralProjectionToken has negative duration");
    }
    if (token.end_ns == token.start_ns) {
      continue;
    }
    by_device[token.device_id].push_back(
        {token.start_ns, token_index, true});
    by_device[token.device_id].push_back({token.end_ns, token_index, false});
  }

  std::vector<double> out(tokens.size(), 0.0);
  for (auto& item : by_device) {
    std::vector<Boundary>& boundaries = item.second;
    std::sort(boundaries.begin(), boundaries.end(),
              [](const Boundary& lhs, const Boundary& rhs) {
                return lhs.time_ns < rhs.time_ns;
              });
    std::set<std::size_t> active_compute;
    std::set<std::size_t> active_comm;
    std::int64_t cursor_ns =
        boundaries.empty() ? 0 : boundaries.front().time_ns;
    std::size_t boundary_index = 0;
    while (boundary_index < boundaries.size()) {
      const std::int64_t boundary_ns = boundaries[boundary_index].time_ns;
      const std::set<std::size_t>& owners =
          active_comm.empty() ? active_compute : active_comm;
      if (!owners.empty()) {
        out[*owners.begin()] += ns_to_us(boundary_ns - cursor_ns);
      }
      while (boundary_index < boundaries.size() &&
             boundaries[boundary_index].time_ns == boundary_ns) {
        const Boundary& boundary = boundaries[boundary_index];
        std::set<std::size_t>& active =
            is_comm_token(tokens[boundary.token_index]) ? active_comm
                                                        : active_compute;
        if (boundary.starts) {
          active.insert(boundary.token_index);
        } else {
          active.erase(boundary.token_index);
        }
        ++boundary_index;
      }
      cursor_ns = boundary_ns;
    }
  }
  return out;
}

std::vector<PreludeCost> compute_prelude_costs(
    const NativeIr& ir,
    const std::vector<StructuralProjectionToken>& tokens,
    const EventCostAttributionMask* cost_mask) {
  std::vector<PreludeCost> costs(tokens.size());
  if (tokens.empty()) {
    return costs;
  }

  const std::unordered_set<TraceEventId::value_type> anchored_events =
      anchored_trace_event_ids(ir);
  const std::unordered_map<TraceEventId::value_type, const TaskRow*>
      task_index = tasks_by_event(ir);
  const std::unordered_set<TraceEventId::value_type> comm_events =
      communication_event_ids(ir);

  const auto event_indexes = build_event_index(ir);
  std::unordered_map<std::uint32_t, std::vector<std::size_t>> tokens_by_device;
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    tokens_by_device[tokens[index].device_id].push_back(index);
  }
  for (auto& item : tokens_by_device) {
    std::vector<std::size_t>& device_tokens = item.second;
    std::sort(device_tokens.begin(), device_tokens.end(),
              [&tokens](std::size_t lhs, std::size_t rhs) {
                if (tokens[lhs].start_ns != tokens[rhs].start_ns) {
                  return tokens[lhs].start_ns < tokens[rhs].start_ns;
                }
                if (tokens[lhs].end_ns != tokens[rhs].end_ns) {
                  return tokens[lhs].end_ns < tokens[rhs].end_ns;
                }
                return lhs < rhs;
              });
    const auto event_index_found = event_indexes.find(item.first);
    std::int64_t frontier_ns = tokens[device_tokens.front()].start_ns;
    for (std::size_t token_index : device_tokens) {
      const StructuralProjectionToken& token = tokens[token_index];
      if (token.start_ns > frontier_ns &&
          event_index_found != event_indexes.end()) {
        costs[token_index] = compute_interval_cost(
            ir, event_index_found->second, anchored_events, task_index,
            comm_events, cost_mask, frontier_ns, token.start_ns);
      } else if (token.start_ns > frontier_ns) {
        costs[token_index].idle_us = ns_to_us(token.start_ns - frontier_ns);
      }
      frontier_ns = std::max(frontier_ns, token.end_ns);
    }
  }
  return costs;
}
std::vector<StructuralProjectionToken> build_structural_projection_tokens_from_native_ir_impl(
    const NativeIr& ir,
    const EventCostAttributionMask* cost_mask) {
  std::vector<StructuralProjectionToken> tokens;
  tokens.reserve(ir.tokens.size());
  for (const TokenRow& token : ir.tokens.rows()) {
    if (!token.anchor_id.valid() || token.anchor_id.value() >= ir.anchors.size()) {
      throw std::invalid_argument("TokenRow anchor_id is out of range");
    }
    const AnchorRow& anchor = ir.anchors.row(token.anchor_id);

    StructuralProjectionToken out;
    out.ordinal = token.sequence_index;
    out.device_id = anchor.device_id;
    out.symbol_id = token.symbol_id;
    out.display_op = symbol_value_or_empty(ir, token.symbol_id);
    out.display_category = structural_anchor_kind_name(
        structural_anchor_kind_for_anchor_kind(anchor.kind));
    out.anchor_kind = structural_anchor_kind_for_anchor_kind(anchor.kind);
    out.anchor_id = token.anchor_id;
    out.start_ns = token.start_ns;
    out.end_ns = token.end_ns;
    tokens.push_back(std::move(out));
  }
  const std::vector<PreludeCost> prelude_costs =
      compute_prelude_costs(ir, tokens, cost_mask);
  const std::vector<double> timeline_anchor_costs =
      compute_timeline_anchor_costs(tokens);
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    tokens[i].has_prelude_cost = true;
    tokens[i].timeline_anchor_us = timeline_anchor_costs[i];
    tokens[i].prelude_exec_aux_us = prelude_costs[i].exec_aux_us;
    tokens[i].prelude_comm_us = prelude_costs[i].comm_us;
    tokens[i].prelude_idle_us = prelude_costs[i].idle_us;
    tokens[i].prelude_aux_event_count = prelude_costs[i].aux_event_count;
    tokens[i].prelude_aux_us = prelude_costs[i].aux_us;
  }
  return tokens;
}

}  // namespace

std::vector<StructuralProjectionToken> build_structural_projection_tokens_from_native_ir(
    const NativeIr& ir) {
  return build_structural_projection_tokens_from_native_ir_impl(ir, nullptr);
}

std::vector<StructuralProjectionToken> build_structural_projection_tokens_from_native_ir(
    const NativeIr& ir,
    FlatAnchorBuildConfig config) {
  const EventCostAttributionMask mask =
      build_event_cost_attribution_mask(ir, std::move(config));
  return build_structural_projection_tokens_from_native_ir_impl(ir, &mask);
}

std::vector<NativeStructuralDevicePartition> partition_structural_projection_tokens_by_device(
    const NativeIr& ir) {
  const std::vector<StructuralProjectionToken> tokens =
      build_structural_projection_tokens_from_native_ir(ir);
  std::map<std::uint32_t, std::vector<StructuralProjectionToken>> by_device;
  for (const StructuralProjectionToken& token : tokens) {
    by_device[token.device_id].push_back(token);
  }
  std::vector<NativeStructuralDevicePartition> out;
  out.reserve(by_device.size());
  for (auto& item : by_device) {
    NativeStructuralDevicePartition partition;
    partition.device_id = item.first;
    partition.tokens = std::move(item.second);
    out.push_back(std::move(partition));
  }
  return out;
}

std::vector<NativeStructuralDevicePartition> partition_structural_projection_tokens_by_device(
    const NativeIr& ir,
    FlatAnchorBuildConfig config) {
  const std::vector<StructuralProjectionToken> tokens =
      build_structural_projection_tokens_from_native_ir(ir, std::move(config));
  std::map<std::uint32_t, std::vector<StructuralProjectionToken>> by_device;
  for (const StructuralProjectionToken& token : tokens) {
    by_device[token.device_id].push_back(token);
  }
  std::vector<NativeStructuralDevicePartition> out;
  out.reserve(by_device.size());
  for (auto& item : by_device) {
    out.push_back(
        NativeStructuralDevicePartition{item.first, std::move(item.second)});
  }
  return out;
}

}  // namespace traceloom::compat
