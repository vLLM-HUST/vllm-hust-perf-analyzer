#include "traceloom/compat/perfetto_exporter.h"

#include "perfetto_export_internal.h"

#include <sqlite3.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "traceloom/core/sha256.h"

namespace traceloom::compat {
namespace {

struct DbCloser {
  void operator()(sqlite3* db) const { sqlite3_close(db); }
};
using Db = std::unique_ptr<sqlite3, DbCloser>;

struct StatementCloser {
  void operator()(sqlite3_stmt* stmt) const { sqlite3_finalize(stmt); }
};
using Statement = std::unique_ptr<sqlite3_stmt, StatementCloser>;

Db open_db(const std::string& path) {
  sqlite3* raw = nullptr;
  if (sqlite3_open_v2(path.c_str(), &raw, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    const std::string error = raw ? sqlite3_errmsg(raw) : "open failed";
    if (raw) sqlite3_close(raw);
    throw std::runtime_error("failed to open queryable database timeline: " + error);
  }
  return Db(raw);
}

Statement prepare(sqlite3* db, const std::string& sql) {
  sqlite3_stmt* raw = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Perfetto export query failed: " + std::string(sqlite3_errmsg(db)) +
                             "\n" + sql);
  }
  return Statement(raw);
}

std::string text(sqlite3_stmt* stmt, int column) {
  const auto* value = sqlite3_column_text(stmt, column);
  return value ? reinterpret_cast<const char*>(value) : "";
}

bool has_object(sqlite3* db, const std::string& name) {
  auto stmt = prepare(db, "SELECT 1 FROM sqlite_master WHERE name=? LIMIT 1");
  sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);
  return sqlite3_step(stmt.get()) == SQLITE_ROW;
}

using perfetto_internal::json_quote;

std::string number(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(3) << value;
  return out.str();
}

class Sink {
 public:
  explicit Sink(const std::string& path) : gzip_(ends_with(path, ".gz")) {
    const std::filesystem::path output(path);
    if (output.has_parent_path()) {
      std::filesystem::create_directories(output.parent_path());
    }
    if (gzip_) {
#if defined(_WIN32)
      throw std::runtime_error(
          "gzip Perfetto output is unavailable on this platform; use a .json output path");
#else
      open_gzip(path);
#endif
    } else {
      plain_.open(path, std::ios::binary);
      if (!plain_) {
        throw std::runtime_error("failed to open output: " + path);
      }
    }
  }

  ~Sink() {
#if !defined(_WIN32)
    if (gzip_stream_) {
      std::fclose(gzip_stream_);
    }
    if (gzip_pid_ > 0) {
      int status = 0;
      while (waitpid(gzip_pid_, &status, 0) < 0 && errno == EINTR) {
      }
    }
#endif
  }

  void write(const std::string& value) {
    if (gzip_) {
#if !defined(_WIN32)
      if (std::fwrite(value.data(), 1, value.size(), gzip_stream_) != value.size()) {
        throw std::runtime_error("failed to write gzip Perfetto output");
      }
#endif
    } else {
      plain_ << value;
      if (!plain_) {
        throw std::runtime_error("failed to write Perfetto output");
      }
    }
  }

  void close() {
    if (gzip_) {
#if !defined(_WIN32)
      if (std::fclose(gzip_stream_) != 0) {
        gzip_stream_ = nullptr;
        throw std::runtime_error("failed to close gzip input stream");
      }
      gzip_stream_ = nullptr;

      int status = 0;
      while (waitpid(gzip_pid_, &status, 0) < 0) {
        if (errno != EINTR) {
          gzip_pid_ = -1;
          throw std::runtime_error("failed to wait for gzip process");
        }
      }
      gzip_pid_ = -1;
      if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error("gzip failed while writing Perfetto output");
      }
#endif
    } else {
      plain_.close();
      if (!plain_) {
        throw std::runtime_error("failed to close Perfetto output");
      }
    }
  }

 private:
