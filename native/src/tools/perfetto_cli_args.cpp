#include "perfetto_cli_args.h"

#include <limits>
#include <stdexcept>
#include <string>

namespace traceloom::tools {

compat::PerfettoDistributedRankInput parse_distributed_rank_input(const std::string& value) {
  const auto separator = value.find('=');
  if (separator == std::string::npos || separator == 0 || separator + 1 == value.size())
    throw std::invalid_argument("--distributed-rank expects RANK=TIMELINE.db: " + value);
  std::size_t consumed = 0;
  const unsigned long long parsed = std::stoull(value.substr(0, separator), &consumed, 10);
  if (consumed != separator ||
      parsed > static_cast<unsigned long long>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("invalid rank in --distributed-rank: " + value);
  return {static_cast<int>(parsed), value.substr(separator + 1)};
}

}  // namespace traceloom::tools
