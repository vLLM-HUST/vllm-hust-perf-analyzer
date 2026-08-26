#include "perfetto_export_internal.h"

#include <sqlite3.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "traceloom/core/sha256.h"

namespace traceloom::compat::perfetto_internal {
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
    throw std::runtime_error("failed to open distributed TraceLoom timeline DB: " + path +
                             ": " + error);
  }
  sqlite3_busy_timeout(raw, 30000);
  return Db(raw);
}

Statement prepare(sqlite3* db, const std::string& sql) {
  sqlite3_stmt* raw = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK) {
    throw std::runtime_error("distributed Perfetto export query failed: " +
                             std::string(sqlite3_errmsg(db)) + "\n" + sql);
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

std::string json_quote(const std::string& value) {
  std::ostringstream out;
  out << '"';
  for (unsigned char ch : value) {
    switch (ch) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (ch < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(ch) << std::dec;
        } else {
          out << static_cast<char>(ch);
        }
    }
  }
  return out.str() + '"';
}

std::string number(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(3) << value;
  return out.str();
}

std::string precise_number(long double value) {
  std::ostringstream out;
  out << std::setprecision(std::numeric_limits<long double>::max_digits10)
      << value;
  return out.str();
}

DistributedRankTimeline load_rank(const PerfettoDistributedRankInput& input) {
  if (input.rank < 0) throw std::invalid_argument("distributed rank must be non-negative");
  if (input.timeline_db_path.empty())
    throw std::invalid_argument("distributed rank timeline DB path must not be empty");
  auto db = open_db(input.timeline_db_path);
  if (!has_object(db.get(), "traceloom_v_tree_node") ||
      !has_object(db.get(), "traceloom_tree_node_occurrence"))
    throw std::invalid_argument(
        "distributed rank " + std::to_string(input.rank) +
        " input is not a TraceLoom timeline DB with tree occurrences: " +
        input.timeline_db_path);

  auto stmt = prepare(
      db.get(),
      "SELECT n.node_id,n.local_node_id,n.view_name,n.path,n.label,"
      "COALESCE(n.category,''),n.db_idx,n.device_id,n.tree_depth,"
      "o.occurrence_idx,COALESCE(o.repeat_context,''),o.start_ns,o.end_ns,"
      "o.anchor_start_idx,o.anchor_end_idx,o.anchor_count,o.compute_us,o.comm_us,"
      "o.idle_us,o.total_us,o.self_us,o.aux_events,o.aux_us "
      "FROM traceloom_v_tree_node n JOIN traceloom_tree_node_occurrence o "
      "ON o.node_id=n.node_id AND o.db_idx=n.db_idx AND o.device_id=n.device_id "
      "AND o.view_name=n.view_name WHERE n.kind='atom' "
      "ORDER BY o.start_ns,o.end_ns,n.node_id,o.occurrence_idx");

  DistributedRankTimeline rank;
  rank.rank = input.rank;
  rank.timeline_db_path = input.timeline_db_path;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    DistributedTimelineEventSlice event;
    event.node_id = text(stmt.get(), 0);
    event.local_node_id = text(stmt.get(), 1);
    event.view_name = text(stmt.get(), 2);
    event.rooted_role_path = text(stmt.get(), 3);
    event.label = text(stmt.get(), 4);
    event.category = text(stmt.get(), 5);
    event.database_index = sqlite3_column_int(stmt.get(), 6);
    event.device_id = sqlite3_column_int(stmt.get(), 7);
    event.tree_depth = sqlite3_column_int(stmt.get(), 8);
    event.occurrence_index = sqlite3_column_int64(stmt.get(), 9);
    event.repeat_context = text(stmt.get(), 10);
    event.start = sqlite3_column_int64(stmt.get(), 11);
    event.end = sqlite3_column_int64(stmt.get(), 12);
    event.anchor_start = sqlite3_column_int64(stmt.get(), 13);
    event.anchor_end = sqlite3_column_int64(stmt.get(), 14);
    event.anchor_count = sqlite3_column_int64(stmt.get(), 15);
    event.compute_us = sqlite3_column_double(stmt.get(), 16);
    event.comm_us = sqlite3_column_double(stmt.get(), 17);
    event.idle_us = sqlite3_column_double(stmt.get(), 18);
    event.total_us = sqlite3_column_double(stmt.get(), 19);
    event.self_us = sqlite3_column_double(stmt.get(), 20);
    event.aux_events = sqlite3_column_int64(stmt.get(), 21);
    event.aux_us = sqlite3_column_double(stmt.get(), 22);
    rank.events.push_back(std::move(event));
  }
  if (rank.events.empty())
    throw std::invalid_argument("distributed rank " + std::to_string(input.rank) +
                                " TraceLoom timeline has no atom occurrences");
  rank.source_anchor = rank.events.front().start;
  rank.timeline_db_sha256 = sha256_file_hex(input.timeline_db_path);
  return rank;
}