#if !defined(_WIN32)
  void open_gzip(const std::string& path) {
    const int output_fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output_fd < 0) {
      throw std::runtime_error("failed to open gzip output: " + path + ": " + std::strerror(errno));
    }

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
      ::close(output_fd);
      throw std::runtime_error("failed to create gzip pipe");
    }

    gzip_pid_ = fork();
    if (gzip_pid_ == 0) {
      ::close(pipe_fds[1]);
      if (dup2(pipe_fds[0], STDIN_FILENO) < 0 || dup2(output_fd, STDOUT_FILENO) < 0) {
        _exit(126);
      }
      ::close(pipe_fds[0]);
      ::close(output_fd);
      execlp("gzip", "gzip", "-c", static_cast<char*>(nullptr));
      _exit(127);
    }

    ::close(pipe_fds[0]);
    ::close(output_fd);
    if (gzip_pid_ < 0) {
      ::close(pipe_fds[1]);
      throw std::runtime_error("failed to start gzip process");
    }

    gzip_stream_ = fdopen(pipe_fds[1], "wb");
    if (!gzip_stream_) {
      ::close(pipe_fds[1]);
      int status = 0;
      while (waitpid(gzip_pid_, &status, 0) < 0 && errno == EINTR) {
      }
      gzip_pid_ = -1;
      throw std::runtime_error("failed to open gzip input stream");
    }
  }
#endif

  static bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
  }

  bool gzip_ = false;
#if !defined(_WIN32)
  std::FILE* gzip_stream_ = nullptr;
  pid_t gzip_pid_ = -1;
#endif
  std::ofstream plain_;
};

class TraceWriter : public perfetto_internal::RawTraceWriter {
 public:
  TraceWriter(const std::string& path, std::int64_t origin) : sink_(path), origin_(origin) {
    sink_.write("{\"traceEvents\":[");
  }
  void event(const std::string& json) {
    if (!first_) sink_.write(",");
    first_ = false;
    sink_.write(json);
  }
  void process(int pid, const std::string& name, int order) override {
    event("{\"ph\":\"M\",\"pid\":" + std::to_string(pid) +
          ",\"tid\":0,\"name\":\"process_name\",\"args\":{\"name\":" + json_quote(name) + "}}");
    event("{\"ph\":\"M\",\"pid\":" + std::to_string(pid) +
          ",\"tid\":0,\"name\":\"process_sort_index\",\"args\":{\"sort_index\":" +
          std::to_string(order) + "}}");
  }
  void thread(int pid, int tid, const std::string& name, int order) override {
    event("{\"ph\":\"M\",\"pid\":" + std::to_string(pid) + ",\"tid\":" + std::to_string(tid) +
          ",\"name\":\"thread_name\",\"args\":{\"name\":" + json_quote(name) + "}}");
    event("{\"ph\":\"M\",\"pid\":" + std::to_string(pid) + ",\"tid\":" + std::to_string(tid) +
          ",\"name\":\"thread_sort_index\",\"args\":{\"sort_index\":" + std::to_string(order) +
          "}}");
  }
  void slice(int pid, int tid, const std::string& name, std::int64_t start, std::int64_t end,
             const std::string& category, const std::string& args) override {
    event("{\"ph\":\"X\",\"pid\":" + std::to_string(pid) + ",\"tid\":" + std::to_string(tid) +
          ",\"name\":" + json_quote(name) + ",\"cat\":" + json_quote(category) +
          ",\"ts\":" + number((start - origin_) / 1000.0) + ",\"dur\":" +
          number(std::max<std::int64_t>(0, end - start) / 1000.0) + ",\"args\":" + args + "}");
  }
  void counter(int pid, int tid, const std::string& name, std::int64_t ts, const std::string& key,
               double value) override {
    event("{\"ph\":\"C\",\"pid\":" + std::to_string(pid) + ",\"tid\":" + std::to_string(tid) +
          ",\"name\":" + json_quote(name) + ",\"ts\":" + number((ts - origin_) / 1000.0) +
          ",\"args\":{" + json_quote(key) + ":" + number(value) + "}}");
  }
  void finish(const std::string& metadata) {
    sink_.write("],\"displayTimeUnit\":\"ns\",\"metadata\":" + metadata + "}");
    sink_.close();
  }

 private:
  Sink sink_;
  std::int64_t origin_;
  bool first_ = true;
};

