#pragma once

#include <cstddef>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

struct Partition {
  PartitionId id;
  std::size_t owned_begin = 0;
  std::size_t owned_end = 0;
  std::size_t read_begin = 0;
  std::size_t read_end = 0;
};

struct PartitionPlanConfig {
  std::size_t target_tokens_per_partition = 1024;
  std::size_t halo_tokens = 0;
};

class PartitionPlan {
 public:
  static PartitionPlan build(std::size_t token_count,
                             PartitionPlanConfig config);

  std::size_t size() const noexcept { return partitions_.size(); }
  bool empty() const noexcept { return partitions_.empty(); }
  const Partition& partition_at(std::size_t index) const;
  const std::vector<Partition>& partitions() const noexcept {
    return partitions_;
  }

 private:
  std::vector<Partition> partitions_;
};

}  // namespace traceloom
