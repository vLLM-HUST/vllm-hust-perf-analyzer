#include "traceloom/ir/protected_interval_table.h"
#include "traceloom/ir/token_table.h"
#include "traceloom/sequence/boundary_index.h"
#include "traceloom/sequence/partition_plan.h"
#include "traceloom/sequence/protected_sequence.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  TokenTable tokens;
  const TokenId token0 = tokens.append(AnchorId(0), SymbolId(10), 0, 0, 0, 10);
  const TokenId token1 = tokens.append(AnchorId(1), SymbolId(11), 0, 1, 10, 20);
  const TokenId token2 = tokens.append(AnchorId(2), SymbolId(12), 0, 2, 20, 30);
  const TokenId token3 = tokens.append(AnchorId(3), SymbolId(13), 0, 3, 30, 40);
  const TokenId token4 = tokens.append(AnchorId(4), SymbolId(14), 0, 4, 40, 50);

  const ProtectedSequence sequence = ProtectedSequence::from_token_table(tokens);
  require(sequence.size() == 5);
  require(sequence.token_at(2).token_id == token2);
  require(sequence.token_at(2).symbol_id == SymbolId(12));
  require(sequence.token_at(2).sequence_index == 2);
  require(sequence.index_of(token0) == 0);
  require(sequence.index_of(token4) == 4);

  ProtectedIntervalTable intervals;
  const ProtectedIntervalId interval0 = intervals.append(
      ProtectedIntervalKind::kGraphReplayUnit, BoundaryPolicy::kNoCross,
      token1, token3, AnchorId(1), AnchorId(3), SourceRefId(0));

  const BoundaryIndex boundaries = BoundaryIndex::build(sequence, intervals);
  require(boundaries.interval_count() == 1);
  require(boundaries.intervals_covering(0).empty());
  require(boundaries.intervals_covering(2).size() == 1);
  require(boundaries.intervals_covering(2)[0] == interval0);
  require(!boundaries.violates_no_cross_interval(1, 4));
  require(!boundaries.violates_no_cross_interval(2, 3));
  require(boundaries.violates_no_cross_interval(0, 2));
  require(boundaries.violates_no_cross_interval(3, 5));
  require(boundaries.violates_no_cross_interval(0, 5));
  require(boundaries.first_no_cross_violation(0, 5) == interval0);

  const PartitionPlan plan =
      PartitionPlan::build(sequence.size(), PartitionPlanConfig{2, 1});
  require(plan.size() == 3);
  require(plan.partition_at(0).owned_begin == 0);
  require(plan.partition_at(0).owned_end == 2);
  require(plan.partition_at(0).read_begin == 0);
  require(plan.partition_at(0).read_end == 3);
  require(plan.partition_at(1).owned_begin == 2);
  require(plan.partition_at(1).owned_end == 4);
  require(plan.partition_at(1).read_begin == 1);
  require(plan.partition_at(1).read_end == 5);
  require(plan.partition_at(2).owned_begin == 4);
  require(plan.partition_at(2).owned_end == 5);
  require(plan.partition_at(2).read_begin == 3);
  require(plan.partition_at(2).read_end == 5);

  bool caught_bad_sequence = false;
  try {
    (void)sequence.token_at(999);
  } catch (const std::out_of_range&) {
    caught_bad_sequence = true;
  }
  require(caught_bad_sequence);

  bool caught_bad_token_id = false;
  try {
    (void)sequence.index_of(TokenId(999));
  } catch (const std::out_of_range&) {
    caught_bad_token_id = true;
  }
  require(caught_bad_token_id);

  bool caught_bad_partition_config = false;
  try {
    (void)PartitionPlan::build(sequence.size(), PartitionPlanConfig{0, 0});
  } catch (const std::invalid_argument&) {
    caught_bad_partition_config = true;
  }
  require(caught_bad_partition_config);

  bool caught_bad_sequence_index = false;
  try {
    TokenTable bad_tokens;
    bad_tokens.append(AnchorId(0), SymbolId(1), 0, 1, 0, 10);
    (void)ProtectedSequence::from_token_table(bad_tokens);
  } catch (const std::invalid_argument&) {
    caught_bad_sequence_index = true;
  }
  require(caught_bad_sequence_index);

  return 0;
}