struct Node {
  std::string key, id, local, parent_key, parent_id, view, path, kind, label, category;
  int db = 0, device = 0, depth = 0, order = 0, repeat = 0;
  std::vector<std::string> children;
  std::string topology, topology_sha, motif;
};

std::string node_key(int db, int device, const std::string& view, const std::string& node_id) {
  return std::to_string(db) + "\x1f" + std::to_string(device) + "\x1f" + view + "\x1f" + node_id;
}

struct Occurrence {
  std::string node, context;
  int index = 0;
  int parent_index = -1, member_order = 0;
  std::int64_t start = 0, end = 0;
  std::int64_t anchor_start = 0, anchor_end = 0, anchor_count = 0;
  double compute = 0, comm = 0, idle = 0, total = 0, self = 0, aux_us = 0;
  std::int64_t aux_events = 0;
};

struct Interval {
  const Node* node = nullptr;
  Occurrence occurrence;
  bool repeat_body = false;
  int body = 0;
};

std::string alpha_ordinal(std::size_t index) {
  std::string out;
  for (std::size_t value = index + 1; value; value = (value - 1) / 26)
    out.push_back(static_cast<char>('A' + (value - 1) % 26));
  std::reverse(out.begin(), out.end());
  return out;
}

std::string args_for_interval(const Interval& value) {
  const Node& n = *value.node;
  const Occurrence& o = value.occurrence;
  std::ostringstream out;
  out << "{\"semantic_kind\":"
      << json_quote(value.repeat_body ? "derived_repeat_body_window" : "published_tree_occurrence");
  if (value.repeat_body) {
    out << ",\"motif_alias\":" << json_quote(n.motif) << ",\"repeat_node_id\":" << json_quote(n.id)
        << ",\"aggregate_occurrence_idx\":" << o.index << ",\"body_ordinal\":" << value.body
        << ",\"repeat_count\":" << n.repeat << ",\"body_id\":"
        << json_quote(n.id + "@" + std::to_string(o.index) + "/body-" + std::to_string(value.body));
  } else {
    out << ",\"node_id\":" << json_quote(n.id) << ",\"occurrence_idx\":" << o.index;
  }
  out << ",\"database_index\":" << n.db << ",\"device_id\":" << n.device
      << ",\"view_name\":" << json_quote(n.view)
      << ",\"parent_node_id\":" << (n.parent_id.empty() ? "null" : json_quote(n.parent_id))
      << ",\"repeat_context\":" << json_quote(o.context)
      << ",\"rooted_role_path\":" << json_quote(n.path) << ",\"tree_depth\":" << n.depth
      << ",\"topology_signature_sha256\":" << json_quote(n.topology_sha)
      << ",\"anchor_start_idx\":" << o.anchor_start << ",\"anchor_end_idx\":" << o.anchor_end
      << ",\"anchor_count\":" << o.anchor_count << ",\"compute_us\":" << number(o.compute)
      << ",\"comm_us\":" << number(o.comm) << ",\"idle_us\":" << number(o.idle)
      << ",\"total_us\":" << number(o.total) << ",\"self_us\":" << number(o.self)
      << ",\"aux_events\":" << o.aux_events << ",\"aux_us\":" << number(o.aux_us) << '}';
  return out.str();
}

