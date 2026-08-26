#include "traceloom/compat/perfetto_exporter.h"
#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path temp_path(const std::string& suffix) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("traceloom_perfetto_exporter_" + std::to_string(now) + suffix);
}

void exec_sql(sqlite3* db, const std::string& sql) {
  char* error = nullptr;
  const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
  if (rc != SQLITE_OK) {
    const std::string message = error ? error : "unknown SQLite error";
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
}

void add_device(sqlite3* db, int device, std::int64_t offset) {
  const std::string d = std::to_string(device);
  const std::string o = std::to_string(offset);
  exec_sql(db,
           "INSERT INTO traceloom_v_tree_node VALUES"
           "('node-N001','',0," +
               d +
               ",'expanded','N001',0,'N001',0,'seq',"
               "'root','',0),"
               "('node-N002','node-N001',0," +
               d +
               ",'expanded','N002',1,'N001/N002',1,'repeat','repeat','',2),"
               "('node-N003','node-N002',0," +
               d +
               ",'expanded','N003',2,'N001/N002/N003',2,'atom','MatMul',"
               "'compute',0)");
  exec_sql(db,
           "INSERT INTO traceloom_tree_node_occurrence VALUES"
           "('node-N001',0," +
               d + ",'expanded',0,'',0,1,2," + o + "," + o +
               "+1000,10,0,0,10,0,0,0),"
               "('node-N002',0," +
               d + ",'expanded',0,'',0,1,2," + o + "," + o +
               "+1000,10,0,0,10,0,0,0),"
               "('node-N003',0," +
               d + ",'expanded',0,'N002#1',0,0,1," + o + "," + o +
               "+400,4,0,0,4,4,0,0),"
               "('node-N003',0," +
               d + ",'expanded',1,'N002#2',1,1,1," + o + "+500," + o + "+1000,5,0,0,5,5,0,0)");
}

void create_fixture(const std::filesystem::path& path) {
  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    throw std::runtime_error("failed to create SQLite fixture");
  }
  try {
    exec_sql(db,
             "CREATE TABLE traceloom_metadata(key TEXT,value TEXT);"
             "INSERT INTO traceloom_metadata VALUES"
             "('artifact_kind','queryable_database_timeline');"
             "CREATE TABLE traceloom_v_tree_node("
             "node_id TEXT,parent_node_id TEXT,db_idx INTEGER,device_id "
             "INTEGER,view_name TEXT,local_node_id TEXT,display_order INTEGER,"
             "path TEXT,tree_depth INTEGER,kind TEXT,label TEXT,category TEXT,"
             "repeat_count INTEGER);"
             "CREATE TABLE traceloom_tree_node_occurrence("
             "node_id TEXT,db_idx INTEGER,device_id INTEGER,view_name TEXT,"
             "occurrence_idx INTEGER,repeat_context TEXT,anchor_start_idx "
             "INTEGER,anchor_end_idx INTEGER,anchor_count INTEGER,start_ns "
             "INTEGER,end_ns INTEGER,compute_us REAL,comm_us REAL,idle_us REAL,"
             "total_us REAL,self_us REAL,aux_events INTEGER,aux_us REAL);"
             "CREATE TABLE traceloom_raw_table("
             "source_id TEXT,source_path TEXT,source_table TEXT,"
             "embedded_table_name TEXT);"
             "CREATE TABLE raw_strings(id INTEGER,value TEXT);"
             "CREATE TABLE raw_pytorch(startNs INTEGER,endNs INTEGER,"
             "globalTid INTEGER,name INTEGER);"
             "INSERT INTO traceloom_raw_table VALUES"
             "('rank0','raw.db','STRING_IDS','raw_strings'),"
             "('rank0','raw.db','PYTORCH_API','raw_pytorch');"
             "INSERT INTO raw_strings VALUES(7,'torch.matmul');"
             "INSERT INTO raw_pytorch VALUES(900,1050,42,7);");
    add_device(db, 0, 1000);
    add_device(db, 1, 3000);
    sqlite3_close(db);
  } catch (...) {
    sqlite3_close(db);
    throw;
  }
}

