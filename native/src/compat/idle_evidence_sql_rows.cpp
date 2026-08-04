#include "traceloom/compat/idle_evidence_sql_rows.h"

#include <algorithm>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "traceloom/compat/timeline_rows.h"
#include "traceloom/core/sha256.h"

namespace traceloom::compat {
namespace {

double ns_to_us(std::uint64_t ns) {
  return static_cast<double>(ns) / 1000.0;
}

std::string json_string(const std::string& value) {
  std::ostringstream out;
  out << '"';
  for (const unsigned char ch : value) {
    switch (ch) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (ch < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<unsigned int>(ch) << std::dec;
        } else {
          out << static_cast<char>(ch);
        }
    }
  }
  out << '"';
  return out.str();
}

std::string nullable_integer(const std::optional<std::int64_t>& value) {
  return value.has_value() ? std::to_string(*value) : "null";
}

std::string build_metadata_json(
    const ProductiveTimelineRunResult& productive,
    const IdleExplanationRunResult& explanations,
    const IdleEvidenceSqlRowOptions& options) {
  std::vector<const DeviceTimelineResult*> devices;
  devices.reserve(productive.devices.size());
  for (const DeviceTimelineResult& device : productive.devices) {
    devices.push_back(&device);
  }
  std::stable_sort(devices.begin(), devices.end(),
                   [](const DeviceTimelineResult* lhs,
                      const DeviceTimelineResult* rhs) {
                     return lhs->device_id < rhs->device_id;
                   });

  // This is RFC 8785 canonical JSON for the restricted metadata value set
  // used here: lexicographically ordered object keys, integer numbers, arrays,
  // booleans/null, and minimally escaped UTF-8 strings.
  std::ostringstream out;
  out << "{\"analysis_status\":"
      << json_string(std::string(analysis_status_name(productive.status)))
      << ",\"attribution_rule_version\":"
      << json_string(explanations.attribution_rule_version)
      << ",\"collection_status\":"
      << json_string(
             std::string(collection_status_name(explanations.collection_status)))
      << ",\"contract_version\":" << json_string(options.contract_version)
      << ",\"db_idx\":" << options.db_idx << ",\"devices\":[";
  for (std::size_t index = 0; index < devices.size(); ++index) {
    if (index != 0) {
      out << ',';
    }
    const DeviceTimelineResult& device = *devices[index];
    out << "{\"device_id\":" << device.device_id
        << ",\"span_end_ns\":" << nullable_integer(device.span_end_ns)
        << ",\"span_start_ns\":" << nullable_integer(device.span_start_ns)
        << ",\"status\":"
        << json_string(std::string(analysis_status_name(device.status))) << '}';
  }
  out << "],\"host_api_rules_sha256\":"
      << json_string(options.host_api_rules_sha256)
      << ",\"host_api_rules_version\":"
      << json_string(options.host_api_rules_version)
      << ",\"semantic_rules_sha256\":"
      << json_string(productive.semantic_rules_sha256)
      << ",\"semantic_rules_version\":"
      << json_string(productive.semantic_rules_version)
      << ",\"source_kind\":" << json_string(options.source_kind)
      << ",\"source_path\":" << json_string(options.source_path) << '}';
  return out.str();
}

struct SourceIdentity {
  std::string source_kind;
  std::string source_table;
  std::string source_key;
  std::string trace_event_id;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

SourceIdentity source_identity(const NativeIr& ir, TraceEventId id) {
  if (!id.valid() || id.value() >= ir.trace_events.size()) {
    throw std::invalid_argument(
        "idle evidence lineage has an invalid trace event id");
  }
  const TraceEventRow& event = ir.trace_events.row(id);
  const SourceRefRow& source = ir.source_refs.row(event.source_ref_id);
  return SourceIdentity{source.source_kind,
                        source.table_name,
                        std::to_string(event.source_row_id),
                        trace_event_compat_id(id),
                        event.start_ns,
                        event.end_ns};
}

EvidenceLinkSqlRow evidence_link(const NativeIr& ir,
                                 const std::string& owner_kind,
                                 const std::string& owner_id,
                                 std::uint32_t ordinal,
                                 TraceEventId trace_event_id,
                                 const std::string& relation,
                                 const std::string& level,
                                 std::int64_t owner_start_ns,
                                 std::int64_t owner_end_ns) {
  const SourceIdentity source = source_identity(ir, trace_event_id);
  EvidenceLinkSqlRow row;
  row.owner_kind = owner_kind;
  row.owner_id = owner_id;
  row.evidence_ordinal = ordinal;
  row.source_kind = source.source_kind;
  row.source_table = source.source_table;
  row.source_key = source.source_key;
  row.relation = relation;
  row.evidence_level = level;
  row.overlap_start_ns = std::max(owner_start_ns, source.start_ns);
  row.overlap_end_ns = std::min(owner_end_ns, source.end_ns);
  row.has_overlap = row.overlap_end_ns > row.overlap_start_ns;
  row.trace_event_id = source.trace_event_id;
  return row;
}

std::string interval_id(const std::string& run_id,
                        std::uint32_t db_idx,
                        std::uint32_t device_id,
                        std::uint32_t order) {
  return run_id + ":device_interval:" + std::to_string(db_idx) + ':' +
         std::to_string(device_id) + ':' + std::to_string(order);
}

}  // namespace

IdleEvidenceSqlRows build_idle_evidence_sql_rows(
    const NativeIr& ir,
    const IdleEvidencePipelineResult& pipeline,
    const IdleExplanationAttributionRows& attribution,
    const IdleEvidenceSqlRowOptions& options) {
  IdleEvidenceSqlRows out;
  const ProductiveTimelineRunResult& productive = pipeline.productive_timeline;
  const StreamStateRunResult& streams = pipeline.stream_states;
  const IdleExplanationRunResult& explanations = pipeline.idle_explanations;
  if (productive.status != streams.status ||
      productive.status != explanations.status) {
    throw std::invalid_argument(
        "idle evidence pipeline stages disagree on analysis status");
  }

  RunMetadataSqlRow metadata;
  metadata.analysis_status = analysis_status_name(productive.status);
  metadata.contract_version = options.contract_version;
  metadata.semantic_rules_version = productive.semantic_rules_version;
  metadata.semantic_rules_sha256 = productive.semantic_rules_sha256;
  metadata.attribution_rule_version = explanations.attribution_rule_version;
  metadata.host_api_rules_version = options.host_api_rules_version;
  metadata.host_api_rules_sha256 = options.host_api_rules_sha256;
  metadata.collection_status = collection_status_name(explanations.collection_status);
  metadata.db_idx = options.db_idx;
  metadata.source_kind = options.source_kind;
  metadata.source_path = options.source_path;
  metadata.metadata_json = build_metadata_json(productive, explanations, options);
  metadata.run_id = sha256_hex(metadata.metadata_json);

  for (const DeviceTimelineResult& device : productive.devices) {
    if (!device.span_start_ns.has_value() || !device.span_end_ns.has_value()) {
      continue;
    }
    if (!metadata.has_span) {
      metadata.has_span = true;
      metadata.span_start_ns = *device.span_start_ns;
      metadata.span_end_ns = *device.span_end_ns;
    } else {
      metadata.span_start_ns =
          std::min(metadata.span_start_ns, *device.span_start_ns);
      metadata.span_end_ns = std::max(metadata.span_end_ns, *device.span_end_ns);
    }
  }
  out.run_metadata.push_back(metadata);

  std::size_t device_interval_count = 0;
  for (const DeviceTimelineResult& device : productive.devices) {
    device_interval_count += device.intervals.size();
  }
  out.device_intervals.reserve(device_interval_count);
  std::map<std::uint32_t, std::vector<const DeviceIntervalSqlRow*>> gaps;
  for (const DeviceTimelineResult& device : productive.devices) {
    for (std::size_t index = 0; index < device.intervals.size(); ++index) {
      const DeviceIntervalRow& input = device.intervals[index];
      if (input.end_ns <= input.start_ns) {
        throw std::invalid_argument(
            "idle evidence device interval is not positive");
      }
      DeviceIntervalSqlRow row;
      row.interval_id = interval_id(metadata.run_id, options.db_idx,
                                    device.device_id, index);
      row.run_id = metadata.run_id;
      row.db_idx = options.db_idx;
      row.device_id = device.device_id;
      row.interval_order = static_cast<std::uint32_t>(index);
      row.start_ns = input.start_ns;
      row.end_ns = input.end_ns;
      row.duration_ns = static_cast<std::uint64_t>(input.end_ns - input.start_ns);
      row.duration_us = ns_to_us(row.duration_ns);
      row.interval_kind =
          input.kind == DeviceIntervalKind::kProductiveActive
              ? "productive_active"
              : "visible_productive_idle";
      row.source_count = input.source_links.size();
      row.clock_domain = "device";
      row.contract_version = options.contract_version;
      row.semantic_rules_version = productive.semantic_rules_version;
      row.attribution_rule_version = explanations.attribution_rule_version;
      out.device_intervals.push_back(row);
      const DeviceIntervalSqlRow& stored = out.device_intervals.back();
      if (input.kind == DeviceIntervalKind::kVisibleProductiveIdle) {
        gaps[device.device_id].push_back(&stored);
      }
      for (std::size_t source_index = 0;
           source_index < input.source_links.size(); ++source_index) {
        EvidenceLinkSqlRow link = evidence_link(
            ir, "device_interval", stored.interval_id,
            static_cast<std::uint32_t>(source_index),
            input.source_links[source_index].trace_event_id,
            "none", "none", input.start_ns, input.end_ns);
        // This owner kind records provenance, not an explanation claim.
        // Absorbed communication-task lineage can be non-overlapping, and the
        // contract requires relation=none links to carry no overlap extent.
        link.has_overlap = false;
        out.evidence_links.push_back(std::move(link));
      }
    }
  }

  std::map<std::uint32_t, CollectionStatus> collection_by_device;
  for (const IdleExplanationDeviceResult& device : explanations.devices) {
    collection_by_device[device.device_id] = device.collection_status;
  }
  for (const StreamStateDeviceResult& device : streams.devices) {
    const auto collection_it = collection_by_device.find(device.device_id);
    const CollectionStatus device_collection =
        collection_it == collection_by_device.end()
            ? explanations.collection_status
            : collection_it->second;
    for (const StreamStateTimeline& timeline : device.timelines) {
      for (std::size_t index = 0; index < timeline.intervals.size(); ++index) {
        const StreamStateInterval& input = timeline.intervals[index];
        if (input.end_ns <= input.start_ns) {
          throw std::invalid_argument(
              "idle evidence stream state is not positive");
        }
        StreamStateSqlRow row;
        row.state_id = metadata.run_id + ":stream_state:" +
                       std::to_string(options.db_idx) + ':' +
                       std::to_string(device.device_id) + ':' +
                       std::to_string(timeline.stream_id) + ':' +
                       std::to_string(index);
        row.run_id = metadata.run_id;
        row.db_idx = options.db_idx;
        row.device_id = device.device_id;
        row.stream_id = timeline.stream_id;
        row.state_order = static_cast<std::uint32_t>(index);
        row.start_ns = input.start_ns;
        row.end_ns = input.end_ns;
        row.duration_ns =
            static_cast<std::uint64_t>(input.end_ns - input.start_ns);
        row.duration_us = ns_to_us(row.duration_ns);
        row.state = stream_state_name(input.state);
        row.source_count = input.source_links.size();
        row.stream_universe_kind = "observed";
        row.stream_universe_size = device.stream_universe_size;
        row.observed_stream_count = device.stream_universe_size;
        row.observed_universe_scan_complete =
            device.observed_universe_scan_complete;
        row.collection_status = collection_status_name(device_collection);
        row.clock_domain = "device";
        row.contract_version = options.contract_version;
        row.semantic_rules_version = productive.semantic_rules_version;
        row.attribution_rule_version = explanations.attribution_rule_version;
        out.stream_states.push_back(row);
        for (std::size_t source_index = 0;
             source_index < input.source_links.size(); ++source_index) {
          const StreamStateSourceLink& source = input.source_links[source_index];
          EvidenceLinkSqlRow link = evidence_link(
              ir, "stream_state", row.state_id,
              static_cast<std::uint32_t>(source_index), source.trace_event_id,
              "none", "none", input.start_ns, input.end_ns);
          link.has_overlap = false;
          link.has_stream_id = true;
          link.stream_id = timeline.stream_id;
          link.state = row.state;
          link.matched_rule_id = source.matched_rule_id.value_or("");
          out.evidence_links.push_back(std::move(link));
        }
      }
    }
  }

  for (const IdleExplanationDeviceResult& device : explanations.devices) {
    const std::vector<const DeviceIntervalSqlRow*>& device_gaps =
        gaps[device.device_id];
    std::size_t gap_index = 0;
    for (std::size_t index = 0; index < device.explanations.size(); ++index) {
      const IdleExplanationRow& input = device.explanations[index];
      while (gap_index < device_gaps.size() &&
             device_gaps[gap_index]->end_ns <= input.start_ns) {
        ++gap_index;
      }
      if (input.end_ns <= input.start_ns || gap_index >= device_gaps.size() ||
          input.start_ns < device_gaps[gap_index]->start_ns ||
          input.end_ns > device_gaps[gap_index]->end_ns) {
        throw std::invalid_argument(
            "idle explanation does not belong to one productive gap");
      }
      IdleExplanationSqlRow row;
      row.idle_explanation_id = metadata.run_id + ":idle_explanation:" +
                                std::to_string(options.db_idx) + ':' +
                                std::to_string(device.device_id) + ':' +
                                std::to_string(index);
      row.run_id = metadata.run_id;
      row.gap_interval_id = device_gaps[gap_index]->interval_id;
      row.db_idx = options.db_idx;
      row.device_id = device.device_id;
      row.explanation_order = static_cast<std::uint32_t>(index);
      row.start_ns = input.start_ns;
      row.end_ns = input.end_ns;
      row.duration_ns = static_cast<std::uint64_t>(input.end_ns - input.start_ns);
      row.duration_us = ns_to_us(row.duration_ns);
      row.category = idle_explanation_category_name(input.category);
      row.evidence_level = idle_evidence_level_name(input.evidence_level);
      row.evidence_relation =
          idle_evidence_relation_name(input.evidence_relation);
      row.alignment_status = input.alignment_status;
      row.collection_status = collection_status_name(device.collection_status);
      row.reason = input.reason;
      row.source_count = input.source_links.size();
      row.clock_domain = "device";
      row.contract_version = options.contract_version;
      row.semantic_rules_version = productive.semantic_rules_version;
      row.attribution_rule_version = explanations.attribution_rule_version;
      out.idle_explanations.push_back(row);
      for (std::size_t source_index = 0;
           source_index < input.source_links.size(); ++source_index) {
        const IdleExplanationSourceLink& source = input.source_links[source_index];
        EvidenceLinkSqlRow link = evidence_link(
            ir, "explanation", row.idle_explanation_id,
            static_cast<std::uint32_t>(source_index),
            source.source.trace_event_id, row.evidence_relation,
            row.evidence_level, input.start_ns, input.end_ns);
        if (!link.has_overlap) {
          throw std::invalid_argument(
              "idle explanation source does not overlap its owning slice");
        }
        if (row.evidence_relation == "none") {
          // Unknown/ambiguous sources remain diagnostic lineage only. The
          // frozen relation=none encoding has no temporal evidence extent.
          link.has_overlap = false;
        }
        link.has_stream_id = true;
        link.stream_id = source.stream_id;
        link.state = stream_state_name(source.state);
        link.matched_rule_id = source.source.matched_rule_id.value_or("");
        out.evidence_links.push_back(std::move(link));
      }
    }
  }

  for (const AnchorIdleExplanationRow& input : attribution.anchors) {
    AnchorIdleExplanationSqlRow row;
    row.anchor_id = input.anchor_id;
    row.run_id = metadata.run_id;
    row.db_idx = input.db_idx;
    row.device_id = input.device_id;
    row.anchor_idx = input.anchor_idx;
    row.category = input.category;
    row.evidence_level = input.evidence_level;
    row.slice_count = input.slice_count;
    row.duration_ns = input.duration_ns;
    row.duration_us = ns_to_us(input.duration_ns);
    out.anchor_attribution.push_back(std::move(row));
  }
  for (const NodeIdleExplanationRow& input : attribution.nodes) {
    NodeIdleExplanationSqlRow row;
    row.node_id = input.node_id;
    row.run_id = metadata.run_id;
    row.db_idx = input.db_idx;
    row.device_id = input.device_id;
    row.view_name = input.view_name;
    row.category = input.category;
    row.evidence_level = input.evidence_level;
    row.slice_count = input.slice_count;
    row.duration_ns = input.duration_ns;
    row.duration_us = ns_to_us(input.duration_ns);
    out.node_attribution.push_back(std::move(row));
  }
  return out;
}

}  // namespace traceloom::compat
