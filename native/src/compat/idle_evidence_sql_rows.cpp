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

std::string fixed_decimal(long double value, int precision) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}

std::string fixed_scale(long double value) {
  return fixed_decimal(value, 12);
}

std::string symbol_text(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

void append_digest_string(std::ostringstream& out, const std::string& value) {
  out << value.size() << ':' << value << ';';
}

std::string clock_marker_sha256(const NativeIr& ir) {
  std::ostringstream payload;
  for (const ClockMarkerRow& marker : ir.clock_markers.rows()) {
    const SourceRefRow& source = ir.source_refs.row(marker.source_ref_id);
    append_digest_string(payload, source.source_kind);
    append_digest_string(payload, source.table_name);
    payload << marker.source_row_id << ';';
    append_digest_string(payload, symbol_text(ir, marker.marker_symbol_id));
    payload << marker.host_before_ns << ';' << marker.host_after_ns << ';'
            << marker.device_timestamp_ns << ';' << marker.host_pid << ';'
            << marker.host_tid << ';' << marker.device_id << ';'
            << marker.has_stream_id << ';' << marker.stream_id << ';'
            << marker.has_connection_id << ';' << marker.raw_connection_id
            << ';';
    append_digest_string(payload,
                         symbol_text(ir, marker.call_site_symbol_id));
    payload << marker.return_status << '\n';
  }
  return sha256_hex(payload.str());
}

std::string build_metadata_json(
    const NativeIr& ir,
    const ProductiveTimelineRunResult& productive,
    const IdleExplanationRunResult& explanations,
    const ClockAlignmentRunResult& alignment,
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
      << json_string(std::string(analysis_status_name(explanations.status)))
      << ",\"attribution_rule_version\":"
      << json_string(explanations.attribution_rule_version)
      << ",\"clock_marker_count\":" << ir.clock_markers.size()
      << ",\"clock_marker_sha256\":"
      << json_string(clock_marker_sha256(ir))
      << ",\"clock_model_version\":"
      << json_string(alignment.model_version)
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
    const ClockModel* model = alignment.find_device(device.device_id);
    out << "{\"absolute_residual_max_ns\":"
        << json_string(model == nullptr
                           ? "0.000000"
                           : fixed_decimal(
                                 model->absolute_residual_max_ns, 6))
        << ",\"absolute_residual_p50_ns\":"
        << json_string(model == nullptr
                           ? "0.000000"
                           : fixed_decimal(
                                 model->absolute_residual_p50_ns, 6))
        << ",\"absolute_residual_p95_ns\":"
        << json_string(model == nullptr
                           ? "0.000000"
                           : fixed_decimal(
                                 model->absolute_residual_p95_ns, 6))
        << ",\"alignment_status\":"
        << json_string(model == nullptr
                           ? "uncalibrated"
                           : std::string(alignment_status_name(
                                 model->alignment_status)))
        << ",\"bracket_uncertainty_p95_ns\":"
        << json_string(model == nullptr
                           ? "0.000000"
                           : fixed_decimal(
                                 model->bracket_uncertainty_p95_ns, 6))
        << ",\"device_id\":" << device.device_id
        << ",\"drift_ppm\":"
        << json_string(model == nullptr
                           ? "0.000000"
                           : fixed_decimal(model->drift_ppm, 6))
        << ",\"epsilon_ns\":" << (model == nullptr ? 0 : model->epsilon_ns)
        << ",\"fit_marker_count\":"
        << (model == nullptr ? 0 : model->fit_marker_count)
        << ",\"inlier_marker_count\":"
        << (model == nullptr ? 0 : model->inlier_marker_count)
        << ",\"input_marker_count\":"
        << (model == nullptr ? 0 : model->input_marker_count)
        << ",\"offset_ns\":"
        << json_string(model == nullptr
                           ? "0.000000"
                           : fixed_decimal(model->offset_ns, 6))
        << ",\"reference_device_ns\":"
        << json_string(model == nullptr
                           ? "0.000000"
                           : fixed_decimal(model->reference_device_ns, 6))
        << ",\"reference_host_ns\":"
        << json_string(model == nullptr
                           ? "0.000000"
                           : fixed_decimal(model->reference_host_ns, 6))
        << ",\"rejected_marker_count\":"
        << (model == nullptr ? 0 : model->rejected_marker_count)
        << ",\"scale\":"
        << json_string(model == nullptr ? "1.000000000000"
                                        : fixed_scale(model->scale))
        << ",\"span_end_ns\":" << nullable_integer(device.span_end_ns)
        << ",\"span_start_ns\":" << nullable_integer(device.span_start_ns)
        << ",\"status\":"
        << json_string(std::string(analysis_status_name(device.status)))
        << ",\"validation_marker_count\":"
        << (model == nullptr ? 0 : model->validation_marker_count) << '}';
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

SourceIdentity host_source_identity(const NativeIr& ir,
                                    HostApiEventId id) {
  if (!id.valid() || id.value() >= ir.host_api_events.size()) {
    throw std::invalid_argument(
        "idle evidence lineage has an invalid host API event id");
  }
  const HostApiEventRow& event = ir.host_api_events.row(id);
  const SourceRefRow& source = ir.source_refs.row(event.source_ref_id);
  return SourceIdentity{source.source_kind, source.table_name,
                        std::to_string(event.source_row_id), "",
                        event.start_ns, event.end_ns};
}

SourceIdentity marker_source_identity(const NativeIr& ir,
                                      ClockMarkerId id) {
  if (!id.valid() || id.value() >= ir.clock_markers.size()) {
    throw std::invalid_argument(
        "clock model lineage has an invalid marker id");
  }
  const ClockMarkerRow& marker = ir.clock_markers.row(id);
  const SourceRefRow& source = ir.source_refs.row(marker.source_ref_id);
  return SourceIdentity{source.source_kind, source.table_name,
                        std::to_string(marker.source_row_id), "",
                        marker.device_timestamp_ns,
                        marker.device_timestamp_ns};
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

EvidenceLinkSqlRow non_event_evidence_link(
    const SourceIdentity& source,
    const std::string& owner_kind,
    const std::string& owner_id,
    std::uint32_t ordinal,
    const std::string& relation,
    const std::string& level,
    bool has_overlap,
    std::int64_t overlap_start_ns,
    std::int64_t overlap_end_ns) {
  EvidenceLinkSqlRow row;
  row.owner_kind = owner_kind;
  row.owner_id = owner_id;
  row.evidence_ordinal = ordinal;
  row.source_kind = source.source_kind;
  row.source_table = source.source_table;
  row.source_key = source.source_key;
  row.relation = relation;
  row.evidence_level = level;
  row.has_overlap = has_overlap;
  row.overlap_start_ns = overlap_start_ns;
  row.overlap_end_ns = overlap_end_ns;
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

std::string host_api_id(const std::string& run_id,
                        std::uint32_t db_idx,
                        HostApiEventId id) {
  return run_id + ":host_api:" + std::to_string(db_idx) + ':' +
         std::to_string(id.value());
}

std::string clock_model_id(const std::string& run_id,
                           std::uint32_t db_idx,
                           std::uint32_t device_id) {
  return run_id + ":clock_model:" + std::to_string(db_idx) + ':' +
         std::to_string(device_id);
}

std::int64_t midpoint_floor(std::int64_t before, std::int64_t after) {
  const int remainder = static_cast<int>(before % 2) +
                        static_cast<int>(after % 2);
  return before / 2 + after / 2 + remainder / 2 -
         (remainder < 0 && remainder % 2 != 0 ? 1 : 0);
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
  IdleEvidenceSqlRowOptions resolved_options = options;
  if (resolved_options.host_api_rules_version == "not_loaded" &&
      pipeline.host_correlation.host_api_rules_version != "not_loaded") {
    resolved_options.host_api_rules_version =
        pipeline.host_correlation.host_api_rules_version;
    resolved_options.host_api_rules_sha256 =
        pipeline.host_correlation.host_api_rules_sha256;
  }
  // E3 validates every interval-bearing and unknown task, so it can
  // legitimately degrade to invalid_input even when E2's productive-only
  // projection remains valid. E4 deliberately carries the conservative join
  // of those statuses. Reject only a pipeline whose E4 result does not match
  // that contract; do not turn an auditable invalid-input result into a
  // materialization failure.
  const AnalysisStatus expected_status =
      productive.status == AnalysisStatus::kInvalidInput ||
              streams.status == AnalysisStatus::kInvalidInput
          ? AnalysisStatus::kInvalidInput
          : productive.status;
  if (explanations.status != expected_status) {
    throw std::invalid_argument(
        "idle evidence pipeline stages disagree on analysis status");
  }

  RunMetadataSqlRow metadata;
  metadata.analysis_status = analysis_status_name(explanations.status);
  metadata.contract_version = resolved_options.contract_version;
  metadata.semantic_rules_version = productive.semantic_rules_version;
  metadata.semantic_rules_sha256 = productive.semantic_rules_sha256;
  metadata.attribution_rule_version = explanations.attribution_rule_version;
  metadata.host_api_rules_version = resolved_options.host_api_rules_version;
  metadata.host_api_rules_sha256 = resolved_options.host_api_rules_sha256;
  metadata.collection_status = collection_status_name(explanations.collection_status);
  metadata.db_idx = resolved_options.db_idx;
  metadata.source_kind = resolved_options.source_kind;
  metadata.source_path = resolved_options.source_path;
  metadata.metadata_json = build_metadata_json(
      ir, productive, explanations, pipeline.clock_alignment,
      resolved_options);
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

  std::map<ClockMarkerId, std::pair<std::string, std::string>> marker_usage;
  for (const ClockModel& model : pipeline.clock_alignment.models) {
    const std::string model_id = clock_model_id(
        metadata.run_id, resolved_options.db_idx, model.device_id);
    for (ClockMarkerId marker_id : model.input_markers) {
      marker_usage[marker_id] = {model_id, "unused_invalid_model"};
    }
    for (ClockMarkerId marker_id : model.rejected_markers) {
      marker_usage[marker_id] = {model_id, "rejected_marker"};
    }
    for (ClockMarkerId marker_id : model.fit_markers) {
      marker_usage[marker_id] = {model_id, "fit_marker"};
    }
    for (ClockMarkerId marker_id : model.validation_markers) {
      marker_usage[marker_id] = {model_id, "validation_marker"};
    }
  }
  for (const ClockMarkerRow& input : ir.clock_markers.rows()) {
    const SourceRefRow& source = ir.source_refs.row(input.source_ref_id);
    const auto usage = marker_usage.find(input.id);
    ClockMarkerSqlRow row;
    row.clock_marker_id = metadata.run_id + ":clock_marker:" +
                          std::to_string(resolved_options.db_idx) + ':' +
                          std::to_string(input.id.value());
    row.run_id = metadata.run_id;
    row.clock_model_id =
        usage == marker_usage.end()
            ? clock_model_id(metadata.run_id, resolved_options.db_idx,
                             input.device_id)
            : usage->second.first;
    row.db_idx = resolved_options.db_idx;
    row.marker_id = symbol_text(ir, input.marker_symbol_id);
    row.host_before_ns = input.host_before_ns;
    row.host_after_ns = input.host_after_ns;
    row.host_midpoint_ns =
        midpoint_floor(input.host_before_ns, input.host_after_ns);
    row.device_timestamp_ns = input.device_timestamp_ns;
    row.host_pid = input.host_pid;
    row.host_tid = input.host_tid;
    row.device_id = input.device_id;
    row.has_stream_id = input.has_stream_id;
    row.stream_id = input.stream_id;
    row.has_connection_id = input.has_connection_id;
    row.connection_id = input.raw_connection_id;
    row.call_site = symbol_text(ir, input.call_site_symbol_id);
    row.return_status = input.return_status;
    row.marker_state = usage == marker_usage.end()
                           ? "unassigned"
                           : usage->second.second;
    row.source_kind = source.source_kind;
    row.source_table = source.table_name;
    row.source_key = std::to_string(input.source_row_id);
    row.contract_version = resolved_options.contract_version;
    out.clock_markers.push_back(std::move(row));
  }

  // Cross-clock model rows are emitted even when invalid or uncalibrated so
  // absence of promoted host evidence is directly auditable.
  for (const ClockModel& input : pipeline.clock_alignment.models) {
    ClockModelSqlRow row;
    row.clock_model_id = clock_model_id(
        metadata.run_id, resolved_options.db_idx, input.device_id);
    row.run_id = metadata.run_id;
    row.db_idx = options.db_idx;
    row.device_id = input.device_id;
    row.source_clock_domain = input.source_clock_domain;
    row.target_clock_domain = input.target_clock_domain;
    row.mapping_kind = input.mapping_kind;
    row.scale = fixed_scale(input.scale);
    row.offset_ns = fixed_decimal(input.offset_ns, 6);
    row.reference_host_ns = fixed_decimal(input.reference_host_ns, 6);
    row.reference_device_ns = fixed_decimal(input.reference_device_ns, 6);
    row.drift_ppm = static_cast<double>(input.drift_ppm);
    row.fit_method = input.fit_method;
    row.fit_method_version = input.fit_method_version;
    row.fit_random_seed = input.fit_random_seed;
    row.input_marker_count = input.input_marker_count;
    row.inlier_marker_count = input.inlier_marker_count;
    row.rejected_marker_count = input.rejected_marker_count;
    row.fit_marker_count = input.fit_marker_count;
    row.validation_marker_count = input.validation_marker_count;
    row.absolute_residual_p50_ns =
        static_cast<double>(input.absolute_residual_p50_ns);
    row.absolute_residual_p95_ns =
        static_cast<double>(input.absolute_residual_p95_ns);
    row.absolute_residual_max_ns =
        static_cast<double>(input.absolute_residual_max_ns);
    row.bracket_uncertainty_p95_ns =
        static_cast<double>(input.bracket_uncertainty_p95_ns);
    row.epsilon_ns = input.epsilon_ns;
    row.alignment_status = alignment_status_name(input.alignment_status);
    row.reason = input.reason;
    out.clock_models.push_back(row);

    std::uint32_t ordinal = 0;
    for (ClockMarkerId marker_id : input.input_markers) {
      EvidenceLinkSqlRow link = non_event_evidence_link(
          marker_source_identity(ir, marker_id), "clock_model",
          row.clock_model_id, ordinal++, "none", "none", false, 0, 0);
      const auto usage = marker_usage.find(marker_id);
      link.state = usage == marker_usage.end() ? "unassigned"
                                               : usage->second.second;
      out.evidence_links.push_back(std::move(link));
    }
  }

  std::map<HostApiEventId,
           const HostCorrelationRunResult::HostApiClassificationRow*>
      host_classification;
  for (const auto& row :
       pipeline.host_correlation.host_api_classification) {
    host_classification[row.host_api_event_id] = &row;
  }
  for (const HostApiEventRow& input : ir.host_api_events.rows()) {
    if (input.end_ns <= input.start_ns) {
      throw std::invalid_argument("host API event is not a positive interval");
    }
    const SourceRefRow& source = ir.source_refs.row(input.source_ref_id);
    HostApiEventSqlRow row;
    row.api_event_id = host_api_id(metadata.run_id, options.db_idx, input.id);
    row.run_id = metadata.run_id;
    row.db_idx = options.db_idx;
    row.start_ns = input.start_ns;
    row.end_ns = input.end_ns;
    row.duration_ns = static_cast<std::uint64_t>(input.end_ns - input.start_ns);
    row.duration_us = ns_to_us(row.duration_ns);
    row.global_tid = input.raw_global_tid;
    row.connection_id = input.raw_connection_id;
    row.api_type = symbol_text(ir, input.api_type_symbol_id);
    row.api_name = symbol_text(ir, input.api_name_symbol_id);
    const auto classification = host_classification.find(input.id);
    if (classification != host_classification.end() &&
        classification->second->matched) {
      row.api_family = host_api_family_name(classification->second->family);
    }
    row.has_device_id = input.has_device_id;
    row.device_id = input.device_id;
    row.source_kind = source.source_kind;
    row.source_table = source.table_name;
    row.source_key = std::to_string(input.source_row_id);
    row.contract_version = resolved_options.contract_version;
    row.host_api_rules_version = resolved_options.host_api_rules_version;
    out.host_api_events.push_back(std::move(row));
  }

  for (std::size_t index = 0;
       index < pipeline.host_correlation.task_api_links.size(); ++index) {
    const TaskApiLinkRow& input =
        pipeline.host_correlation.task_api_links[index];
    const HostApiEventRow& api = ir.host_api_events.row(input.host_api_event_id);
    TaskApiLinkSqlRow row;
    row.task_api_link_id = metadata.run_id + ":task_api_link:" +
                           std::to_string(options.db_idx) + ':' +
                           std::to_string(index);
    row.run_id = metadata.run_id;
    row.api_event_id =
        host_api_id(metadata.run_id, options.db_idx, input.host_api_event_id);
    row.db_idx = options.db_idx;
    row.has_device_id = input.has_device_id;
    row.device_id = input.device_id;
    row.connection_id = input.raw_connection_id;
    row.link_status = task_api_link_status_name(input.link_status);
    row.api_name = symbol_text(ir, api.api_name_symbol_id);
    if (input.task_id.valid()) {
      const TaskRow& task = ir.tasks.row(input.task_id);
      const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
      row.trace_event_id = trace_event_compat_id(task.trace_event_id);
      row.has_stream_id = true;
      row.stream_id = event.stream_id;
      row.task_type = symbol_text(ir, task.task_type_symbol_id);
    }
    out.task_api_links.push_back(std::move(row));
  }

  for (std::size_t index = 0;
       index < pipeline.host_correlation.candidates.size(); ++index) {
    const HostIdleCandidateRow& input =
        pipeline.host_correlation.candidates[index];
    const auto gap_it = gaps.find(input.device_id);
    const DeviceIntervalSqlRow* owning_gap = nullptr;
    if (gap_it != gaps.end()) {
      for (const DeviceIntervalSqlRow* gap : gap_it->second) {
        if (gap->start_ns == input.gap_start_ns &&
            gap->end_ns == input.gap_end_ns) {
          owning_gap = gap;
          break;
        }
      }
    }
    if (owning_gap == nullptr) {
      throw std::invalid_argument(
          "idle candidate does not identify one productive gap");
    }
    IdleCandidateSqlRow row;
    row.candidate_id = metadata.run_id + ":idle_candidate:" +
                       std::to_string(options.db_idx) + ':' +
                       std::to_string(input.device_id) + ':' +
                       std::to_string(index);
    row.run_id = metadata.run_id;
    row.gap_interval_id = owning_gap->interval_id;
    row.db_idx = options.db_idx;
    row.device_id = input.device_id;
    row.candidate_order = static_cast<std::uint32_t>(index);
    row.candidate_category = host_evidence_category_name(input.category);
    row.candidate_level = input.candidate_level;
    row.candidate_relation = input.candidate_relation;
    row.candidate_status = host_candidate_status_name(input.candidate_status);
    row.reason = input.reason;
    row.alignment_status = alignment_status_name(input.alignment_status);
    row.source_count = 0;
    for (const HostEvidenceSourceLink& source : input.source_links) {
      ++row.source_count;
      if (source.task_id.valid()) {
        ++row.source_count;
      }
    }
    row.contract_version = resolved_options.contract_version;
    row.attribution_rule_version = explanations.attribution_rule_version;
    out.idle_candidates.push_back(row);
    std::uint32_t ordinal = 0;
    for (const HostEvidenceSourceLink& source : input.source_links) {
      out.evidence_links.push_back(non_event_evidence_link(
          host_source_identity(ir, source.host_api_event_id), "candidate",
          row.candidate_id, ordinal++, source.relation,
          input.candidate_level, true, source.overlap_start_ns,
          source.overlap_end_ns));
      if (source.task_id.valid()) {
        const TaskRow& task = ir.tasks.row(source.task_id);
        EvidenceLinkSqlRow task_link = evidence_link(
            ir, "candidate", row.candidate_id, ordinal++,
            task.trace_event_id, source.relation, input.candidate_level,
            source.overlap_start_ns, source.overlap_end_ns);
        // exact_connection_id is structural; the mapped possible-delay extent
        // belongs to the candidate rather than to the task's own interval.
        task_link.has_overlap = true;
        task_link.overlap_start_ns = source.overlap_start_ns;
        task_link.overlap_end_ns = source.overlap_end_ns;
        out.evidence_links.push_back(std::move(task_link));
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
      for (const HostEvidenceSourceLink& source : input.host_source_links) {
        ++row.source_count;
        if (source.task_id.valid()) {
          ++row.source_count;
        }
      }
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
      std::uint32_t host_ordinal =
          static_cast<std::uint32_t>(input.source_links.size());
      for (const HostEvidenceSourceLink& source : input.host_source_links) {
        const std::int64_t overlap_start =
            std::max(input.start_ns, source.overlap_start_ns);
        const std::int64_t overlap_end =
            std::min(input.end_ns, source.overlap_end_ns);
        if (overlap_end <= overlap_start) {
          throw std::invalid_argument(
              "host evidence does not cover its owning explanation slice");
        }
        out.evidence_links.push_back(non_event_evidence_link(
            host_source_identity(ir, source.host_api_event_id), "explanation",
            row.idle_explanation_id, host_ordinal++, source.relation,
            row.evidence_level, true, overlap_start, overlap_end));
        if (source.task_id.valid()) {
          const TaskRow& task = ir.tasks.row(source.task_id);
          out.evidence_links.push_back(non_event_evidence_link(
              source_identity(ir, task.trace_event_id), "explanation",
              row.idle_explanation_id, host_ordinal++, source.relation,
              row.evidence_level, true, overlap_start, overlap_end));
        }
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
