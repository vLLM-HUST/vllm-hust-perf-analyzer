#include "runtime_api_family.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace traceloom::compat::detail {
namespace {

bool contains(const std::string& value, const char* needle) {
  return value.find(needle) != std::string::npos;
}

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

struct ActivityRange {
  AnchorHostIntervalSqlRow* interval = nullptr;
  const ActivityGroup* group = nullptr;
  std::size_t lo = 0;
  std::size_t hi = 0;
};

std::uint64_t saturating_add(std::uint64_t lhs, std::uint64_t rhs) {
  return rhs > std::numeric_limits<std::uint64_t>::max() - lhs
             ? std::numeric_limits<std::uint64_t>::max()
             : lhs + rhs;
}

}  // namespace

std::string public_runtime_api_family(const std::string& api_name) {
  std::string name = api_name;
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (!(name.rfind("acl", 0) == 0 || name.rfind("cuda", 0) == 0 ||
        name.rfind("hip", 0) == 0)) {
    return {};
  }
  if (contains(name, "wait")) return "wait";
  if (contains(name, "synchronize")) return "synchronize";
  if (contains(name, "query")) return "query";
  if (contains(name, "eventrecord") || contains(name, "recordevent")) {
    return "event_record";
  }
  if (contains(name, "eventcreate") || contains(name, "createevent") ||
      contains(name, "eventdestroy") || contains(name, "destroyevent")) {
    return "event_lifecycle";
  }
  if (contains(name, "graphlaunch") ||
      contains(name, "aclmdlriexecuteasync")) {
    return "graph_launch";
  }
  if (contains(name, "launch")) return "launch";
  if (contains(name, "memcpy") || contains(name, "memset") ||
      contains(name, "inplacecopy")) {
    return "memory";
  }
  if (contains(name, "capture") || contains(name, "graph")) {
    return "graph_control";
  }
  return "other";
}

void materialize_host_activity_rows(RuntimeDeviceSqlRows& rows,
                                    std::uint64_t max_activity_rows) {
  rows.host_activity_materialization_limit = max_activity_rows;
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

  std::vector<ActivityRange> ranges;
  ranges.reserve(rows.host_intervals.size());
  for (AnchorHostIntervalSqlRow& interval : rows.host_intervals) {
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
    rows.host_activity_candidate_upper_bound = saturating_add(
        rows.host_activity_candidate_upper_bound, hi - lo);
    ranges.push_back(ActivityRange{&interval, &group, lo, hi});
  }

  if (rows.host_activity_candidate_upper_bound > max_activity_rows) {
    rows.host_activity_materialization_state =
        "withheld_candidate_upper_bound_exceeds_limit";
    for (AnchorHostIntervalSqlRow& interval : rows.host_intervals) {
      if (interval.support_state == "supported_ordered") {
        interval.support_state =
            "supported_ordered_activity_withheld_size_limit";
      }
    }
    return;
  }

  rows.host_activities.reserve(
      static_cast<std::size_t>(rows.host_activity_candidate_upper_bound));
  for (const ActivityRange& range : ranges) {
    const std::int64_t host_start = std::stoll(range.interval->host_start_ns);
    const std::int64_t host_end = std::stoll(range.interval->host_end_ns);
    std::uint32_t observed_order = 0;
    struct ApiSummary {
      std::uint64_t call_count = 0;
      std::set<std::string> api_names;
      double scheduled_call_us = 0.0;
      double scheduled_overlap_us = 0.0;
    };
    std::map<std::string, ApiSummary> summaries;
    for (std::size_t index = range.lo; index < range.hi; ++index) {
      const RuntimeCallSqlRow& call = *range.group->calls[index];
      if (call.end_ns <= host_start) continue;
      const std::int64_t overlap_ns =
          std::min(call.end_ns, host_end) - std::max(call.start_ns, host_start);
      rows.host_activities.push_back(AnchorHostActivitySqlRow{
          range.interval->interval_id,
          call.runtime_call_id,
          observed_order++,
      });
      const std::string api_family = public_runtime_api_family(call.api_name);
      if (!api_family.empty()) {
        ApiSummary& summary = summaries[api_family];
        ++summary.call_count;
        summary.api_names.insert(call.api_name);
        summary.scheduled_call_us += call.dur_us;
        summary.scheduled_overlap_us +=
            static_cast<double>(overlap_ns) / 1000.0;
      }
    }
    for (const auto& [api_family, summary] : summaries) {
      rows.host_api_summaries.push_back(AnchorHostApiSummarySqlRow{
          range.interval->interval_id,
          api_family,
          summary.call_count,
          summary.api_names.size(),
          summary.scheduled_call_us,
          summary.scheduled_overlap_us,
      });
    }
  }
}

}  // namespace traceloom::compat::detail
