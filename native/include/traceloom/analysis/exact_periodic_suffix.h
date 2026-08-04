#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace traceloom {

struct ExactPeriodicSuffixCandidate {
  std::size_t start = 0;
  std::size_t period = 0;
  std::size_t full_repeats = 0;
  std::size_t trailing = 0;

  bool valid() const noexcept { return period != 0; }
};

ExactPeriodicSuffixCandidate find_exact_periodic_suffix(
    const std::vector<std::int64_t>& identities,
    std::size_t minimum_full_repeats = 3);

}  // namespace traceloom
