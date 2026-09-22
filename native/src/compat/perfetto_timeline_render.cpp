#include "perfetto_export_internal.h"

#include "traceloom/analysis/structural_symbol_normalization.h"

#include <algorithm>
#include <map>
#include <iomanip>
#include <sstream>
#include <tuple>
#include <utility>
#include <vector>

namespace traceloom::compat::perfetto_internal {
std::string json_quote(const std::string& value) {
  std::ostringstream out;
  out << '"';
  for (unsigned char ch : value) {
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
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch)
              << std::dec;
        } else {
          out << static_cast<char>(ch);
        }
    }
  }
  return out.str() + '"';
}

std::optional<std::string> device_event_display_name(
    const std::string& provider, const std::string& name) {
  if (provider != "ascend") return name;
  static const auto rules = load_default_structural_symbol_ruleset();
  const std::string normalized = normalize_selected_task_structural_symbol(
      provider, name, rules);
  if (normalized == "KERNEL_AICPU" || normalized == "AivKernel" ||
      (normalized.size() >= 3 &&
       normalized.compare(normalized.size() - 3, 3, "SQE") == 0)) {
    return std::nullopt;
  }
  return normalized;
}

void apply_device_event_display_policy(std::vector<TimelineSlice>& slices,
                                       const std::string& provider) {
  for (auto& slice : slices) {
    if (!slice.event) continue;
    const auto name = device_event_display_name(provider, slice.name);
    if (name.has_value()) slice.name = *name;
  }
  slices.erase(std::remove_if(slices.begin(), slices.end(), [&](const auto& slice) {
    return slice.event &&
           !device_event_display_name(provider, slice.name).has_value();
  }), slices.end());
}

void write_common_timeline(RawTraceWriter& writer, std::vector<TimelineSlice> slices,
                           PerfettoExportReceipt& receipt) {
  // Stream and replay identities are attributes, not display partitions.
  // Only actual overlap needs another event lane within a device/view.
  using Group = std::tuple<int, int, std::string, bool, int>;
  std::map<Group, std::vector<TimelineSlice>> groups;
  for (auto& slice : slices) {
    const Group key{slice.db, slice.device, slice.view, slice.event,
                    slice.event ? 0 : slice.depth};
    groups[key].push_back(std::move(slice));
  }
  int next_structure_tid = 1, next_event_tid = 900000;
  for (auto& [key, values] : groups) {
    std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) {
      return std::tie(a.start, a.end, a.name, a.args) <
             std::tie(b.start, b.end, b.name, b.args);
    });
    std::vector<std::pair<std::int64_t, int>> lanes;
    for (const auto& slice : values) {
      auto lane = std::find_if(lanes.begin(), lanes.end(), [&](const auto& entry) {
        return entry.first <= slice.start;
      });
      if (lane == lanes.end()) {
        const int tid = slice.event ? next_event_tid++ : next_structure_tid++;
        const std::string name =
            std::string(slice.event ? "timeline events" : "subtree") +
            " · db " + std::to_string(slice.db) + " · device " +
            std::to_string(slice.device) + " · " + slice.view +
            (slice.event ? std::string{} : " · depth " + std::to_string(slice.depth)) +
            " · lane " + std::to_string(lanes.size());
        writer.thread(110, tid, name, tid);
        lanes.push_back({slice.end, tid});
        lane = lanes.end() - 1;
      } else {
        lane->first = slice.end;
      }
      auto args=slice.args;
      args.pop_back();
      // Ordinary and replay-derived slices share the same query selectors.
      // Keep legacy aliases, but never append duplicate JSON keys.
      if (slice.replay) {
        args += ",\"database_index\":" + std::to_string(slice.db);
        args += ",\"view_name\":" + json_quote(slice.view);
      } else {
        args += ",\"db_idx\":" + std::to_string(slice.db);
      }
      args+=",\"projection_plane\":\""+std::string(slice.event ? "device_events" : "structure")+"\",";
      args+="\"display_depth\":"+(slice.event ? std::string("null") : std::to_string(slice.depth));
      args+=",\"replay_derived\":"+std::string(slice.replay ? "true" : "false")+"}";
      writer.slice(110, lane->second, slice.name, slice.start, slice.end,
                   slice.category, args);
      if (slice.event) ++receipt.atomic_slices;
      else if (slice.repeat_body) ++receipt.repeat_body_slices;
      else ++receipt.structural_slices;
      if (slice.replay) {
        if (slice.event) ++receipt.replay_member_slices;
        else ++receipt.replay_structure_slices;
      }
    }
  }
}
}  // namespace traceloom::compat::perfetto_internal
