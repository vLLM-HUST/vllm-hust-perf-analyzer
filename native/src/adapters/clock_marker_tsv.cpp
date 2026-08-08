#include "traceloom/adapters/clock_marker_tsv.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace traceloom {
namespace {

std::vector<std::string> split_tsv(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    const std::size_t tab = line.find('\t', start);
    fields.push_back(line.substr(start, tab - start));
    if (tab == std::string::npos) {
      return fields;
    }
    start = tab + 1;
  }
}

template <typename Integer>
Integer parse_integer(const std::string& value,
                      std::size_t line,
                      const char* field) {
  if (value.empty()) {
    throw std::invalid_argument("empty clock marker " + std::string(field) +
                                " at line " + std::to_string(line));
  }
  Integer result{};
  const char* begin = value.data();
  const char* end = begin + value.size();
  const auto parsed = std::from_chars(begin, end, result, 10);
  if (parsed.ec != std::errc() || parsed.ptr != end) {
    throw std::invalid_argument("invalid clock marker " + std::string(field) +
                                " at line " + std::to_string(line));
  }
  return result;
}

}  // namespace

ClockMarkerTsvLoadResult load_clock_marker_tsv(const std::string& path,
                                               NativeIr& ir) {
  std::ifstream input(path);
  if (!input) {
    throw std::invalid_argument("cannot open clock marker TSV: " + path);
  }
  static const std::vector<std::string> kHeader{
      "marker_id",          "host_before_ns", "host_after_ns",
      "device_timestamp_ns", "host_pid",       "host_tid",
      "device_id",          "stream_id",       "connection_id",
      "call_site",          "return_status"};

  ClockMarkerTsvLoadResult result;
  result.source_ref_id =
      ir.source_refs.append("clock_marker_tsv", path, "clock_marker", 0);
  std::string line;
  std::size_t line_number = 0;
  bool saw_header = false;
  while (std::getline(input, line)) {
    ++line_number;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const std::vector<std::string> fields = split_tsv(line);
    if (!saw_header) {
      if (fields != kHeader) {
        throw std::invalid_argument(
            "invalid clock marker TSV header in " + path);
      }
      saw_header = true;
      continue;
    }
    if (fields.size() != kHeader.size()) {
      throw std::invalid_argument(
          "clock marker row at line " + std::to_string(line_number) +
          " must contain eleven tab-separated fields");
    }
    if (fields[0].empty() || fields[9].empty()) {
      throw std::invalid_argument(
          "clock marker id/call_site is empty at line " +
          std::to_string(line_number));
    }
    const bool has_stream = !fields[7].empty();
    const bool has_connection = !fields[8].empty();
    const std::int64_t return_status = parse_integer<std::int64_t>(
        fields[10], line_number, "return_status");
    const std::uint64_t raw_device =
        parse_integer<std::uint64_t>(fields[6], line_number, "device_id");
    if (raw_device > std::numeric_limits<std::uint32_t>::max()) {
      throw std::invalid_argument("clock marker device_id is out of range at line " +
                                  std::to_string(line_number));
    }
    ir.clock_markers.append(
        result.source_ref_id, line_number, ir.symbols.intern(fields[0]),
        parse_integer<std::int64_t>(fields[1], line_number,
                                    "host_before_ns"),
        parse_integer<std::int64_t>(fields[2], line_number, "host_after_ns"),
        parse_integer<std::int64_t>(fields[3], line_number,
                                    "device_timestamp_ns"),
        parse_integer<std::uint64_t>(fields[4], line_number, "host_pid"),
        parse_integer<std::uint64_t>(fields[5], line_number, "host_tid"),
        static_cast<std::uint32_t>(raw_device), has_stream,
        has_stream
            ? parse_integer<std::uint64_t>(fields[7], line_number, "stream_id")
            : 0,
        has_connection,
        has_connection ? parse_integer<std::int64_t>(
                             fields[8], line_number, "connection_id")
                       : -1,
        ir.symbols.intern(fields[9]), return_status);
    ++result.marker_count;
    if (return_status != 0) {
      ++result.rejected_marker_count;
    }
  }
  if (!saw_header) {
    throw std::invalid_argument("clock marker TSV has no header: " + path);
  }
  return result;
}

