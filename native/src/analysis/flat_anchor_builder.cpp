#include "traceloom/analysis/flat_anchor_builder.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "traceloom/analysis/structural_symbol_normalization.h"

namespace traceloom {
namespace {

struct AnchorCandidate {
  TraceEventId trace_event_id;
  ReplayUnitId replay_unit_id = ReplayUnitId::invalid();
  ReplayUnitLaunchMemberId replay_unit_launch_member_id =
      ReplayUnitLaunchMemberId::invalid();
  AnchorKind kind = AnchorKind::kUnknown;
  SymbolId symbol_id;
  StructuralSymbolDecision symbol_decision;
  SourceRefId source_ref_id;
  std::uint64_t source_row_id = 0;
  std::uint32_t device_id = 0;
  std::uint32_t stream_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

AnchorCandidate anchor_candidate_from_event(
    const NativeIr& ir,
    TraceEventId trace_event_id,
    ReplayUnitId replay_unit_id,
    AnchorKind kind,
    ResolvedStructuralSymbol resolved_symbol) {
  const TraceEventRow& event = ir.trace_events.row(trace_event_id);
  return AnchorCandidate{trace_event_id,
                         replay_unit_id,
                         ReplayUnitLaunchMemberId::invalid(),
                         kind,
                         resolved_symbol.structural_symbol_id,
                         resolved_symbol.decision,
                         event.source_ref_id,
                         event.source_row_id,
                         event.device_id,
                         event.stream_id,
                         event.start_ns,
                         event.end_ns};
}

struct CommunicationSpan {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

struct ReplayUnitSpan {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

SymbolId choose_task_symbol(const TaskRow& task) {
  if (task.op_type_symbol_id.valid()) {
    return task.op_type_symbol_id;
  }
  if (task.op_name_symbol_id.valid()) {
    return task.op_name_symbol_id;
  }
  if (task.comm_name_symbol_id.valid()) {
    return task.comm_name_symbol_id;
  }
  return task.task_type_symbol_id;
}

std::string symbol_text(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

std::string lower_ascii(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

std::string normalize_task_key(std::string value) {
  for (char& ch : value) {
    if (std::isalnum(static_cast<unsigned char>(ch))) {
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    } else {
      ch = '_';
    }
  }
  while (value.find("__") != std::string::npos) {
    value.replace(value.find("__"), 2, "_");
  }
  while (!value.empty() && value.front() == '_') {
    value.erase(value.begin());
  }
  while (!value.empty() && value.back() == '_') {
    value.pop_back();
  }
  return value;
}

bool has_concrete_operator_identity(const TaskRow& task) {
  return task.op_name_symbol_id.valid() || task.op_type_symbol_id.valid() ||
         task.compute_task_type_symbol_id.valid() ||
         task.comm_name_symbol_id.valid();
}

std::string provider_scope_from_source_kind(const std::string& source_kind) {
  const std::string lower = lower_ascii(source_kind);
  if (lower.find("cuda") != std::string::npos ||
      lower.find("nsys") != std::string::npos) {
    return "cuda";
  }
  if (lower.find("ascend") != std::string::npos ||
      lower.find("cann") != std::string::npos) {
    return "ascend";
  }
  if (lower.find("hygon") != std::string::npos ||
      lower.find("hip") != std::string::npos) {
    return "hygon";
  }
  return "any";
}

SignalClassificationInput make_signal_classification_input(
    const NativeIr& ir,
    const TaskRow& task) {
  const std::string task_type =
      normalize_task_key(symbol_text(ir, task.task_type_symbol_id));
  const std::string label = symbol_text(ir, choose_task_symbol(task));
  const std::string blob = lower_ascii(
      label + " " + symbol_text(ir, task.op_name_symbol_id) + " " +
      symbol_text(ir, task.op_type_symbol_id) + " " +
      symbol_text(ir, task.compute_task_type_symbol_id) + " " +
      symbol_text(ir, task.task_type_symbol_id));

  const bool has_concrete_operator = has_concrete_operator_identity(task);
  const std::string provider_scope = provider_scope_from_source_kind(
      ir.source_refs.row(task.source_ref_id).source_kind);
  return SignalClassificationInput{"task", task_type, blob, label,
                                   has_concrete_operator, provider_scope};
}

void validate_task_trace_event_refs(const TaskTable& tasks,
                                    std::size_t trace_event_count) {
  for (const TaskRow& task : tasks.rows()) {
    if (!task.trace_event_id.valid() ||
        task.trace_event_id.value() >= trace_event_count) {
      throw std::invalid_argument("TaskRow trace_event_id is out of range");
    }
  }
}

std::unordered_set<TraceEventId::value_type> communication_trace_event_ids(
    const CommunicationOpTable& communication_ops) {
  std::unordered_set<TraceEventId::value_type> out;
  for (const CommunicationOpRow& row : communication_ops.rows()) {
    if (row.trace_event_id.valid()) {
      out.insert(row.trace_event_id.value());
    }
  }
  return out;
}

std::uint64_t connection_key(std::uint32_t device_id,
                             std::int64_t connection_id) {
  return (static_cast<std::uint64_t>(device_id) << 32u) ^
         (static_cast<std::uint64_t>(connection_id) & 0xffffffffu);
}

std::unordered_map<std::uint32_t, std::vector<ReplayUnitSpan>>
replay_unit_spans_by_device(const NativeIr& ir) {
  std::unordered_map<std::uint32_t, std::vector<ReplayUnitSpan>> out;
  for (const ReplayUnitRow& replay : ir.replay_units.rows()) {
    if (!replay.launch_trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(replay.launch_trace_event_id);
    out[event.device_id].push_back(ReplayUnitSpan{event.start_ns, event.end_ns});
  }
  for (auto& item : out) {
    std::sort(item.second.begin(), item.second.end(),
              [](const ReplayUnitSpan& lhs, const ReplayUnitSpan& rhs) {
                if (lhs.start_ns != rhs.start_ns) {
                  return lhs.start_ns < rhs.start_ns;
                }
                return lhs.end_ns < rhs.end_ns;
              });
  }
  return out;
}

std::unordered_map<std::uint64_t, std::vector<CommunicationSpan>>
communication_spans_by_connection(const NativeIr& ir) {
  std::unordered_map<std::uint64_t, std::vector<CommunicationSpan>> out;
  for (const CommunicationOpRow& comm : ir.communication_ops.rows()) {
    if (!comm.trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(comm.trace_event_id);
    out[connection_key(event.device_id, comm.raw_connection_id)].push_back(
        CommunicationSpan{event.start_ns, event.end_ns});
  }
  return out;
}

bool task_type_is_skipped(const NativeIr& ir,
                          const TaskRow& task,
                          const FlatAnchorBuildConfig& config) {
  if (!task.task_type_symbol_id.valid()) {
    return false;
  }
  const std::string& task_type = ir.symbols.value(task.task_type_symbol_id);
  return std::find(config.skipped_task_type_symbols.begin(),
                   config.skipped_task_type_symbols.end(),
                   task_type) != config.skipped_task_type_symbols.end();
}

bool task_is_covered_by_communication_op(
    const NativeIr& ir,
    const TaskRow& task,
    const std::unordered_map<std::uint64_t, std::vector<CommunicationSpan>>&
        comm_spans) {
  if (!task.trace_event_id.valid() || task.raw_connection_id < 0) {
    return false;
  }
  const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
  const auto found =
      comm_spans.find(connection_key(event.device_id, task.raw_connection_id));
  if (found == comm_spans.end()) {
    return false;
  }
  for (const CommunicationSpan& span : found->second) {
    if (event.start_ns <= span.end_ns && event.end_ns >= span.start_ns) {
      return true;
    }
  }
  return false;
}

bool event_is_covered_by_replay_unit(
    const TraceEventRow& event,
    const std::unordered_map<std::uint32_t, std::vector<ReplayUnitSpan>>&
        replay_spans) {
  const auto found = replay_spans.find(event.device_id);
  if (found == replay_spans.end()) {
    return false;
  }
  const std::vector<ReplayUnitSpan>& spans = found->second;
  const auto first_after_start =
      std::upper_bound(spans.begin(), spans.end(), event.start_ns,
                       [](std::int64_t value, const ReplayUnitSpan& span) {
                         return value < span.start_ns;
                       });
  if (first_after_start == spans.begin()) {
    return false;
  }
  const ReplayUnitSpan& span = *(first_after_start - 1);
  return event.start_ns >= span.start_ns && event.end_ns <= span.end_ns;
}

bool candidate_less(const AnchorCandidate& lhs,
                    const AnchorCandidate& rhs) {
  if (lhs.device_id != rhs.device_id) {
    return lhs.device_id < rhs.device_id;
  }
  if (lhs.start_ns != rhs.start_ns) {
    return lhs.start_ns < rhs.start_ns;
  }
  if (lhs.end_ns != rhs.end_ns) {
    return lhs.end_ns < rhs.end_ns;
  }
  if (lhs.stream_id != rhs.stream_id) {
    return lhs.stream_id < rhs.stream_id;
  }
  const auto source_kind_order = [](AnchorKind kind) {
    switch (kind) {
      case AnchorKind::kCommunication:
        return 0;
      case AnchorKind::kGraphReplayUnit:
        return 1;
      case AnchorKind::kDeviceEvent:
        return 2;
      case AnchorKind::kSynchronization:
        return 3;
      case AnchorKind::kGraphH:
      case AnchorKind::kGraphL:
      case AnchorKind::kGraphT:
        return 4;
      case AnchorKind::kUnknown:
        return 5;
    }
    return 6;
  };
  const int lhs_order = source_kind_order(lhs.kind);
  const int rhs_order = source_kind_order(rhs.kind);
  if (lhs_order != rhs_order) {
    return lhs_order < rhs_order;
  }
  if (lhs.symbol_id != rhs.symbol_id) {
    return lhs.symbol_id < rhs.symbol_id;
  }
  if (lhs.source_ref_id != rhs.source_ref_id) {
    return lhs.source_ref_id < rhs.source_ref_id;
  }
  if (lhs.source_row_id != rhs.source_row_id) {
    return lhs.source_row_id < rhs.source_row_id;
  }
  return lhs.trace_event_id < rhs.trace_event_id;
}

}  // namespace

SignalClassificationInput signal_classification_input_for_task(
    const NativeIr& ir,
    const TaskRow& task) {
  if (!task.source_ref_id.valid() ||
      task.source_ref_id.value() >= ir.source_refs.size()) {
    throw std::invalid_argument("TaskRow source_ref_id is out of range");
  }
  return make_signal_classification_input(ir, task);
}

FlatAnchorBuildStats build_flat_anchors(NativeIr& ir,
                                        FlatAnchorBuildConfig config) {
  if (!ir.anchors.empty() || !ir.tokens.empty()) {
    throw std::invalid_argument(
        "build_flat_anchors expects empty AnchorTable and TokenTable");
  }

  validate_task_trace_event_refs(ir.tasks, ir.trace_events.size());
  const std::unordered_set<TraceEventId::value_type> comm_event_ids =
      communication_trace_event_ids(ir.communication_ops);
  const std::unordered_map<std::uint64_t, std::vector<CommunicationSpan>>
      comm_spans = communication_spans_by_connection(ir);
  const std::unordered_map<std::uint32_t, std::vector<ReplayUnitSpan>>
      replay_spans = replay_unit_spans_by_device(ir);
  const bool skip_replay_covered_tasks =
      config.skip_tasks_covered_by_replay_units ||
      config.skip_events_covered_by_replay_units;

  std::vector<AnchorCandidate> candidates;
  candidates.reserve(ir.tasks.size() + ir.communication_ops.size() +
                     ir.replay_units.size());
  FlatAnchorBuildStats stats;
  if (config.classification_rules.rules().empty()) {
    config.classification_rules = load_default_signal_classification_ruleset();
  }
  if (!config.classification_overrides.empty()) {
    config.classification_rules = override_signal_classification_ruleset(
        config.classification_rules, config.classification_overrides);
  }
  stats.classification_policy_id =
      config.classification_rules.metadata().policy_id;
  stats.classification_policy_version =
      config.classification_rules.metadata().policy_version;
  stats.classification_manifest_sha256 =
      config.classification_rules.metadata().manifest_sha256;
  if (config.structural_symbol_rules.empty()) {
    config.structural_symbol_rules =
        load_default_structural_symbol_ruleset();
  }
  ir.structural_symbol_policy = config.structural_symbol_rules.snapshot();
  if (config.filter_auxiliary_task_anchors) {
    stats.projection_kind = "anchor_compute_collective_only";
  }

  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.trace_event_id.valid()) {
      continue;
    }
    if (task_type_is_skipped(ir, task, config)) {
      ++stats.skipped_task_events;
      continue;
    }
    if (config.skip_tasks_covered_by_communication_ops &&
        task_is_covered_by_communication_op(ir, task, comm_spans)) {
      ++stats.skipped_task_events;
      continue;
    }
    const TraceEventRow& task_event = ir.trace_events.row(task.trace_event_id);
    if (skip_replay_covered_tasks &&
        event_is_covered_by_replay_unit(task_event, replay_spans)) {
      ++stats.skipped_task_events;
      continue;
    }
    if (config.filter_auxiliary_task_anchors) {
      const SignalClassificationDecision decision =
          config.classification_rules.decide(
              signal_classification_input_for_task(ir, task));
      switch (decision.role) {
        case SignalRole::kAuxiliary:
          ++stats.auxiliary_task_events;
          ++stats.skipped_task_events;
          continue;
        case SignalRole::kTransparent:
          ++stats.transparent_task_events;
          ++stats.skipped_task_events;
          continue;
        case SignalRole::kUnknownAnchor:
          // Noise filtering is deliberately positive-selection: an
          // unfamiliar row remains in the sequence so new behavior cannot
          // disappear as "noise".
          ++stats.unknown_anchor_task_events;
          ++stats.preserved_unclassified_task_events;
          break;
        case SignalRole::kAnchor:
          break;
      }
    }
    if (comm_event_ids.find(task.trace_event_id.value()) !=
        comm_event_ids.end()) {
      continue;
    }
    candidates.push_back(anchor_candidate_from_event(
        ir, task.trace_event_id, ReplayUnitId::invalid(),
        AnchorKind::kDeviceEvent,
        normalize_task_structural_symbol(ir, task,
                                         config.structural_symbol_rules)));
  }

  for (const ReplayUnitRow& replay : ir.replay_units.rows()) {
    if (!replay.launch_trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event =
        ir.trace_events.row(replay.launch_trace_event_id);
    if (!replay.replay_composition_region_id.valid()) {
      candidates.push_back(anchor_candidate_from_event(
          ir, replay.launch_trace_event_id, replay.id,
          AnchorKind::kGraphReplayUnit,
          preserve_structural_symbol(
              event.raw_name_symbol_id,
              StructuralSymbolSource::kTraceEventRawName)));
      continue;
    }
    std::vector<const ReplayUnitLaunchMemberRow*> members;
    for (const ReplayUnitLaunchMemberRow& member :
         ir.replay_unit_launch_members.rows()) {
      if (member.replay_unit_id == replay.id) {
        members.push_back(&member);
      }
    }
    std::sort(members.begin(), members.end(),
              [](const ReplayUnitLaunchMemberRow* lhs,
                 const ReplayUnitLaunchMemberRow* rhs) {
                return lhs->member_order < rhs->member_order;
              });
    if (members.empty()) {
      throw std::logic_error(
          "exact ReplayUnit has no graph launch membership");
    }
    for (std::size_t index = 0; index < members.size(); ++index) {
      const ReplayUnitLaunchMemberRow& member = *members[index];
      if (member.member_order != index) {
        throw std::logic_error(
            "exact ReplayUnit launch membership order is not contiguous");
      }
      const GraphLaunchOccurrenceRow& launch =
          ir.graph_launch_occurrences.row(
              member.graph_launch_occurrence_id);
      const ReplayCompositionSlotRow& slot =
          ir.replay_composition_slots.row(
              member.replay_composition_slot_id);
      AnchorKind kind = AnchorKind::kUnknown;
      std::string symbol;
      switch (slot.role) {
        case ReplayCompositionSlotRole::kHead:
          kind = AnchorKind::kGraphH;
          symbol = "ACLH";
          break;
        case ReplayCompositionSlotRole::kLayer:
          kind = AnchorKind::kGraphL;
          symbol = "ACLL";
          break;
        case ReplayCompositionSlotRole::kTail:
          kind = AnchorKind::kGraphT;
          symbol = "ACLT";
          break;
        case ReplayCompositionSlotRole::kGraph:
          kind = AnchorKind::kGraphReplayUnit;
          symbol = "ACLGraph";
          break;
        case ReplayCompositionSlotRole::kGeneric:
          kind = AnchorKind::kGraphReplayUnit;
          symbol = members.size() == 1
                       ? "ACLG"
                       : "ACLG" + std::to_string(slot.slot_order + 1);
          break;
        case ReplayCompositionSlotRole::kCudaGraph:
          kind = AnchorKind::kGraphReplayUnit;
          symbol = "CUDAGraph";
          break;
        case ReplayCompositionSlotRole::kUnclassified:
          throw std::logic_error(
              "exact ReplayUnit member has an unclassified slot role");
      }
      std::uint32_t raw_stream_id = event.stream_id;
      const StreamId stream_id = launch.model_stream_id.valid()
                                     ? launch.model_stream_id
                                     : launch.execute_stream_id;
      if (stream_id.valid()) {
        raw_stream_id = static_cast<std::uint32_t>(
            ir.streams.row(stream_id).raw_stream_id);
      }
      candidates.push_back(AnchorCandidate{
          TraceEventId::invalid(), replay.id, member.id, kind,
          ir.symbols.intern(symbol),
          synthesize_structural_symbol(ir.symbols.intern(symbol)).decision,
          replay.source_ref_id,
          member.id.value() + 1, launch.device_id, raw_stream_id,
          launch.start_ns, launch.end_ns});
    }
  }

  for (const CommunicationOpRow& comm : ir.communication_ops.rows()) {
    if (!comm.trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(comm.trace_event_id);
    if (config.skip_events_covered_by_replay_units &&
        event_is_covered_by_replay_unit(event, replay_spans)) {
      continue;
    }
    candidates.push_back(anchor_candidate_from_event(
        ir, comm.trace_event_id, ReplayUnitId::invalid(),
        AnchorKind::kCommunication,
        normalize_communication_structural_symbol(
            ir, comm, config.structural_symbol_rules)));
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const AnchorCandidate& lhs, const AnchorCandidate& rhs) {
              return candidate_less(lhs, rhs);
            });

  struct ReplayAnchorSpan {
    AnchorId first_anchor_id = AnchorId::invalid();
    AnchorId last_anchor_id = AnchorId::invalid();
    TokenId first_token_id = TokenId::invalid();
    TokenId last_token_id = TokenId::invalid();
  };
  std::vector<ReplayAnchorSpan> replay_anchor_spans(ir.replay_units.size());
  std::uint32_t sequence_index = 0;
  for (const AnchorCandidate& candidate : candidates) {
    const AnchorId anchor = ir.anchors.append(
        candidate.source_ref_id, candidate.trace_event_id,
        candidate.replay_unit_id, candidate.kind, candidate.symbol_id,
        candidate.device_id, candidate.stream_id, candidate.start_ns,
        candidate.end_ns, candidate.replay_unit_launch_member_id,
        candidate.symbol_decision);
    const TokenId token = ir.tokens.append(
        anchor, candidate.symbol_id, candidate.device_id, sequence_index++,
        candidate.start_ns, candidate.end_ns);

    if (candidate.replay_unit_id.valid()) {
      ReplayAnchorSpan& span =
          replay_anchor_spans[candidate.replay_unit_id.value()];
      if (!span.first_anchor_id.valid()) {
        span.first_anchor_id = anchor;
        span.first_token_id = token;
      }
      span.last_anchor_id = anchor;
      span.last_token_id = token;
    }

    if (candidate.kind == AnchorKind::kCommunication) {
      ++stats.communication_anchors;
    } else {
      ++stats.device_event_anchors;
    }
  }
  for (const ReplayUnitRow& replay : ir.replay_units.rows()) {
    ReplayAnchorSpan& span = replay_anchor_spans[replay.id.value()];
    if (!span.first_anchor_id.valid()) {
      continue;
    }
    ir.replay_units.set_anchor_bounds(replay.id, span.first_anchor_id,
                                      span.last_anchor_id);
    if (replay.replay_composition_region_id.valid()) {
      ir.protected_intervals.append(
          ProtectedIntervalKind::kGraphReplayUnit, BoundaryPolicy::kNoCross,
          span.first_token_id, span.last_token_id, span.first_anchor_id,
          span.last_anchor_id, replay.source_ref_id);
    }
  }
  stats.tokens = ir.tokens.size();
  return stats;
}

}  // namespace traceloom
