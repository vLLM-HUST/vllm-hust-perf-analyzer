#include "perfetto_cli_args.h"

#include <limits>
#include <iostream>
#include <stdexcept>
#include <string>

namespace traceloom::tools {

void print_perfetto_reading_notes(const compat::PerfettoExportReceipt& receipt) {
  std::cerr << "  reading: views overlap; do not sum structure, device_events and raw_provider.\n"
               "  query: filter args.projection_plane='device_events' and one rank/device/view; sums are not wall time.\n";
  if (!receipt.distributed_alignment_boundary.empty())
    std::cerr << "  clock boundary: " << receipt.distributed_alignment_boundary << "\n";
}

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
