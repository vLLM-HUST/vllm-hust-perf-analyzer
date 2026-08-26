#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/compat/perfetto_exporter.h"

struct sqlite3;

namespace traceloom::compat::perfetto_internal {

class RawTraceWriter {
 public:
  virtual ~RawTraceWriter() = default;

  virtual void process(int pid, const std::string& name, int order) = 0;
  virtual void thread(int pid, int tid, const std::string& name, int order) = 0;
  virtual void slice(int pid, int tid, const std::string& name, std::int64_t start,
                     std::int64_t end, const std::string& category, const std::string& args) = 0;
  virtual void counter(int pid, int tid, const std::string& name, std::int64_t timestamp,
                       const std::string& key, double value) = 0;
};

struct DistributedTimelineEventSlice {
  std::string node_id;
  std::string local_node_id;
  std::string view_name;
  std::string rooted_role_path;
  std::string repeat_context;
  std::string label;
  std::string category;
  int database_index = 0;
  int device_id = 0;
  int tree_depth = 0;
  std::int64_t occurrence_index = 0;
  std::int64_t start = 0;
  std::int64_t end = 0;
  std::int64_t anchor_start = 0;
  std::int64_t anchor_end = 0;
  std::int64_t anchor_count = 0;
  double compute_us = 0;
  double comm_us = 0;
  double idle_us = 0;
  double total_us = 0;
  double self_us = 0;
  std::int64_t aux_events = 0;
  double aux_us = 0;
};

struct DistributedRankTimeline {
  int rank = 0;
  std::string timeline_db_path;
  std::string timeline_db_sha256;
  std::int64_t source_anchor = 0;
  std::vector<DistributedTimelineEventSlice> events;
};

struct DistributedFlatTimeline {
  int reference_rank = 0;
  std::int64_t display_reference_anchor = 0;
  std::vector<DistributedRankTimeline> ranks;
};

std::int64_t raw_timeline_origin(sqlite3* db, std::int64_t current);
void export_raw_provider_timeline(sqlite3* db, RawTraceWriter& writer,
                                  PerfettoExportReceipt& receipt);
DistributedFlatTimeline load_distributed_flat_timeline(
    const PerfettoExportOptions& options);
void export_distributed_flat_timeline(const DistributedFlatTimeline& timeline,
                                      RawTraceWriter& writer,
                                      PerfettoExportReceipt& receipt);

}  // namespace traceloom::compat::perfetto_internal
