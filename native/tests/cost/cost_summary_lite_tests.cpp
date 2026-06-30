#include "traceloom/cost/cost_summary_lite.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  AnchorTable anchors;
  anchors.append(SourceRefId(0), TraceEventId(0), ReplayUnitId::invalid(),
                 AnchorKind::kDeviceEvent, SymbolId(1), 0, 0, 100, 140);
  anchors.append(SourceRefId(0), TraceEventId(1), ReplayUnitId(0),
                 AnchorKind::kGraphReplayUnit, SymbolId(2), 0, 0, 140, 220);
  anchors.append(SourceRefId(0), TraceEventId(2), ReplayUnitId::invalid(),
                 AnchorKind::kDeviceEvent, SymbolId(3), 0, 0, 220, 250);

  const CostSummaryLite summary = summarize_anchor_costs(anchors);
  require(summary.anchor_count == 3);
  require(summary.total_duration_ns == 150);
  require(summary.by_kind.size() == 2);
  require(summary.by_kind[0].kind == AnchorKind::kDeviceEvent);
  require(summary.by_kind[0].anchor_count == 2);
  require(summary.by_kind[0].duration_ns == 70);
  require(summary.by_kind[1].kind == AnchorKind::kGraphReplayUnit);
  require(summary.by_kind[1].anchor_count == 1);
  require(summary.by_kind[1].duration_ns == 80);

  bool caught_bad_duration = false;
  try {
    AnchorTable bad;
    bad.append(SourceRefId(0), TraceEventId(0), ReplayUnitId::invalid(),
               AnchorKind::kDeviceEvent, SymbolId(1), 0, 0, 10, 0);
    (void)summarize_anchor_costs(bad);
  } catch (const std::invalid_argument&) {
    caught_bad_duration = true;
  }
  require(caught_bad_duration);

  return 0;
}
