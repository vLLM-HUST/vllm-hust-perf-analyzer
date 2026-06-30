#include "traceloom/adapters/fixture_adapter.h"
#include "traceloom/pattern/candidate_reduce.h"
#include "traceloom/pattern/candidate_scan.h"
#include "traceloom/sequence/boundary_index.h"
#include "traceloom/sequence/partition_plan.h"
#include "traceloom/sequence/protected_sequence.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>
#include <vector>

namespace {

bool has_key_count(const std::vector<traceloom::CandidateSummaryRow>& summary,
                   std::vector<traceloom::SymbolId> symbols,
                   std::size_t count) {
  for (const traceloom::CandidateSummaryRow& row : summary) {
    if (row.key.symbols == symbols && row.occurrence_count == count) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  FixtureInput input;
  input.tokens = {
      FixtureToken{"A", AnchorKind::kDeviceEvent, 0, 0, 0, 10},
      FixtureToken{"B", AnchorKind::kDeviceEvent, 0, 0, 10, 20},
      FixtureToken{"C", AnchorKind::kGraphReplayUnit, 0, 0, 20, 30},
      FixtureToken{"A", AnchorKind::kDeviceEvent, 0, 0, 30, 40},
      FixtureToken{"B", AnchorKind::kDeviceEvent, 0, 0, 40, 50},
  };
  input.protected_intervals = {
      FixtureProtectedInterval{ProtectedIntervalKind::kGraphReplayUnit,
                               BoundaryPolicy::kNoCross, 1, 3},
  };

  const FixtureAdapter adapter(input);
  const NativeIr ir = adapter.load();

  require(ir.source_refs.size() == 1);
  require(ir.trace_events.size() == 5);
  require(ir.anchors.size() == 5);
  require(ir.tokens.size() == 5);
  require(ir.protected_intervals.size() == 1);
  require(ir.symbols.size() == 3);

  const ProtectedSequence sequence =
      ProtectedSequence::from_token_table(ir.tokens);
  const BoundaryIndex boundaries =
      BoundaryIndex::build(sequence, ir.protected_intervals);
  const PartitionPlan plan =
      PartitionPlan::build(sequence.size(), PartitionPlanConfig{2, 2});

  const std::vector<CandidateSummaryRow> summary =
      reduce_candidates(scan_candidate_partitions(
          sequence, boundaries, plan, CandidateScanConfig{2, 3}, 4));

  require(!has_key_count(summary, {SymbolId(0), SymbolId(1)}, 2));
  require(has_key_count(summary, {SymbolId(1), SymbolId(2)}, 1));
  require(has_key_count(summary, {SymbolId(2), SymbolId(0)}, 1));

  for (const CandidateSummaryRow& row : summary) {
    require(row.key.symbols != std::vector<SymbolId>({SymbolId(0), SymbolId(1)}));
    require(row.key.symbols != std::vector<SymbolId>({SymbolId(0), SymbolId(1),
                                                     SymbolId(2)}));
    require(row.key.symbols != std::vector<SymbolId>({SymbolId(2), SymbolId(0),
                                                     SymbolId(1)}));
  }

  bool caught_bad_interval = false;
  try {
    FixtureInput bad;
    bad.tokens = {FixtureToken{"A"}};
    bad.protected_intervals = {
        FixtureProtectedInterval{ProtectedIntervalKind::kGraphReplayUnit,
                                 BoundaryPolicy::kNoCross, 0, 3},
    };
    (void)FixtureAdapter(bad).load();
  } catch (const std::out_of_range&) {
    caught_bad_interval = true;
  }
  require(caught_bad_interval);

  return 0;
}