std::string args_for_event(const DistributedFlatTimeline& timeline,
                           const DistributedRankTimeline& rank,
                           const DistributedTimelineEventSlice& event) {
  std::ostringstream out;
  out << "{\"semantic_kind\":\"distributed_traceloom_timeline_event\""
      << ",\"rank\":" << rank.rank
      << ",\"source_timeline_db\":" << json_quote(rank.timeline_db_path)
      << ",\"source_timeline_db_sha256\":" << json_quote(rank.timeline_db_sha256)
      << ",\"alignment\":" << json_quote(timeline.alignment)
      << ",\"display_reference_rank\":" << timeline.reference_rank
      << ",\"source_start_ns\":" << event.start << ",\"source_end_ns\":" << event.end
      << ",\"node_id\":" << json_quote(event.node_id)
      << ",\"local_node_id\":" << json_quote(event.local_node_id)
      << ",\"occurrence_idx\":" << event.occurrence_index
      << ",\"database_index\":" << event.database_index
      << ",\"device_id\":" << event.device_id
      << ",\"view_name\":" << json_quote(event.view_name)
      << ",\"repeat_context\":" << json_quote(event.repeat_context)
      << ",\"rooted_role_path\":" << json_quote(event.rooted_role_path)
      << ",\"tree_depth\":" << event.tree_depth
      << ",\"event_category\":" << json_quote(event.category)
      << ",\"anchor_start_idx\":" << event.anchor_start
      << ",\"anchor_end_idx\":" << event.anchor_end
      << ",\"anchor_count\":" << event.anchor_count
      << ",\"compute_us\":" << number(event.compute_us)
      << ",\"comm_us\":" << number(event.comm_us)
      << ",\"idle_us\":" << number(event.idle_us)
      << ",\"total_us\":" << number(event.total_us)
      << ",\"self_us\":" << number(event.self_us)
      << ",\"aux_events\":" << event.aux_events
      << ",\"aux_us\":" << number(event.aux_us);
  if (timeline.clock_models.empty()) {
    out << ",\"source_anchor_ns\":" << rank.source_anchor
        << ",\"display_reference_anchor_ns\":"
        << timeline.display_reference_anchor;
  } else if (rank.rank == timeline.reference_rank) {
    out << ",\"clock_model_status\":\"reference_identity\""
        << ",\"clock_model_marker_contract\":\"reference-rank-identity\""
        << ",\"clock_model_metric\":\"end\""
        << ",\"clock_model_receipt_sha256\":"
        << json_quote(timeline.clock_model_sha256);
  } else {
    const auto& model = timeline.clock_models.at(rank.rank);
    out << ",\"clock_model_status\":"
        << json_quote(std::string(clock_calibration_status_name(model.status)))
        << ",\"clock_model_marker_contract\":"
        << json_quote(model.marker_contract)
        << ",\"clock_model_metric\":\"end\""
        << ",\"clock_model_scale\":" << json_quote(precise_number(model.scale))
        << ",\"clock_model_reference_source_ns\":"
        << json_quote(precise_number(model.reference_source_ns))
        << ",\"clock_model_reference_target_ns\":"
        << json_quote(precise_number(model.reference_target_ns))
        << ",\"clock_model_receipt_sha256\":"
        << json_quote(timeline.clock_model_sha256);
  }
  out << '}';
  return out.str();
}

}  // namespace