std::map<std::string, Node> load_nodes(sqlite3* db) {
  auto stmt = prepare(db,
                      "SELECT node_id,COALESCE(parent_node_id,''),local_node_id,path,kind,label,"
                      "COALESCE(category,''),db_idx,device_id,view_name,tree_depth,display_order,"
                      "COALESCE(repeat_count,0) FROM traceloom_v_tree_node "
                      "ORDER BY db_idx,device_id,display_order");
  std::map<std::string, Node> nodes;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    Node n;
    n.id = text(stmt.get(), 0);
    n.parent_id = text(stmt.get(), 1);
    n.local = text(stmt.get(), 2);
    n.path = text(stmt.get(), 3);
    n.kind = text(stmt.get(), 4);
    n.label = text(stmt.get(), 5);
    n.category = text(stmt.get(), 6);
    n.db = sqlite3_column_int(stmt.get(), 7);
    n.device = sqlite3_column_int(stmt.get(), 8);
    n.view = text(stmt.get(), 9);
    n.depth = sqlite3_column_int(stmt.get(), 10);
    n.order = sqlite3_column_int(stmt.get(), 11);
    n.repeat = sqlite3_column_int(stmt.get(), 12);
    n.key = node_key(n.db, n.device, n.view, n.id);
    if (!n.parent_id.empty()) n.parent_key = node_key(n.db, n.device, n.view, n.parent_id);
    nodes[n.key] = std::move(n);
  }
  for (auto& [id, n] : nodes)
    if (!n.parent_key.empty() && nodes.count(n.parent_key))
      nodes[n.parent_key].children.push_back(id);
  for (auto& [id, n] : nodes)
    std::sort(n.children.begin(), n.children.end(),
              [&](const auto& a, const auto& b) { return nodes[a].order < nodes[b].order; });
  std::function<std::string(Node&)> visit = [&](Node& n) {
    if (!n.topology.empty()) return n.topology;
    const char code = n.kind == "seq"      ? 'S'
                      : n.kind == "repeat" ? 'R'
                      : n.kind == "atom"   ? 'A'
                                           : 'U';
    n.topology = std::string(1, code) + "(";
    bool first = true;
    for (const auto& child : n.children) {
      if (!first) n.topology += ',';
      first = false;
      n.topology += visit(nodes[child]);
    }
    n.topology += ')';
    n.topology_sha = sha256_hex(n.topology);
    return n.topology;
  };
  for (auto& [id, n] : nodes) visit(n);
  std::set<std::string> repeat_hashes;
  for (const auto& [id, n] : nodes)
    if (n.kind == "repeat") repeat_hashes.insert(n.topology_sha);
  std::size_t i = 0;
  for (const auto& hash : repeat_hashes) {
    const auto alias = alpha_ordinal(i++);
    for (auto& [id, n] : nodes)
      if (n.kind == "repeat" && n.topology_sha == hash) n.motif = alias;
  }
  return nodes;
}

std::map<std::string, std::vector<Occurrence>> load_occurrences(sqlite3* db) {
  auto stmt = prepare(db,
                      "SELECT "
                      "node_id,db_idx,device_id,view_name,occurrence_idx,COALESCE(repeat_context,''"
                      "),start_ns,end_ns,"
                      "anchor_start_idx,anchor_end_idx,anchor_count,compute_us,comm_us,idle_us,"
                      "total_us,self_us,aux_events,aux_us FROM traceloom_tree_node_occurrence "
                      "ORDER BY db_idx,device_id,view_name,start_ns,end_ns,node_id,occurrence_idx");
  std::map<std::string, std::vector<Occurrence>> out;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    Occurrence o;
    o.node = text(stmt.get(), 0);
    const int db_index = sqlite3_column_int(stmt.get(), 1);
    const int device = sqlite3_column_int(stmt.get(), 2);
    const std::string view = text(stmt.get(), 3);
    o.index = sqlite3_column_int(stmt.get(), 4);
    o.context = text(stmt.get(), 5);
    o.start = sqlite3_column_int64(stmt.get(), 6);
    o.end = sqlite3_column_int64(stmt.get(), 7);
    o.anchor_start = sqlite3_column_int64(stmt.get(), 8);
    o.anchor_end = sqlite3_column_int64(stmt.get(), 9);
    o.anchor_count = sqlite3_column_int64(stmt.get(), 10);
    o.compute = sqlite3_column_double(stmt.get(), 11);
    o.comm = sqlite3_column_double(stmt.get(), 12);
    o.idle = sqlite3_column_double(stmt.get(), 13);
    o.total = sqlite3_column_double(stmt.get(), 14);
    o.self = sqlite3_column_double(stmt.get(), 15);
    o.aux_events = sqlite3_column_int64(stmt.get(), 16);
    o.aux_us = sqlite3_column_double(stmt.get(), 17);
    out[node_key(db_index, device, view, o.node)].push_back(std::move(o));
  }
  // Repeat labels describe a definition path, not a concrete parent instance.
  // Carry canonical HPO membership into the compatibility cost projection.
  if (has_object(db, "traceloom_position_member")) {
    auto members = prepare(db,
        "SELECT c.position_id,c.db_idx,c.device_id,t.semantic_projection,"
        "c.occurrence_idx,p.occurrence_idx,m.member_order "
        "FROM traceloom_position_member m "
        "JOIN traceloom_position_occurrence c ON c.occurrence_id=m.child_occurrence_id "
        "AND c.db_idx=m.db_idx AND c.device_id=m.device_id "
        "AND c.tree_id=m.tree_id AND c.view_name=m.view_name "
        "JOIN traceloom_position_occurrence p ON p.occurrence_id=m.parent_occurrence_id "
        "AND p.db_idx=m.db_idx AND p.device_id=m.device_id "
        "AND p.tree_id=m.tree_id AND p.view_name=m.view_name "
        "JOIN traceloom_semantic_tree t ON t.tree_id=m.tree_id "
        "AND t.db_idx=m.db_idx AND t.device_id=m.device_id AND t.view_name=m.view_name "
        "WHERE m.member_kind='child_occurrence'");
    std::map<std::pair<std::string, int>, Occurrence*> indexed;
    for (auto& [key, occurrences] : out)
      for (auto& occurrence : occurrences) indexed[{key, occurrence.index}] = &occurrence;
    while (sqlite3_step(members.get()) == SQLITE_ROW) {
      const auto key = node_key(sqlite3_column_int(members.get(), 1),
                                sqlite3_column_int(members.get(), 2), text(members.get(), 3),
                                text(members.get(), 0));
      auto found = indexed.find({key, sqlite3_column_int(members.get(), 4)});
      if (found == indexed.end()) continue;
      found->second->parent_index = sqlite3_column_int(members.get(), 5);
      found->second->member_order = sqlite3_column_int(members.get(), 6);
    }
  }
  return out;
}

