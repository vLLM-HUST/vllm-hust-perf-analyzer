#pragma once
#include <string>
#include <map>
#include <vector>

namespace traceloom::compat {
// Imports a closed or explicitly incomplete runtime context into the temporary
// AugDB before recovery/publication. Does not change events or geometry;
// optional candidate partitioning consumes the resulting identity relation.
void import_scheduler_context(const std::string &sqlite_path,
                              const std::vector<std::string> &context_paths);
void register_scheduler_context_catalog(const std::string& sqlite_path,
                                        const std::vector<std::string>& context_paths);
std::map<std::string, std::string> scheduler_event_partitions(const std::string& sqlite_path);
} // namespace traceloom::compat
