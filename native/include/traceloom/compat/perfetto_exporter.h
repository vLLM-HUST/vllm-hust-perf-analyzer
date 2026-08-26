#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace traceloom::compat {

struct PerfettoDistributedRankInput {
  int rank = 0;
  std::string timeline_db_path;
};

struct PerfettoExportOptions {
  bool include_raw_provider_timeline = true;
  std::vector<PerfettoDistributedRankInput> distributed_ranks;
  int distributed_reference_rank = 0;
};

struct PerfettoExportReceipt {
  std::uint64_t repeat_body_slices = 0;
  std::uint64_t structural_slices = 0;
  std::uint64_t atomic_slices = 0;
  std::uint64_t raw_slices = 0;
  std::uint64_t distributed_timeline_slices = 0;
  std::uint64_t distributed_rank_tracks = 0;
  std::uint64_t counter_samples = 0;
  std::uint64_t motif_classes = 0;
};

bool is_queryable_database_timeline(const std::string& analysis_db_path);

PerfettoExportReceipt write_perfetto_trace(const std::string& analysis_db_path,
                                           const std::string& output_path,
                                           const PerfettoExportOptions& options = {});

}  // namespace traceloom::compat
