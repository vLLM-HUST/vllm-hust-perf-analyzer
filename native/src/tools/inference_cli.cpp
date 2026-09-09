#include "inference_cli.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "traceloom/inference/trace.h"

namespace traceloom::tools {
namespace {
volatile std::sig_atomic_t stop = 0;
void interrupt(int) { stop = 1; }
void help() {
  std::cout << "Usage: traceloom import-inference EVENTS.ndjson-or-DIRECTORY "
               "[--output "
               "analysis.db]\n"
               "         [--html-out view.html] [--perfetto-out trace.json] "
               "[--follow]\n"
               "         [--include-summaries] [--max-events N]\n"
               "       traceloom export-inference analysis.db [--html-out "
               "view.html]\n"
               "         [--perfetto-out trace.json]\n"
               "Imports complete NDJSON lines transactionally. Summary text is "
               "omitted by default.\n"
               "Follow publishes local snapshots until SIGINT; no network "
               "listener is started.\n";
}
}  // namespace
int run_inference_cli(int argc, char** argv) {
  namespace fs = std::filesystem;
  const bool importing = std::string(argv[1]) == "import-inference";
  std::string input, db, html, perfetto;
  inference::ImportOptions options;
  bool follow = false;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    auto value = [&]() {
      if (++i == argc)
        throw std::invalid_argument("missing inference option value");
      return std::string(argv[i]);
    };
    if (a == "--help" || a == "-h") {
      help();
      return 0;
    } else if (a == "--output" && importing)
      db = value();
    else if (a == "--html-out")
      html = value();
    else if (a == "--perfetto-out")
      perfetto = value();
    else if (a == "--follow" && importing)
      follow = true;
    else if (a == "--include-summaries" && importing)
      options.include_summaries = true;
    else if (a == "--max-events" && importing) {
      const auto v = value();
      std::size_t used = 0;
      options.max_events = std::stoull(v, &used);
      if (used != v.size() || options.max_events == 0 ||
          options.max_events > 100000)
        throw std::invalid_argument("max-events must be in 1..100000");
    } else if (!a.empty() && a[0] != '-' && input.empty())
      input = a;
    else
      throw std::invalid_argument("unsupported inference argument");
  }
  if (input.empty()) {
    help();
    throw std::invalid_argument("inference input is required");
  }
  if (!importing) {
    if (html.empty() && perfetto.empty())
      throw std::invalid_argument("an inference projection output is required");
    inference::export_trace(input, html, perfetto);
    return 0;
  }
  if (db.empty())
    db = ((fs::is_directory(input) ? fs::path(input)
                                   : fs::path(input).parent_path()) /
          "traceloom" / "analysis.db")
             .string();
  const std::vector<std::string> paths = {input, db, html, perfetto};
  for (std::size_t i = 0; i < paths.size(); ++i)
    for (std::size_t j = 0; j < i; ++j)
      if (!paths[i].empty() && !paths[j].empty() &&
          (fs::weakly_canonical(paths[i]) == fs::weakly_canonical(paths[j]) ||
           (fs::exists(paths[i]) && fs::exists(paths[j]) &&
            fs::equivalent(paths[i], paths[j]))))
        throw std::invalid_argument("inference input/output paths collide");
  stop = 0;
  const auto old_int = std::signal(SIGINT, interrupt);
  const auto old_term = std::signal(SIGTERM, interrupt);
  bool imported = false;
  std::map<std::string, std::pair<fs::file_time_type, std::uintmax_t>> observed;
  try {
    do {
      if (!fs::exists(input) && follow) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        continue;
      }
      std::vector<std::string> inputs;
      if (fs::is_directory(input)) {
        std::uintmax_t total = 0;
        for (const auto& entry : fs::directory_iterator(input)) {
          if (entry.is_symlink() || !entry.is_regular_file() ||
              entry.path().extension() != ".ndjson")
            continue;
          const auto size = entry.file_size();
          if (inputs.size() >= 1024 || size > 64 * 1024 * 1024 - total)
            throw std::invalid_argument(
                "inference directory exceeds file/byte budget");
          total += size;
          inputs.push_back(entry.path().string());
        }
        std::sort(inputs.begin(), inputs.end());
      } else
        inputs.push_back(input);
      for (const auto& path : inputs)
        for (const auto& output : {db, html, perfetto})
          if (!output.empty() &&
              (fs::weakly_canonical(path) == fs::weakly_canonical(output) ||
               (fs::exists(output) && fs::equivalent(path, output))))
            throw std::invalid_argument(
                "inference segment/output paths collide");
      bool changed = false;
      for (const auto& path : inputs) {
        const auto stamp =
            std::make_pair(fs::last_write_time(path), fs::file_size(path));
        if (observed.count(path) && observed.at(path) == stamp) continue;
        const auto receipt = inference::import_ndjson(path, db, options);
        std::cerr << "inference snapshot: inserted=" << receipt.inserted
                  << " duplicates=" << receipt.duplicates
                  << " discarded_attributes=" << receipt.discarded_attributes
                  << " pending_tail_bytes=" << receipt.pending_tail_bytes
                  << "\n";
        observed[path] = stamp;
        imported = true;
        changed = true;
      }
      // Bound bookkeeping when a producer rotates old segments away.
      for (auto it = observed.begin(); it != observed.end();) {
        if (std::find(inputs.begin(), inputs.end(), it->first) == inputs.end())
          it = observed.erase(it);
        else
          ++it;
      }
      if (changed && (!html.empty() || !perfetto.empty()))
        inference::export_trace(db, html, perfetto, follow);
      if (inputs.empty() && !follow)
        throw std::invalid_argument("no inference NDJSON segments found");
      if (follow) std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    } while (follow && !stop);
    if (follow && imported) inference::export_trace(db, html, perfetto, false);
  } catch (...) {
    // A failed observer must not keep advertising refresh as an active watch.
    if (follow && imported && (!html.empty() || !perfetto.empty())) {
      try {
        inference::export_trace(db, html, perfetto, false);
      } catch (...) {
      }
    }
    std::signal(SIGINT, old_int);
    std::signal(SIGTERM, old_term);
    throw;
  }
  std::signal(SIGINT, old_int);
  std::signal(SIGTERM, old_term);
  return 0;
}
}  // namespace traceloom::tools