namespace {

bool global_tid_matches(std::uint64_t global_tid,
                        std::uint64_t host_pid,
                        std::uint64_t host_tid) {
  if (global_tid == host_tid) {
    return true;
  }
  return (global_tid >> 32u) == host_pid &&
         (global_tid & 0xffffffffULL) == host_tid;
}

std::string api_name(const NativeIr& ir, const HostApiEventRow& api) {
  return api.api_name_symbol_id.valid()
             ? ir.symbols.value(api.api_name_symbol_id)
             : std::string();
}

using ThreadKey = std::pair<std::uint64_t, std::uint64_t>;

struct BracketWindow {
  std::int64_t host_before_ns = 0;
  std::int64_t record_after_ns = 0;
  std::int64_t host_after_ns = 0;
};

long double interval_midpoint(std::int64_t start_ns, std::int64_t end_ns) {
  return static_cast<long double>(start_ns) +
         static_cast<long double>(end_ns - start_ns) / 2.0L;
}

struct OrderedBijectionValidation {
  bool valid = false;
  std::vector<long double> residuals_ns;
};

OrderedBijectionValidation validate_order_preserving_bijection(
    const std::vector<BracketWindow>& brackets,
    const std::vector<const HostApiEventRow*>& apis) {
  OrderedBijectionValidation validation;
  // One point cannot identify an affine clock relation. A single bracket may
  // still resolve through direct temporal overlap, but never through ordinal
  // fallback alone.
  if (brackets.size() < 2 || brackets.size() != apis.size()) {
    return validation;
  }

  std::vector<long double> bracket_midpoints;
  std::vector<long double> api_midpoints;
  bracket_midpoints.reserve(brackets.size());
  api_midpoints.reserve(apis.size());
  for (std::size_t index = 0; index < brackets.size(); ++index) {
    bracket_midpoints.push_back(interval_midpoint(
        brackets[index].host_before_ns, brackets[index].record_after_ns));
    api_midpoints.push_back(
        interval_midpoint(apis[index]->start_ns, apis[index]->end_ns));
    if (index > 0 &&
        (bracket_midpoints[index] <= bracket_midpoints[index - 1] ||
         api_midpoints[index] <= api_midpoints[index - 1])) {
      return validation;
    }
  }

  // CLOCK_REALTIME brackets and msprof host timestamps are different host
  // clocks in practice: a long capture can carry a sub-millisecond offset and
  // a small rate difference. Map the first/last profiler API midpoints onto
  // the first/last bracket midpoints before checking identity. Every mapped
  // API must still have its same-ordinal bracket as the unique nearest one.
  const long double api_span = api_midpoints.back() - api_midpoints.front();
  const long double bracket_span =
      bracket_midpoints.back() - bracket_midpoints.front();
  if (api_span <= 0.0L || bracket_span <= 0.0L) {
    return validation;
  }
  const long double scale = bracket_span / api_span;
  validation.residuals_ns.reserve(api_midpoints.size());
  for (std::size_t api_index = 0; api_index < api_midpoints.size();
       ++api_index) {
    const long double mapped_api_midpoint =
        bracket_midpoints.front() +
        scale * (api_midpoints[api_index] - api_midpoints.front());
    std::size_t nearest_index = 0;
    long double nearest_distance =
        std::numeric_limits<long double>::infinity();
    bool tied = false;
    for (std::size_t bracket_index = 0;
         bracket_index < bracket_midpoints.size(); ++bracket_index) {
      const long double distance = std::fabs(
          bracket_midpoints[bracket_index] - mapped_api_midpoint);
      if (distance < nearest_distance) {
        nearest_index = bracket_index;
        nearest_distance = distance;
        tied = false;
      } else if (distance == nearest_distance) {
        tied = true;
      }
    }
    if (tied || nearest_index != api_index) {
      validation.residuals_ns.clear();
      return validation;
    }
    validation.residuals_ns.push_back(std::fabs(
        bracket_midpoints[api_index] - mapped_api_midpoint));
  }
  validation.valid = true;
  return validation;
}

}  // namespace

