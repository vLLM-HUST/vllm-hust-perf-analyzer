#include "traceloom/compat/aux_attribution_rows.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "traceloom/analysis/event_cost_attribution.h"
#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/timeline_rows.h"

namespace traceloom::compat {
namespace {

double ns_to_us(std::int64_t ns) {
  return static_cast<double>(ns) / 1000.0;
}

std::string symbol_value_or_empty(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

std::unordered_set<TraceEventId::value_type> anchored_trace_event_ids(
    const NativeIr& ir) {
  std::unordered_set<TraceEventId::value_type> out;
  for (const AnchorRow& anchor : ir.anchors.rows()) {
    if (anchor.trace_event_id.valid()) {
      if (anchor.trace_event_id.value() >= ir.trace_events.size()) {
        throw std::invalid_argument("AnchorRow trace_event_id is out of range");
      }
      out.insert(anchor.trace_event_id.value());
    }
  }
  return out;
}

std::unordered_set<TraceEventId::value_type> host_runtime_trace_event_ids(
    const NativeIr& ir) {
  std::set<std::pair<SourceRefId::value_type, std::uint64_t>> runtime_sources;
  for (const RuntimeCallRow& call : ir.runtime_calls.rows()) {
    runtime_sources.emplace(call.source_ref_id.value(), call.source_row_id);
  }
  std::unordered_set<TraceEventId::value_type> out;
  for (const TraceEventRow& event : ir.trace_events.rows()) {
    if (runtime_sources.find(
            {event.source_ref_id.value(), event.source_row_id}) !=
        runtime_sources.end()) {
      out.insert(event.id.value());
    }
  }
  return out;
}

std::unordered_set<TraceEventId::value_type>
reconciled_timing_envelope_event_ids(const NativeIr& ir) {
  std::unordered_set<TraceEventId::value_type> out;
  for (const EventReconciliationMemberRow& member :
       ir.event_reconciliation.members) {
    if (member.role != EventReconciliationMemberRole::kTimingEnvelope ||
        !member.decision_id.valid() ||
        member.decision_id.value() >=
            ir.event_reconciliation.decisions.size()) {
      continue;
    }
    const EventReconciliationDecisionRow& decision =
        ir.event_reconciliation.decisions[member.decision_id.value()];
    if (decision.status == EventReconciliationStatus::kReconciled &&
        member.event_id.valid()) {
      out.insert(member.event_id.value());
    }
  }
  return out;
}

using AnchorsByDevice =
    std::map<std::uint32_t, std::vector<const AnchorRow*>>;

AnchorsByDevice index_anchors_by_device(const NativeIr& ir) {
  AnchorsByDevice result;
  for (const AnchorRow& anchor : ir.anchors.rows()) {
    result[anchor.device_id].push_back(&anchor);
  }
  for (auto& [device_id, anchors] : result) {
    (void)device_id;
    std::sort(anchors.begin(), anchors.end(),
              [](const AnchorRow* lhs, const AnchorRow* rhs) {
                return lhs->start_ns < rhs->start_ns ||
                       (lhs->start_ns == rhs->start_ns && lhs->id < rhs->id);
              });
  }
  return result;
}

const AnchorRow* following_anchor_for_aux_event(
    const AnchorsByDevice& anchors_by_device,
    const TraceEventRow& event) {
  const auto device = anchors_by_device.find(event.device_id);
  if (device == anchors_by_device.end()) {
    return nullptr;
  }
  const auto found = std::lower_bound(
      device->second.begin(), device->second.end(), event.end_ns,
      [](const AnchorRow* anchor, std::int64_t end_ns) {
        return anchor->start_ns < end_ns;
      });
  return found == device->second.end() ? nullptr : *found;
}

struct AuxSlotAccum {
  AnchorAuxSlotSqlRow slot;
  bool initialized = false;
};

}  // namespace

AuxAttributionSqlRows build_aux_attribution_sql_rows_impl(
    const NativeIr& ir,
    std::uint32_t db_idx,
    const EventCostAttributionMask* cost_mask) {
  const std::unordered_set<TraceEventId::value_type> anchored_events =
      anchored_trace_event_ids(ir);
  const std::unordered_set<TraceEventId::value_type> host_runtime_events =
      host_runtime_trace_event_ids(ir);
  const std::unordered_set<TraceEventId::value_type>
      reconciled_timing_envelopes =
          reconciled_timing_envelope_event_ids(ir);
  const AnchorsByDevice anchors_by_device = index_anchors_by_device(ir);
  AuxAttributionSqlRows rows;
  std::map<AnchorId::value_type, AuxSlotAccum> slots;
  std::map<AnchorId::value_type, std::uint32_t> next_aux_order;

  for (const TraceEventRow& event : ir.trace_events.rows()) {
    if (anchored_events.find(event.id.value()) != anchored_events.end()) {
      continue;
    }
    if (host_runtime_events.find(event.id.value()) !=
        host_runtime_events.end()) {
      continue;
    }
    // A timing envelope reconciled into a canonical anchor is retained as
    // normalized evidence, but is not additional device work.  Counting it as
    // prelude auxiliary cost would charge the same physical interval twice.
    if (reconciled_timing_envelopes.find(event.id.value()) !=
        reconciled_timing_envelopes.end()) {
      continue;
    }
    if (cost_mask != nullptr && !cost_mask->includes(event.id)) {
      continue;
    }
    const AnchorRow* anchor =
        following_anchor_for_aux_event(anchors_by_device, event);
    if (anchor == nullptr) {
      continue;
    }

    const std::uint32_t aux_order = ++next_aux_order[anchor->id.value()];
    AuxLinkSqlRow link;
    link.anchor_id = anchor_compat_id(anchor->id);
    link.aux_event_id = trace_event_compat_id(event.id);
    link.db_idx = db_idx;
    link.device_id = event.device_id;
    link.aux_order = aux_order;
    link.aux_step_idx = event.id.value();
    link.link_type = "prelude";
    link.reason = "unanchored_event_before_anchor";
    link.aux_kind = symbol_value_or_empty(ir, event.raw_name_symbol_id);
    link.aux_dur_us = ns_to_us(event.end_ns - event.start_ns);
    rows.aux_links.push_back(std::move(link));

    AuxSlotAccum& accum = slots[anchor->id.value()];
    if (!accum.initialized) {
      accum.slot.anchor_id = anchor_compat_id(anchor->id);
      accum.slot.db_idx = db_idx;
      accum.slot.device_id = anchor->device_id;
      accum.slot.anchor_idx = anchor->id.value() + 1;
      accum.slot.anchor_step_idx = anchor->trace_event_id.valid()
                                       ? anchor->trace_event_id.value()
                                       : anchor->id.value();
      accum.slot.aux_start_step_idx = event.id.value();
      accum.slot.aux_end_step_idx = event.id.value();
      accum.initialized = true;
    } else {
      accum.slot.aux_start_step_idx =
          std::min(accum.slot.aux_start_step_idx, event.id.value());
      accum.slot.aux_end_step_idx =
          std::max(accum.slot.aux_end_step_idx, event.id.value());
    }
    accum.slot.aux_event_count += 1;
    accum.slot.aux_dur_us += ns_to_us(event.end_ns - event.start_ns);
  }

  rows.aux_slots.reserve(slots.size());
  for (auto& entry : slots) {
    rows.aux_slots.push_back(std::move(entry.second.slot));
  }
  return rows;
}

AuxAttributionSqlRows build_aux_attribution_sql_rows(const NativeIr& ir,
                                                     std::uint32_t db_idx) {
  return build_aux_attribution_sql_rows_impl(ir, db_idx, nullptr);
}

AuxAttributionSqlRows build_aux_attribution_sql_rows(
    const NativeIr& ir,
    FlatAnchorBuildConfig config,
    std::uint32_t db_idx) {
  const EventCostAttributionMask mask =
      build_event_cost_attribution_mask(ir, std::move(config));
  return build_aux_attribution_sql_rows_impl(ir, db_idx, &mask);
}

}  // namespace traceloom::compat
