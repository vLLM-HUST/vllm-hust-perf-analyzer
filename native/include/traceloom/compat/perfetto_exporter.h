#pragma once

#include <cstdint>
#include <string>

namespace traceloom::compat {

struct PerfettoExportOptions {
  bool include_raw_provider_timeline = true;
};

struct PerfettoExportReceipt {
  std::uint64_t repeat_body_slices = 0;
  std::uint64_t structural_slices = 0;
  std::uint64_t atomic_slices = 0;
  std::uint64_t raw_slices = 0;
  std::uint64_t counter_samples = 0;
  std::uint64_t motif_classes = 0;
};

bool is_queryable_database_timeline(const std::string& analysis_db_path);

PerfettoExportReceipt write_perfetto_trace(const std::string& analysis_db_path,
                                           const std::string& output_path,
                                           const PerfettoExportOptions& options = {});

}  // namespace traceloom::compat
