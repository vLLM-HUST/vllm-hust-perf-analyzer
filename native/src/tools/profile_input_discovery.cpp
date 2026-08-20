#include "profile_input_discovery.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include "traceloom/adapters/ascend_sqlite_adapter.h"
#include "traceloom/adapters/cuda_nsys_sqlite_adapter.h"
#include "traceloom/adapters/hygon_sqlite_adapter.h"

namespace traceloom::tools {
namespace {

namespace fs = std::filesystem;

bool looks_like_msprof_db(const fs::path& path) {
  if (!fs::is_regular_file(path)) {
    return false;
  }
  const std::string name = path.filename().string();
  return name.rfind("msprof_", 0) == 0 && path.extension() == ".db";
}

bool has_sqlite_profile_extension(const fs::path& path) {
  const std::string extension = path.extension().string();
  return extension == ".db" || extension == ".sqlite" ||
         extension == ".sqlite3";
}

bool looks_like_supported_profile_db(const fs::path& path,
                                     const std::string& source_kind) {
  if (!fs::is_regular_file(path) || !has_sqlite_profile_extension(path)) {
    return false;
  }
  if (source_kind == "cuda_nsys_sqlite") {
    return looks_like_cuda_nsys_sqlite_profile(path.string());
  }
  if (source_kind == "hygon_sqlite") {
    return looks_like_hygon_sqlite_profile(path.string());
  }
  if (source_kind == "ascend_sqlite_hot_path") {
    return ascend_sqlite_has_usable_task_table(path.string());
  }
  if (source_kind == "ascend_sqlite_split") {
    return false;
  }
  if (looks_like_cuda_nsys_sqlite_profile(path.string())) {
    return true;
  }
  if (ascend_sqlite_has_usable_task_table(path.string())) {
    return true;
  }
  return looks_like_hygon_sqlite_profile(path.string());
}

}  // namespace

std::vector<std::string> discover_profile_dbs(
    const std::string& input,
    const std::string& source_kind) {
  const fs::path root(input);
  std::vector<fs::path> dbs;
  std::vector<fs::path> split_profiles;
  std::error_code ec;
  const bool allow_split_profiles =
      source_kind == "auto" || source_kind == "ascend_sqlite_split";
  if (looks_like_supported_profile_db(root, source_kind)) {
    dbs.push_back(root);
  } else if (allow_split_profiles && looks_like_msprof_db(root)) {
    const fs::path profile_dir = root.parent_path();
    if (looks_like_ascend_split_sqlite_profile(profile_dir.string())) {
      split_profiles.push_back(profile_dir);
    }
  } else if (fs::is_directory(root, ec)) {
    if (allow_split_profiles &&
        looks_like_ascend_split_sqlite_profile(root.string())) {
      split_profiles.push_back(root);
    }
    for (fs::recursive_directory_iterator iterator(root), end;
         iterator != end; ++iterator) {
      const auto& entry = *iterator;
      if (entry.is_directory() && entry.path().filename() == "traceloom") {
        iterator.disable_recursion_pending();
        continue;
      }
      if (looks_like_supported_profile_db(entry.path(), source_kind)) {
        dbs.push_back(entry.path());
      }
      if (allow_split_profiles && entry.is_directory() &&
          fs::is_directory(entry.path() / "host" / "sqlite", ec) &&
          looks_like_ascend_split_sqlite_profile(entry.path().string())) {
        split_profiles.push_back(entry.path());
      }
    }
  } else if (!fs::exists(root, ec)) {
    throw std::invalid_argument("input path does not exist: " + input);
  } else {
    throw std::invalid_argument(
        "input is not a supported Ascend/Hygon/CUDA profile DB or directory: " +
        input);
  }
  std::sort(dbs.begin(), dbs.end());
  std::sort(split_profiles.begin(), split_profiles.end());
  split_profiles.erase(
      std::unique(split_profiles.begin(), split_profiles.end()),
      split_profiles.end());
  for (const fs::path& profile : split_profiles) {
    const bool has_usable_monolithic =
        std::any_of(dbs.begin(), dbs.end(), [&](const fs::path& db) {
          return db.parent_path() == profile;
        });
    if (!has_usable_monolithic) {
      dbs.push_back(profile);
    }
  }
  std::sort(dbs.begin(), dbs.end());
  std::vector<std::string> result;
  result.reserve(dbs.size());
  for (const auto& db : dbs) {
    result.push_back(db.string());
  }
  return result;
}

std::string input_format_for(const std::string& source_db,
                             const std::string& source_kind) {
  const fs::path source(source_db);
  const std::string filename = source.filename().string();
  if (source_kind == "ascend_sqlite_hot_path" &&
      filename.rfind("ascend_pytorch_profiler_", 0) == 0) {
    return "torch_npu_profiler";
  }
  if (source_kind == "ascend_sqlite_split") {
    return "cann_split_profile";
  }
  if (source_kind == "ascend_sqlite_hot_path") {
    return filename.rfind("msprof_", 0) == 0 ? "cann_msprof"
                                              : "ascend_profiler_sqlite";
  }
  if (source_kind == "cuda_nsys_sqlite") {
    return "cuda_nsys_sqlite";
  }
  if (source_kind == "hygon_sqlite") {
    return "hygon_sqlite";
  }
  return "unknown";
}

}  // namespace traceloom::tools