DistributedFlatTimeline load_distributed_flat_timeline(const PerfettoExportOptions& options) {
  DistributedFlatTimeline timeline;
  if (options.distributed_ranks.empty()) return timeline;
  timeline.reference_rank = options.distributed_reference_rank;
  std::optional<DistributedClockModelSet> clock_models;
  if (!options.distributed_clock_model_path.empty()) {
    clock_models = load_distributed_clock_models(
        options.distributed_clock_model_path, options.distributed_ranks,
        timeline.reference_rank);
  }
  std::set<int> ranks;
  for (const auto& input : options.distributed_ranks) {
    if (!ranks.insert(input.rank).second)
      throw std::invalid_argument("duplicate distributed rank: " + std::to_string(input.rank));
    timeline.ranks.push_back(load_rank(input));
  }
  std::sort(timeline.ranks.begin(), timeline.ranks.end(),
            [](const auto& left, const auto& right) { return left.rank < right.rank; });
  const auto reference =
      std::find_if(timeline.ranks.begin(), timeline.ranks.end(), [&](const auto& rank) {
        return rank.rank == timeline.reference_rank;
      });
  if (reference == timeline.ranks.end())
    throw std::invalid_argument("distributed reference rank " +
                                std::to_string(timeline.reference_rank) + " was not provided");
  timeline.display_reference_anchor = reference->source_anchor;
  if (!clock_models) {
    timeline.alignment = "first_timeline_event_per_rank";
    timeline.alignment_evidence_status = "display_only";
    timeline.alignment_boundary =
        "first-event translation only; no shared absolute-clock claim";
  } else {
    timeline.alignment = "collective_end_affine_clock_model";
    timeline.alignment_evidence_status = clock_models->evidence_status;
    timeline.alignment_boundary =
        clock_models->evidence_status == "candidate_only"
            ? "display-only candidate mapping; source timestamps retained; no calibrated global-time claim"
            : "validated affine clock-model mapping; source timestamps retained";
    timeline.clock_model_path = clock_models->path;
    timeline.clock_model_sha256 = clock_models->sha256;
    timeline.clock_models = std::move(clock_models->models);
  }
  for (const auto& rank : timeline.ranks) {
    for (const auto& event : rank.events) {
      const std::int64_t display_start =
          map_distributed_display_timestamp(timeline, rank, event.start);
      const std::int64_t display_end =
          map_distributed_display_timestamp(timeline, rank, event.end);
      if (display_end < display_start) {
        throw std::invalid_argument(
            "distributed clock model produced a negative duration for rank " +
            std::to_string(rank.rank));
      }
      timeline.display_min_start =
          std::min(timeline.display_min_start, display_start);
    }
  }
  return timeline;
}

std::int64_t map_distributed_display_timestamp(
    const DistributedFlatTimeline& timeline,
    const DistributedRankTimeline& rank,
    std::int64_t source_timestamp_ns) {
  if (timeline.clock_models.empty()) {
    return timeline.display_reference_anchor +
           (source_timestamp_ns - rank.source_anchor);
  }
  if (rank.rank == timeline.reference_rank) return source_timestamp_ns;
  const auto found = timeline.clock_models.find(rank.rank);
  if (found == timeline.clock_models.end()) {
    throw std::invalid_argument("missing distributed clock model for rank " +
                                std::to_string(rank.rank));
  }
  const auto mapped =
      map_clock_timestamp_ns_for_display(found->second, source_timestamp_ns);
  if (!mapped) {
    throw std::invalid_argument(
        "distributed clock model cannot map timestamp for rank " +
        std::to_string(rank.rank));
  }
  return *mapped;
}

void export_distributed_flat_timeline(const DistributedFlatTimeline& timeline,
                                      RawTraceWriter& writer,
                                      PerfettoExportReceipt& receipt) {
  if (timeline.ranks.empty()) return;
  constexpr int pid = 120;
  writer.process(pid,
                 timeline.clock_models.empty()
                     ? "TraceLoom · distributed flat timeline events · first-event normalized"
                     : "TraceLoom · distributed flat timeline events · collective-end affine aligned",
                 1);
  int tid = 1;
  for (const auto& rank : timeline.ranks) {
    writer.thread(pid, tid, "timeline events · rank " + std::to_string(rank.rank), tid);
    for (const auto& event : rank.events) {
      const std::int64_t display_start = map_distributed_display_timestamp(
          timeline, rank, event.start);
      const std::int64_t display_end = map_distributed_display_timestamp(
          timeline, rank, event.end);
      if (display_end < display_start) {
        throw std::invalid_argument(
            "distributed clock model produced a negative duration for rank " +
            std::to_string(rank.rank));
      }
      writer.slice(pid, tid, event.label, display_start, display_end,
                   "traceloom.distributed_timeline_event",
                   args_for_event(timeline, rank, event));
      ++receipt.distributed_timeline_slices;
    }
    ++receipt.distributed_rank_tracks;
    ++tid;
  }
}

}  // namespace traceloom::compat::perfetto_internal
