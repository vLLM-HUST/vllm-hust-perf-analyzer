#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace traceloom::inference {

struct ImportOptions {
  bool include_summaries = false;
  std::size_t max_events = 100000;
};
struct ImportReceipt {
  std::size_t inserted = 0;
  std::size_t duplicates = 0;
  std::size_t discarded_attributes = 0;
  std::size_t pending_tail_bytes = 0;
};

// Imports a bounded snapshot transactionally. Only newline-terminated records
// are admitted. Repeated imports are idempotent; conflicting IDs roll back.
ImportReceipt import_ndjson(const std::string& input,
                            const std::string& database,
                            const ImportOptions& options = {});

// Read one consistent DB snapshot. Both projections retain observed states;
// neither synthesizes missing ends nor a global clock/critical path.
void export_trace(const std::string& database, const std::string& html_output,
                  const std::string& perfetto_output, bool live = false);
bool is_inference_database(const std::string& path);

}  // namespace traceloom::inference