void create_rank_fixture(const std::filesystem::path& path, std::int64_t offset) {
  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK)
    throw std::runtime_error("failed to create distributed rank fixture");
  try {
    exec_sql(db,
             "CREATE TABLE traceloom_v_tree_node("
             "node_id TEXT,parent_node_id TEXT,db_idx INTEGER,device_id INTEGER,"
             "view_name TEXT,local_node_id TEXT,display_order INTEGER,path TEXT,"
             "tree_depth INTEGER,kind TEXT,label TEXT,category TEXT,repeat_count INTEGER);"
             "CREATE TABLE traceloom_tree_node_occurrence("
             "node_id TEXT,db_idx INTEGER,device_id INTEGER,view_name TEXT,"
             "occurrence_idx INTEGER,repeat_context TEXT,anchor_start_idx INTEGER,"
             "anchor_end_idx INTEGER,anchor_count INTEGER,start_ns INTEGER,end_ns INTEGER,"
             "compute_us REAL,comm_us REAL,idle_us REAL,total_us REAL,self_us REAL,"
             "aux_events INTEGER,aux_us REAL);"
             "INSERT INTO traceloom_v_tree_node VALUES"
             "('node-C','',0,0,'native_report_tree','C',0,'C',0,'atom','MatMul','compute',0),"
             "('node-M','',0,0,'native_report_tree','M',1,'M',0,'atom','AllToAll','comm',0);");
    const std::string o = std::to_string(offset);
    exec_sql(db,
             "INSERT INTO traceloom_tree_node_occurrence VALUES"
             "('node-C',0,0,'native_report_tree',0,'',0,0,1," + o + "," + o +
                 "+100,0.1,0,0,0.1,0.1,0,0),"
             "('node-M',0,0,'native_report_tree',0,'',1,1,1," + o + "+200," + o +
                 "+300,0,0.1,0,0.1,0.1,0,0);");
    sqlite3_close(db);
  } catch (...) {
    sqlite3_close(db);
    throw;
  }
}

void create_clock_model_fixture(const std::filesystem::path& path,
                                const std::string& metric = "end") {
  std::ofstream output(path);
  output
      << "{\"format\":\"traceloom.distributed-clock-model/v1\","
         "\"rank\":1,\"reference_rank\":0,\"metric\":\""
      << metric
      << "\",\"status\":\"candidate_only\","
         "\"source_clock_domain\":\"rank-1-device\","
         "\"target_clock_domain\":\"rank-0-device\","
         "\"marker_contract\":\"collective-family-group-ordinal-candidate-v1\","
         "\"scale\":0.5,\"reference_source_ns\":50000,"
         "\"reference_target_ns\":10000}\n";
}

std::string read_plain(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

#if !defined(_WIN32)
std::string read_gzip(const std::filesystem::path& path) {
  const std::string command = "gzip -cd " + path.string();
  std::FILE* file = popen(command.c_str(), "r");
  if (!file) {
    throw std::runtime_error("failed to read gzip test output");
  }
  std::string output;
  char buffer[4096];
  std::size_t count = 0;
  while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    output.append(buffer, count);
  }
  const bool read_error = std::ferror(file) != 0;
  const int close_rc = pclose(file);
  if (read_error || close_rc != 0) {
    throw std::runtime_error("invalid gzip test output");
  }
  return output;
}
#endif

void require_contains(const std::string& haystack, const std::string& needle) {
  traceloom::testing::require(haystack.find(needle) != std::string::npos);
}

std::size_t count_occurrences(const std::string& haystack, const std::string& needle) {
  std::size_t count = 0;
  for (std::size_t at = 0; (at = haystack.find(needle, at)) != std::string::npos;
       at += needle.size()) {
    ++count;
  }
  return count;
}

}  // namespace

