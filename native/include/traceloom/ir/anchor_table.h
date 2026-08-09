#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

enum class AnchorKind {
  kUnknown,
  kDeviceEvent,
  kGraphReplayUnit,
  kCommunication,
  kSynchronization,
  kGraphH,
  kGraphL,
  kGraphT,
};

struct AnchorRow {
  AnchorId id;
  SourceRefId source_ref_id;
  TraceEventId trace_event_id;
  ReplayUnitId replay_unit_id;
  // Exact slot/launch member of the replay unit that owns this anchor. Valid
  // only for anchors created for an exact ReplayUnitLaunchMember; anchors for
  // best-effort replay units and ordinary device events keep this invalid.
  // This is the builder-owned identity that maps an exact launch occurrence
  // to its promoted tree anchor without any temporal inference.
  ReplayUnitLaunchMemberId replay_unit_launch_member_id =
      ReplayUnitLaunchMemberId::invalid();
  AnchorKind kind = AnchorKind::kUnknown;
  SymbolId symbol_id;
  std::uint32_t device_id = 0;
  std::uint32_t stream_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

class AnchorTable {
 public:
  AnchorId append(SourceRefId source_ref_id,
                  TraceEventId trace_event_id,
                  ReplayUnitId replay_unit_id,
                  AnchorKind kind,
                  SymbolId symbol_id,
                  std::uint32_t device_id,
                  std::uint32_t stream_id,
                  std::int64_t start_ns,
                  std::int64_t end_ns,
                  ReplayUnitLaunchMemberId replay_unit_launch_member_id =
                      ReplayUnitLaunchMemberId::invalid());

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const AnchorRow& row(AnchorId id) const;
  const std::vector<AnchorRow>& rows() const noexcept { return rows_; }

 private:
  std::vector<AnchorRow> rows_;
};

}  // namespace traceloom