std::vector<Interval> build_intervals(const std::map<std::string, Node>& nodes,
                                      const std::map<std::string, std::vector<Occurrence>>& occs) {
  std::vector<Interval> out;
  for (const auto& [id, n] : nodes)
    if (n.kind != "atom" && n.kind != "repeat") {
      auto it = occs.find(id);
      if (it != occs.end())
        for (const auto& o : it->second) out.push_back({&n, o, false, 0});
    }
  for (const auto& [id, n] : nodes)
    if (n.kind == "repeat") {
      using Bodies = std::map<int, std::vector<const Occurrence*>>;
      std::map<int, Bodies> grouped;
      auto aggregates = occs.find(id);
      if (aggregates == occs.end()) continue;
      // Older AugDBs have no HPO membership. Their context is usable only when
      // it identifies exactly one parent; never combine ambiguous instances.
      std::map<std::string, int> legacy_parents;
      for (const auto& aggregate : aggregates->second) {
        if (!legacy_parents.emplace(aggregate.context, aggregate.index).second)
          legacy_parents[aggregate.context] = -1;
      }
      for (const auto& child : n.children) {
        auto it = occs.find(child);
        if (it == occs.end()) continue;
        for (const auto& o : it->second) {
          if (o.parent_index >= 0) {
            grouped[o.parent_index][o.member_order].push_back(&o);
            continue;
          }
          const auto slash = o.context.rfind('/');
          const std::string component =
              slash == std::string::npos ? o.context : o.context.substr(slash + 1);
          const std::string prefix = n.local + "#";
          if (component.rfind(prefix, 0) != 0) continue;
          const std::string digits = component.substr(prefix.size());
          if (digits.empty() || !std::all_of(digits.begin(), digits.end(),
                                             [](unsigned char c) { return std::isdigit(c); }))
            continue;
          const auto parent = legacy_parents.find(
              slash == std::string::npos ? "" : o.context.substr(0, slash));
          if (parent == legacy_parents.end() || parent->second < 0)
            throw std::runtime_error("ambiguous repeat parent without HPO membership for " + child);
          grouped[parent->second][std::stoi(digits)].push_back(&o);
        }
      }
      for (const auto& aggregate : aggregates->second) {
        auto found = grouped.find(aggregate.index);
        if (found == grouped.end() || static_cast<int>(found->second.size()) != n.repeat)
          throw std::runtime_error("incomplete repeat-body projection for " + id + " occurrence " +
                                   std::to_string(aggregate.index));
        for (int body = 1; body <= n.repeat; ++body) {
          auto bit = found->second.find(body);
          if (bit == found->second.end() || bit->second.empty())
            throw std::runtime_error("missing repeat body for " + id);
          Occurrence o = aggregate;
          o.context = (aggregate.context.empty() ? "" : aggregate.context + "/") + n.local + "#" +
                      std::to_string(body);
          o.start = std::numeric_limits<std::int64_t>::max();
          o.end = 0;
          o.anchor_start = std::numeric_limits<std::int64_t>::max();
          o.anchor_end = 0;
          o.anchor_count = 0;
          o.compute = o.comm = o.idle = o.total = o.self = o.aux_us = 0;
          o.aux_events = 0;
          for (const auto* m : bit->second) {
            o.start = std::min(o.start, m->start);
            o.end = std::max(o.end, m->end);
            o.anchor_start = std::min(o.anchor_start, m->anchor_start);
            o.anchor_end = std::max(o.anchor_end, m->anchor_end);
            o.anchor_count += m->anchor_count;
            o.compute += m->compute;
            o.comm += m->comm;
            o.idle += m->idle;
            o.total += m->total;
            o.self += m->self;
            o.aux_events += m->aux_events;
            o.aux_us += m->aux_us;
          }
          out.push_back({&n, o, true, body});
        }
      }
    }
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
    return std::tie(a.node->db, a.node->device, a.node->depth, a.occurrence.start,
                    a.occurrence.end) < std::tie(b.node->db, b.node->device, b.node->depth,
                                                 b.occurrence.start, b.occurrence.end);
  });
  return out;
}

}  // namespace