int main() {
  using traceloom::testing::require;

  const auto db_path = temp_path(".db");
  const auto rank0_path = temp_path(".rank0.db");
  const auto rank1_path = temp_path(".rank1.db");
  const auto model_path = temp_path(".models.jsonl");
  const auto incomplete_model_path = temp_path(".incomplete-models.jsonl");
  const auto json_path = temp_path(".json");
  const auto aligned_json_path = temp_path(".aligned.json");
#if defined(_WIN32)
  const auto cli_path = temp_path(".cli.json");
#else
  const auto gzip_path = temp_path(".json.gz");
  const auto cli_path = temp_path(".cli.json.gz");
#endif
  create_fixture(db_path);
  create_rank_fixture(rank0_path, 10000);
  create_rank_fixture(rank1_path, 50000);
  create_clock_model_fixture(model_path);
  create_clock_model_fixture(incomplete_model_path, "start");

  require(traceloom::compat::is_queryable_database_timeline(db_path.string()));
  traceloom::compat::PerfettoExportOptions distributed_options;
  distributed_options.distributed_ranks = {{0, rank0_path.string()}, {1, rank1_path.string()}};
  const auto receipt = traceloom::compat::write_perfetto_trace(
      db_path.string(), json_path.string(), distributed_options);
  require(receipt.structural_slices == 2);
  require(receipt.repeat_body_slices == 4);
  require(receipt.atomic_slices == 0);
  require(receipt.raw_slices == 1);
  require(receipt.distributed_timeline_slices == 4);
  require(receipt.distributed_rank_tracks == 2);
  require(receipt.motif_classes == 1);

  const std::string json = read_plain(json_path);
  require(count_occurrences(json, "N001 · root") == 2);
  require(count_occurrences(json, "N002 · motif A · body 1/2") == 2);
  require(count_occurrences(json, "N002 · motif A · body 2/2") == 2);
  require_contains(json, "\"repeat_node_id\":\"node-N002\"");
  require_contains(json, "\"repeat_context\":\"N002#1\"");
  require_contains(json, "\"database_index\":0,\"device_id\":1");
  require_contains(json, "\"view_name\":\"expanded\"");
  require_contains(json, "torch.matmul");
  require_contains(json, "timeline events · rank 0");
  require_contains(json, "timeline events · rank 1");
  require(count_occurrences(json, "\"ts\":9.100") == 2);
  require(count_occurrences(json, "\"name\":\"MatMul\"") == 2);
  require_contains(json, "\"node_id\":\"node-M\"");
  require_contains(json, "\"occurrence_idx\":0");
  require_contains(json, "\"alignment\":\"first_timeline_event_per_rank\"");
  require(json.find("timeline events · db 0") == std::string::npos);
  require(json.find("shape-") == std::string::npos);

  traceloom::compat::PerfettoExportOptions aligned_options =
      distributed_options;
  aligned_options.distributed_clock_model_path = model_path.string();
  const auto aligned_receipt = traceloom::compat::write_perfetto_trace(
      db_path.string(), aligned_json_path.string(), aligned_options);
  require(aligned_receipt.distributed_alignment ==
              "collective_end_affine_clock_model" &&
          aligned_receipt.distributed_clock_model_sha256.size() == 64);
  const std::string aligned_json = read_plain(aligned_json_path);
  require_contains(
      aligned_json,
      "\"distributed_alignment\":\"collective_end_affine_clock_model\"");
  require_contains(aligned_json,
                   "\"distributed_alignment_evidence_status\":"
                   "\"candidate_only\"");
  require_contains(aligned_json,
                   "\"clock_model_status\":\"candidate_only\"");
  require_contains(aligned_json,
                   "\"clock_model_status\":\"reference_identity\"");
  require_contains(aligned_json, "\"source_start_ns\":50200");
  require_contains(aligned_json, "\"clock_model_metric\":\"end\"");
  require_contains(aligned_json, "\"ts\":9.200");
  bool rejected_incomplete_model = false;
  try {
    auto invalid_options = distributed_options;
    invalid_options.distributed_clock_model_path =
        incomplete_model_path.string();
    traceloom::compat::write_perfetto_trace(
        db_path.string(), aligned_json_path.string(), invalid_options);
  } catch (const std::invalid_argument&) {
    rejected_incomplete_model = true;
  }
  require(rejected_incomplete_model);

#if !defined(_WIN32)
  traceloom::compat::PerfettoExportOptions gzip_options;
  gzip_options.include_raw_provider_timeline = false;
  const auto gzip_receipt =
      traceloom::compat::write_perfetto_trace(db_path.string(), gzip_path.string(), gzip_options);
  require(gzip_receipt.raw_slices == 0);
  require_contains(read_gzip(gzip_path), "N001 · root");
#endif

  const std::string cli_command = std::string("\"") + TRACELOOM_ANALYZER + "\" export-perfetto \"" +
                                  db_path.string() + "\" --output \"" + cli_path.string() +
                                  "\" --distributed-rank 0=\"" + rank0_path.string() +
                                  "\" --distributed-rank 1=\"" + rank1_path.string() +
                                  "\" --distributed-clock-model \"" +
                                  model_path.string() +
                                  "\" >/dev/null 2>&1";
  require(std::system(cli_command.c_str()) == 0);
#if defined(_WIN32)
  require_contains(read_plain(cli_path), "N002 · motif A · body 1/2");
#else
  require_contains(read_gzip(cli_path), "N002 · motif A · body 1/2");
  require_contains(read_gzip(cli_path), "timeline events · rank 1");
  require_contains(read_gzip(cli_path), "collective_end_affine_clock_model");
#endif

  std::remove(db_path.c_str());
  std::remove(rank0_path.c_str());
  std::remove(rank1_path.c_str());
  std::remove(model_path.c_str());
  std::remove(incomplete_model_path.c_str());
  std::remove(json_path.c_str());
  std::remove(aligned_json_path.c_str());
#if !defined(_WIN32)
  std::remove(gzip_path.c_str());
#endif
  std::remove(cli_path.c_str());
  return 0;
}
