#pragma once

#include <cstdint>
#include <string>

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

std::int64_t raw_timeline_origin(sqlite3* db, std::int64_t current);
void export_raw_provider_timeline(sqlite3* db, RawTraceWriter& writer,
                                  PerfettoExportReceipt& receipt);

}  // namespace traceloom::compat::perfetto_internal