bool is_queryable_database_timeline(const std::string& path) {
  try {
    auto db = open_db(path);
    if (!has_object(db.get(), "traceloom_metadata")) return false;
    auto stmt = prepare(db.get(),
                        "SELECT 1 FROM traceloom_metadata WHERE key='artifact_kind' AND "
                        "value='queryable_database_timeline' LIMIT 1");
    return sqlite3_step(stmt.get()) == SQLITE_ROW;
  } catch (...) {
    return false;
  }
}

PerfettoExportReceipt write_perfetto_trace(const std::string& analysis_db_path,
                                           const std::string& output_path,
                                           const PerfettoExportOptions& options) {
  const auto distributed = perfetto_internal::load_distributed_flat_timeline(options);
  auto db = open_db(analysis_db_path);
  if (!has_object(db.get(), "traceloom_v_tree_node") ||
      !has_object(db.get(), "traceloom_tree_node_occurrence"))
    throw std::invalid_argument(
        "input is not a TraceLoom queryable database timeline with tree occurrence views");
  auto nodes = load_nodes(db.get());
  auto occurrences = load_occurrences(db.get());
  auto intervals = build_intervals(nodes, occurrences);
  std::int64_t origin = std::numeric_limits<std::int64_t>::max();
  for (const auto& [id, values] : occurrences)
    for (const auto& o : values) origin = std::min(origin, o.start);
  if (options.include_raw_provider_timeline)
    origin = perfetto_internal::raw_timeline_origin(db.get(), origin);
  origin = std::min(origin, distributed.display_min_start);
  if (origin == std::numeric_limits<std::int64_t>::max()) origin = 0;
  TraceWriter writer(output_path, origin);
  PerfettoExportReceipt receipt;
  writer.process(110, "TraceLoom · execution tree + flat timeline events", 0);
  using perfetto_internal::TimelineSlice;
  using perfetto_internal::AnchorCoordinate;
  auto replay = perfetto_internal::load_replay_timeline(db.get());
  struct AnchorInfo { std::string event_id; std::int64_t stream = -1; };
  std::map<AnchorCoordinate, AnchorInfo> anchors;
  if (has_object(db.get(), "traceloom_anchor") && has_object(db.get(), "traceloom_event")) {
    auto stmt = prepare(db.get(), "SELECT a.db_idx,a.device_id,a.anchor_idx,a.event_id,e.stream_id "
        "FROM traceloom_anchor a LEFT JOIN traceloom_event e ON e.event_id=a.event_id "
        "AND e.db_idx=a.db_idx AND e.device_id=a.device_id");
    while (sqlite3_step(stmt.get()) == SQLITE_ROW)
      anchors[{sqlite3_column_int(stmt.get(),0),sqlite3_column_int(stmt.get(),1),sqlite3_column_int64(stmt.get(),2)}] =
          {text(stmt.get(),3),sqlite3_column_type(stmt.get(),4)==SQLITE_NULL ? -1 : sqlite3_column_int64(stmt.get(),4)};
  }
  std::vector<const Interval*> hidden_graph_units;
  for (const auto& value : intervals) {
    if (value.node->category!="graph_unit" || value.repeat_body) continue;
    bool complete=value.occurrence.anchor_count>0;
    for (auto index=value.occurrence.anchor_start; complete && index<=value.occurrence.anchor_end; ++index)
      complete=replay.expanded_anchors.count({value.node->db,value.node->device,index})!=0;
    if (complete) hidden_graph_units.push_back(&value);
  }
  const auto visible_depth = [&](const Node& node, const Occurrence& occurrence) {
    int depth=node.depth;
    for (const auto* hidden : hidden_graph_units)
      if (node.db==hidden->node->db && node.device==hidden->node->device && node.view==hidden->node->view &&
          node.depth>hidden->node->depth && occurrence.anchor_start>=hidden->occurrence.anchor_start &&
          occurrence.anchor_end<=hidden->occurrence.anchor_end) --depth;
    return depth;
  };
  // Splice complete launch realizations at the old terminal's display depth.
  // The graph container is not another display layer or another device event.
  std::map<AnchorCoordinate, std::vector<std::pair<std::string,int>>> placements;
  for (const auto& [id,node] : nodes) {
    if (node.kind != "atom") continue;
    const auto found = occurrences.find(id);
    if (found == occurrences.end()) continue;
    for (const auto& o : found->second)
      if (o.anchor_count==1 && replay.expanded_anchors.count({node.db,node.device,o.anchor_start}))
        placements[{node.db,node.device,o.anchor_start}].push_back({node.view,visible_depth(node,o)});
  }
  std::set<std::tuple<int,int,std::string,std::string>> replay_events;
  std::vector<TimelineSlice> slices;
  for (const auto& value : replay.slices) {
    const auto found = placements.find({value.db,value.device,value.anchor_index});
    if (found == placements.end()) continue;
    for (const auto& [view,depth] : found->second) {
      if (value.event && !distributed.ranks.empty()) continue;
      if (value.event && !replay_events.insert({value.db,value.device,view,value.event_id}).second) continue;
      auto slice=value; slice.view=view; slice.depth+=depth;
      slices.push_back(std::move(slice));
    }
  }
  for (const auto& value : intervals) {
    if (std::find(hidden_graph_units.begin(),hidden_graph_units.end(),&value)!=hidden_graph_units.end()) continue;
    std::string name;
    if (value.repeat_body)
      name = value.node->local + " · motif " + value.node->motif + " · body " +
             std::to_string(value.body) + "/" + std::to_string(value.node->repeat);
    else if (value.node->category == "model_rule")
      name = value.node->label + " · " + value.node->local;
    else
      name = value.node->local +
             (value.node->parent_id.empty() ? " · root" : " · " + value.node->kind);
    TimelineSlice slice;
    slice.db=value.node->db; slice.device=value.node->device; slice.view=value.node->view;
    slice.depth=visible_depth(*value.node,value.occurrence); slice.start=value.occurrence.start; slice.end=value.occurrence.end;
    slice.name=name; slice.category=value.repeat_body ? "traceloom.repeat_body_window" : "traceloom.structural_interval";
    slice.args=args_for_interval(value); slice.repeat_body=value.repeat_body;
    slices.push_back(std::move(slice));
  }
  if (distributed.ranks.empty()) {
    for (const auto& [id,node] : nodes) {
      if (node.kind != "atom") continue;
      const auto found=occurrences.find(id);
      if (found==occurrences.end()) continue;
      for (const auto& o : found->second) {
        const AnchorCoordinate coordinate{node.db,node.device,o.anchor_start};
        if (o.anchor_count==1 && placements.count(coordinate)) continue;
        const auto info=anchors.find(coordinate);
        const std::string event_id=info==anchors.end() ? "" : info->second.event_id;
        if (!event_id.empty() && replay_events.count({node.db,node.device,node.view,event_id})) continue;
        TimelineSlice slice;
        slice.db=node.db; slice.device=node.device; slice.view=node.view;
        slice.start=o.start; slice.end=o.end; slice.name=node.label; slice.event=true;
        slice.stream=info==anchors.end() ? -1 : info->second.stream;
        slice.event_id=event_id; slice.category="traceloom.timeline_event";
        slice.args=args_for_interval({&node,o,false,0});
        if (!event_id.empty()) {
          slice.args.pop_back();
          slice.args+=",\"event_id\":"+json_quote(event_id)+",\"stream_id\":"+std::to_string(slice.stream)+"}";
        }
        slices.push_back(std::move(slice));
      }
    }
  }
  perfetto_internal::project_collective_display(db.get(),slices);
  std::string display_provider;
  if (has_object(db.get(), "traceloom_raw_table")) {
    auto provider = prepare(db.get(),
        "SELECT 1 FROM traceloom_raw_table WHERE source_table IN "
        "('TASK','COMPUTE_TASK_INFO','COMMUNICATION_OP') LIMIT 1");
    if (sqlite3_step(provider.get()) == SQLITE_ROW) display_provider = "ascend";
  }
  perfetto_internal::apply_device_event_display_policy(slices,display_provider);
  perfetto_internal::write_common_timeline(writer,std::move(slices),receipt);
  std::set<std::string> motifs;
  for (const auto& [id, n] : nodes)
    if (n.kind == "repeat") motifs.insert(n.topology_sha);
  receipt.motif_classes = motifs.size();
  if (options.include_raw_provider_timeline)
    perfetto_internal::export_raw_provider_timeline(db.get(), writer, receipt);
  perfetto_internal::export_context_timeline(db.get(), writer);
  perfetto_internal::export_distributed_flat_timeline(distributed, writer, receipt);
  receipt.distributed_alignment = distributed.alignment;
  receipt.distributed_alignment_boundary = distributed.alignment_boundary;
  receipt.distributed_clock_model_sha256 = distributed.clock_model_sha256;
  writer.finish(
      "{\"format\":\"TraceLoom queryable database timeline Perfetto export\",\"analysis_db\":" +
      json_quote(analysis_db_path) + ",\"time_origin_ns\":" + std::to_string(origin) +
      ",\"aggregation_boundary\":\"Views overlap: never sum structure, device_events and raw_provider together. Filter one projection_plane and one rank/device/view; duration sums are not elapsed or busy time.\"" +
      ",\"repeat_body_slices\":" + std::to_string(receipt.repeat_body_slices) +
      ",\"replay_structure_slices\":" + std::to_string(receipt.replay_structure_slices) +
      ",\"replay_member_slices\":" + std::to_string(receipt.replay_member_slices) +
      ",\"motif_classes\":" + std::to_string(receipt.motif_classes) +
      ",\"distributed_alignment\":" +
      json_quote(distributed.alignment) +
      ",\"distributed_alignment_evidence_status\":" +
      json_quote(distributed.alignment_evidence_status) +
      ",\"distributed_alignment_boundary\":" +
      json_quote(distributed.alignment_boundary) +
      ",\"distributed_clock_model_path\":" +
      json_quote(distributed.clock_model_path) +
      ",\"distributed_clock_model_sha256\":" +
      json_quote(distributed.clock_model_sha256) +
      ",\"distributed_reference_rank\":" +
      std::to_string(distributed.reference_rank) +
      ",\"distributed_rank_tracks\":" +
      std::to_string(receipt.distributed_rank_tracks) + "}");
  return receipt;
}

}  // namespace traceloom::compat