ClockMarkerTsvLoadResult resolve_ascend_clock_marker_bracket_tsv(
    const std::string& path,
    NativeIr& ir) {
  std::ifstream input(path);
  if (!input) {
    throw std::invalid_argument("cannot open clock marker bracket TSV: " +
                                path);
  }
  static const std::vector<std::string> kHeader{
      "marker_id",       "host_before_ns", "record_after_ns",
      "host_after_ns",   "host_pid",       "host_tid",
      "device_id",       "stream_id",      "call_site",
      "return_status"};

  ClockMarkerTsvLoadResult result;
  result.source_ref_id = ir.source_refs.append(
      "clock_marker_bracket_tsv", path, "clock_marker", 0);

  std::map<ThreadKey, std::vector<BracketWindow>>
      successful_brackets_by_thread;
  {
    std::string scan_line;
    std::size_t scan_line_number = 0;
    bool scan_saw_header = false;
    while (std::getline(input, scan_line)) {
      ++scan_line_number;
      if (!scan_line.empty() && scan_line.back() == '\r') {
        scan_line.pop_back();
      }
      if (scan_line.empty() || scan_line.front() == '#') {
        continue;
      }
      const std::vector<std::string> scan_fields = split_tsv(scan_line);
      if (!scan_saw_header) {
        if (scan_fields != kHeader) {
          throw std::invalid_argument(
              "invalid clock marker bracket TSV header in " + path);
        }
        scan_saw_header = true;
        continue;
      }
      if (scan_fields.size() != kHeader.size()) {
        throw std::invalid_argument(
            "clock marker bracket row at line " +
            std::to_string(scan_line_number) +
            " must contain ten tab-separated fields");
      }
      if (parse_integer<std::int64_t>(scan_fields[9], scan_line_number,
                                      "return_status") == 0) {
        const ThreadKey key{
            parse_integer<std::uint64_t>(scan_fields[4], scan_line_number,
                                         "host_pid"),
            parse_integer<std::uint64_t>(scan_fields[5], scan_line_number,
                                         "host_tid")};
        successful_brackets_by_thread[key].push_back(BracketWindow{
            parse_integer<std::int64_t>(scan_fields[1], scan_line_number,
                                        "host_before_ns"),
            parse_integer<std::int64_t>(scan_fields[2], scan_line_number,
                                        "record_after_ns"),
            parse_integer<std::int64_t>(scan_fields[3], scan_line_number,
                                        "host_after_ns")});
      }
    }
    input.clear();
    input.seekg(0);
  }

  std::map<ThreadKey, std::vector<const HostApiEventRow*>> record_apis_by_thread;
  for (const auto& item : successful_brackets_by_thread) {
    std::vector<const HostApiEventRow*>& apis =
        record_apis_by_thread[item.first];
    for (const HostApiEventRow& api : ir.host_api_events.rows()) {
      if (global_tid_matches(api.raw_global_tid, item.first.first,
                             item.first.second) &&
          api_name(ir, api) == "aclrtRecordEvent") {
        apis.push_back(&api);
      }
    }
    std::stable_sort(apis.begin(), apis.end(),
                     [](const HostApiEventRow* lhs,
                        const HostApiEventRow* rhs) {
                       if (lhs->start_ns != rhs->start_ns) {
                         return lhs->start_ns < rhs->start_ns;
                       }
                       if (lhs->end_ns != rhs->end_ns) {
                         return lhs->end_ns < rhs->end_ns;
                       }
                       return lhs->id < rhs->id;
                     });
  }
  std::map<ThreadKey, OrderedBijectionValidation> ordered_bijections;
  for (const auto& item : successful_brackets_by_thread) {
    ordered_bijections[item.first] = validate_order_preserving_bijection(
        item.second, record_apis_by_thread[item.first]);
  }
  std::map<ThreadKey, std::size_t> successful_bracket_ordinals;
  std::string line;
  std::size_t line_number = 0;
  bool saw_header = false;
  while (std::getline(input, line)) {
    ++line_number;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const std::vector<std::string> fields = split_tsv(line);
    if (!saw_header) {
      if (fields != kHeader) {
        throw std::invalid_argument(
            "invalid clock marker bracket TSV header in " + path);
      }
      saw_header = true;
      continue;
    }
    if (fields.size() != kHeader.size()) {
      throw std::invalid_argument(
          "clock marker bracket row at line " +
          std::to_string(line_number) +
          " must contain ten tab-separated fields");
    }
    if (fields[0].empty() || fields[8].empty()) {
      throw std::invalid_argument(
          "clock marker bracket id/call_site is empty at line " +
          std::to_string(line_number));
    }
    const std::int64_t host_before = parse_integer<std::int64_t>(
        fields[1], line_number, "host_before_ns");
    const std::int64_t record_after = parse_integer<std::int64_t>(
        fields[2], line_number, "record_after_ns");
    const std::int64_t host_after = parse_integer<std::int64_t>(
        fields[3], line_number, "host_after_ns");
    if (host_before < 0 || record_after < host_before ||
        host_after < record_after) {
      throw std::invalid_argument(
          "clock marker bracket timestamps are not ordered at line " +
          std::to_string(line_number));
    }
    const std::uint64_t host_pid = parse_integer<std::uint64_t>(
        fields[4], line_number, "host_pid");
    const std::uint64_t host_tid = parse_integer<std::uint64_t>(
        fields[5], line_number, "host_tid");
    const std::uint64_t raw_device = parse_integer<std::uint64_t>(
        fields[6], line_number, "device_id");
    if (raw_device > std::numeric_limits<std::uint32_t>::max()) {
      throw std::invalid_argument(
          "clock marker bracket device_id is out of range at line " +
          std::to_string(line_number));
    }
    const std::uint32_t device_id =
        static_cast<std::uint32_t>(raw_device);
    const bool has_stream = !fields[7].empty();
    const std::uint64_t stream_id =
        has_stream ? parse_integer<std::uint64_t>(fields[7], line_number,
                                                  "stream_id")
                   : 0;
    const std::int64_t return_status = parse_integer<std::int64_t>(
        fields[9], line_number, "return_status");

    std::int64_t device_timestamp_ns = -1;
    bool has_connection = false;
    std::int64_t connection_id = -1;
    bool resolution_rejected = false;
    bool has_profiler_host_interval = false;
    std::int64_t profiler_host_start_ns = 0;
    std::int64_t profiler_host_end_ns = 0;
    ClockMarkerResolutionMethod resolution_method =
        ClockMarkerResolutionMethod::kUnresolved;
    bool has_resolution_residual = false;
    long double resolution_residual_ns = 0.0L;
    if (return_status == 0) {
      const ThreadKey thread_key{host_pid, host_tid};
      const std::size_t bracket_ordinal =
          successful_bracket_ordinals[thread_key]++;
      std::vector<const HostApiEventRow*> record_apis;
      for (const HostApiEventRow& api : ir.host_api_events.rows()) {
        // Match against the narrow caller bracket around aclrtRecordEvent,
        // never the outer bracket that also contains device completion and
        // aclrtSynchronizeEvent. Non-empty overlap plus exact-one identity
        // remains fail-closed across the two host clock domains.
        if (api.start_ns < record_after && api.end_ns > host_before &&
            global_tid_matches(api.raw_global_tid, host_pid, host_tid) &&
            api_name(ir, api) == "aclrtRecordEvent") {
          record_apis.push_back(&api);
        }
      }
      const std::vector<const HostApiEventRow*>& thread_apis =
          record_apis_by_thread[thread_key];
      const OrderedBijectionValidation& ordered_validation =
          ordered_bijections[thread_key];
      const bool has_ordered_bijection =
          ordered_validation.valid &&
          bracket_ordinal < thread_apis.size();
      const HostApiEventRow* ordered_api =
          has_ordered_bijection ? thread_apis[bracket_ordinal] : nullptr;
      const HostApiEventRow* resolved_api = nullptr;
      if (record_apis.size() == 1) {
        if (ordered_api != nullptr && record_apis.front() != ordered_api) {
          throw std::invalid_argument(
              "clock marker bracket at line " +
              std::to_string(line_number) +
              " has overlap/order disagreement for aclrtRecordEvent");
        }
        resolved_api = record_apis.front();
        resolution_method = ClockMarkerResolutionMethod::kDirectOverlap;
      } else if (record_apis.empty() && ordered_api != nullptr) {
        resolved_api = ordered_api;
        resolution_method =
            ClockMarkerResolutionMethod::kOrdinalAffineFallback;
        has_resolution_residual = true;
        resolution_residual_ns =
            ordered_validation.residuals_ns[bracket_ordinal];
      } else {
        throw std::invalid_argument(
            "clock marker bracket at line " +
            std::to_string(line_number) + " overlaps " +
            std::to_string(record_apis.size()) +
            " matching aclrtRecordEvent rows and has no unique "
            "order-preserving fallback");
      }
      has_profiler_host_interval = true;
      profiler_host_start_ns = resolved_api->start_ns;
      profiler_host_end_ns = resolved_api->end_ns;
      connection_id = resolved_api->raw_connection_id;
      if (connection_id < 0) {
        resolution_rejected = true;
      } else {
        has_connection = true;
        std::vector<const TraceEventRow*> task_events;
        for (const TaskRow& task : ir.tasks.rows()) {
          if (task.raw_connection_id != connection_id ||
              !task.trace_event_id.valid() ||
              task.trace_event_id.value() >= ir.trace_events.size()) {
            continue;
          }
          const TraceEventRow& event =
              ir.trace_events.row(task.trace_event_id);
          if (event.device_id == device_id &&
              (!has_stream || event.stream_id == stream_id)) {
            task_events.push_back(&event);
          }
        }
        if (task_events.empty()) {
          // A profiler can stop its device TASK horizon before the final host
          // API rows are flushed. The API identity is still auditable, but it
          // cannot supply a cross-clock observation. Retain it as a rejected
          // marker instead of discarding an otherwise usable capture.
          resolution_rejected = true;
        } else if (task_events.size() > 1) {
          throw std::invalid_argument(
              "clock marker bracket at line " +
              std::to_string(line_number) + " has connectionId " +
              std::to_string(connection_id) + " with " +
              std::to_string(task_events.size()) +
              " matching TASK rows; at most one is required");
        } else {
          device_timestamp_ns = task_events.front()->start_ns;
        }
      }
    } else {
      resolution_rejected = true;
    }
    if (resolution_rejected) {
      ++result.rejected_marker_count;
    }

    ir.clock_markers.append(
        result.source_ref_id, line_number, ir.symbols.intern(fields[0]),
        host_before, host_after, device_timestamp_ns, host_pid, host_tid,
        device_id, has_stream, stream_id, has_connection, connection_id,
        ir.symbols.intern(fields[8]), return_status,
        has_profiler_host_interval, profiler_host_start_ns,
        profiler_host_end_ns, resolution_method, has_resolution_residual,
        resolution_residual_ns, true, record_after);
    ++result.marker_count;
  }
  if (!saw_header) {
    throw std::invalid_argument(
        "clock marker bracket TSV has no header: " + path);
  }
  return result;
}

}  // namespace traceloom
