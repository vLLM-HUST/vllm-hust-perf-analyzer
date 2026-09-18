#pragma once

#include <string>

#include "traceloom/compat/perfetto_exporter.h"

namespace traceloom::tools {

void print_perfetto_reading_notes(const compat::PerfettoExportReceipt& receipt);

compat::PerfettoDistributedRankInput parse_distributed_rank_input(const std::string& value);

}  // namespace traceloom::tools
