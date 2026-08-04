#include "traceloom/analysis/exact_periodic_suffix.h"

#include <algorithm>

namespace traceloom {

ExactPeriodicSuffixCandidate find_exact_periodic_suffix(
    const std::vector<std::int64_t>& identities,
    std::size_t minimum_full_repeats) {
  ExactPeriodicSuffixCandidate best;
  if (minimum_full_repeats < 2) {
    return best;
  }
  for (std::size_t period = 1;
       period <= identities.size() / minimum_full_repeats; ++period) {
    std::size_t start = 0;
    for (std::size_t index = period; index < identities.size(); ++index) {
      if (identities[index] != identities[index - period]) {
        start = std::max(start, index - period + 1);
      }
    }
    const std::size_t covered = identities.size() - start;
    const std::size_t full_repeats = covered / period;
    if (full_repeats < minimum_full_repeats) {
      continue;
    }
    const bool better =
        !best.valid() || covered > identities.size() - best.start ||
        (covered == identities.size() - best.start &&
         full_repeats > best.full_repeats) ||
        (covered == identities.size() - best.start &&
         full_repeats == best.full_repeats && period < best.period);
    if (better) {
      best = ExactPeriodicSuffixCandidate{
          start, period, full_repeats, covered % period};
    }
  }
  return best;
}

}  // namespace traceloom
