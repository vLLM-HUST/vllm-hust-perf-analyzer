#include "perfetto_export_internal.h"

#include <algorithm>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace traceloom::compat::perfetto_internal {
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
