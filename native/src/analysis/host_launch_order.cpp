#include "traceloom/analysis/host_launch_order.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace traceloom {
void apply_host_launch_order(NativeIr& ir) {
  // Replay token bounds have already been committed in observed order.
  if (!ir.protected_intervals.empty() || !ir.replay_units.empty()) return;
  using Key = std::pair<std::string, std::int64_t>;
  std::map<Key, std::vector<const RuntimeCallRow*>> calls;
  for (const auto& call : ir.runtime_calls.rows()) {
    const auto& source = ir.source_refs.row(call.source_ref_id);
    if (call.provider != RuntimeCallProvider::kAscend ||
        call.clock_domain != RuntimeCallClockDomain::kProfilerHost ||
        call.match_policy != RuntimeCallMatchPolicy::kAscendConnectionId ||
        source.table_name != "CANN_API" || call.raw_correlation_id < 0 ||
        !call.api_name_symbol_id.valid() ||
        ir.symbols.value(call.api_name_symbol_id) != "launch") continue;
    calls[{source.source_path, call.raw_correlation_id}].push_back(&call);
  }
  std::map<TraceEventId, std::int64_t> correlations;
  for (const auto& task : ir.tasks.rows())
    correlations[task.trace_event_id] = task.raw_connection_id;
  for (const auto& comm : ir.communication_ops.rows())
    correlations[comm.trace_event_id] = comm.raw_connection_id;
  std::vector<const RuntimeCallRow*> evidence(ir.tokens.size(), nullptr);
  std::map<RuntimeCallId, std::size_t> uses;
  for (const auto& token : ir.tokens.rows()) {
    const auto& anchor = ir.anchors.row(token.anchor_id);
    const auto found = correlations.find(anchor.trace_event_id);
    if (found == correlations.end() || found->second < 0) continue;
    const auto& source = ir.source_refs.row(anchor.source_ref_id);
    const auto hit = calls.find({source.source_path, found->second});
    if (hit == calls.end() || hit->second.size() != 1) continue;
    const auto* call = hit->second.front();
    ++uses[call->id];
    // Monolithic profiler clocks must agree. Reused captures are rejected below.
    if (call->start_ns > token.start_ns || call->end_ns < call->start_ns ||
        (call->raw_global_tid < 0 && call->raw_thread_id < 0) ||
        (call->has_device_id && call->device_id != token.device_id)) continue;
    evidence[token.id.value()] = call;
  }
  for (auto& call : evidence)
    if (call != nullptr && uses[call->id] != 1) call = nullptr;

  TokenTable ordered;
  const auto& tokens = ir.tokens.rows();
  auto emit = [&](std::size_t index, bool accepted) {
    const auto& t = tokens[index];
    ordered.append(t.anchor_id, t.symbol_id, t.device_id,
                   static_cast<std::uint32_t>(ordered.size()), t.start_ns, t.end_ns,
                   accepted ? evidence[index]->id : RuntimeCallId::invalid());
  };
  std::size_t begin = 0;
  while (begin < tokens.size()) {
    if (evidence[begin] == nullptr) { emit(begin++, false); continue; }
    std::size_t end = begin + 1;
    const auto& first = *evidence[begin];
    const auto& source = ir.source_refs.row(first.source_ref_id);
    while (end < tokens.size() && evidence[end] != nullptr &&
           tokens[end].device_id == tokens[begin].device_id &&
           ir.source_refs.row(evidence[end]->source_ref_id).source_path == source.source_path)
      ++end;
    bool safe = true;
    std::map<std::uint32_t, std::int64_t> lane_times;
    std::map<std::int64_t, bool> timestamps;
    std::vector<std::size_t> order;
    for (std::size_t i = begin; i < end; ++i) {
      const auto& call = *evidence[i];
      const auto stream = ir.anchors.row(tokens[i].anchor_id).stream_id;
      const auto prev = lane_times.find(stream);
      if (call.raw_global_tid != first.raw_global_tid ||
          call.raw_thread_id != first.raw_thread_id ||
          call.raw_process_id != first.raw_process_id ||
          !timestamps.emplace(call.start_ns, true).second ||
          (prev != lane_times.end() && prev->second > call.start_ns)) safe = false;
      lane_times[stream] = call.start_ns;
      order.push_back(i);
    }
    if (safe) std::sort(order.begin(), order.end(), [&](auto a, auto b) {
      return evidence[a]->start_ns < evidence[b]->start_ns;
    });
    for (auto i : order) emit(i, safe);
    begin = end;
  }
  ir.tokens = std::move(ordered);
}
}  // namespace traceloom
