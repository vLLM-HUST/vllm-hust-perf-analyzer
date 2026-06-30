#include "traceloom/sequence/partition_plan.h"

#include <algorithm>
#include <stdexcept>

namespace traceloom {

PartitionPlan PartitionPlan::build(std::size_t token_count,
                                   PartitionPlanConfig config) {
  if (config.target_tokens_per_partition == 0) {
    throw std::invalid_argument("target_tokens_per_partition must be nonzero");
  }

  PartitionPlan plan;
  for (std::size_t begin = 0; begin < token_count;
       begin += config.target_tokens_per_partition) {
    const std::size_t end =
        std::min(token_count, begin + config.target_tokens_per_partition);
    const std::size_t read_begin =
        begin > config.halo_tokens ? begin - config.halo_tokens : 0;
    const std::size_t read_end =
        std::min(token_count, end + config.halo_tokens);
    const auto id = checked_next_id<PartitionId>(plan.partitions_.size());
    plan.partitions_.push_back(Partition{id, begin, end, read_begin, read_end});
  }
  return plan;
}

const Partition& PartitionPlan::partition_at(std::size_t index) const {
  if (index >= partitions_.size()) {
    throw std::out_of_range("Partition index is out of range");
  }
  return partitions_[index];
}

}  // namespace traceloom
