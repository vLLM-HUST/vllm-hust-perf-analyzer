#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "traceloom/inference/trace.h"

namespace fs = std::filesystem;
using namespace traceloom::inference;
void check(bool ok, const char* what) {
  if (!ok) throw std::runtime_error(what);
}
std::string read(const fs::path& p) {
  std::ifstream f(p);
  return {std::istreambuf_iterator<char>(f), {}};
}
void write(const fs::path& p, const std::string& s) {
  std::ofstream f(p);
  f << s;
}
std::string scalar(const fs::path& p, const std::string& sql) {
  sqlite3* d = nullptr;
  check(sqlite3_open(p.c_str(), &d) == SQLITE_OK, "open test DB");
  sqlite3_stmt* q = nullptr;
  check(sqlite3_prepare_v2(d, sql.c_str(), -1, &q, nullptr) == SQLITE_OK,
        "prepare test query");
  check(sqlite3_step(q) == SQLITE_ROW, "query test row");
  auto s = sqlite3_column_text(q, 0);
  std::string out = s ? reinterpret_cast<const char*>(s) : "";
  sqlite3_finalize(q);
  sqlite3_close(d);
  return out;
}
void sql(const fs::path& p, const std::string& query) {
  sqlite3* d = nullptr;
  check(sqlite3_open(p.c_str(), &d) == SQLITE_OK, "open");
  check(sqlite3_exec(d, query.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK,
        "exec");
  sqlite3_close(d);
}
std::string event(int seq, const std::string& type,
                  const std::string& span = "0000000000000001",
                  const std::string& extra = "",
                  long long mono = 9007199254740993LL) {
  return "{\"schema_version\":1,\"event_id\":\"e" + std::to_string(seq) +
         "\",\"trace_id\":\"11111111111111111111111111111111\",\"span_id\":\"" +
         span + "\",\"producer_id\":\"p\",\"clock_id\":\"c\",\"sequence\":" +
         std::to_string(seq) + ",\"event_type\":\"" + type +
         "\",\"wall_time_ns\":1788919000000000000,\"monotonic_ns\":" +
         std::to_string(mono) + extra + "}\n";
}
template <class F>
void rejects(F&& f) {
  bool threw = false;
  try {
    f();
  } catch (const std::exception& e) {
    threw = true;
    check(std::string(e.what()).find("SECRET") == std::string::npos,
          "error leaked content");
  }
  check(threw, "expected rejection");
}
int main() {
  try {
    const fs::path root = fs::path(TRACELOOM_TEST_OUTPUT) / "inference-tests";
    fs::remove_all(root);
    fs::create_directories(root);
    const auto source = root / "events.ndjson", db = root / "analysis.db";
    write(source, read(fs::path(TRACELOOM_REPO_ROOT) /
                       "native/tests/fixtures/inference/observable.ndjson"));
    auto receipt = import_ndjson(source, db);
    check(receipt.inserted == 23, "fixture count");
    check(import_ndjson(source, db).duplicates == 23, "idempotent reimport");
    check(scalar(db, "SELECT count(*) FROM traceloom_v_inference_span") == "10",
          "span count");
    check(scalar(db,
                 "SELECT duration_ns FROM traceloom_v_inference_span WHERE "
                 "name='model.generate' AND has_end") == "45000000",
          "exact >2^53 duration");
    check(scalar(db,
                 "SELECT state FROM traceloom_v_inference_trace WHERE "
                 "trace_id='22222222222222222222222222222222'") ==
              "finished_with_incomplete_spans",
          "cancel does not close child");
    check(scalar(db,
                 "SELECT max(observed_path_ns) FROM "
                 "traceloom_inference_span_metric") == "90000000",
          "fork/join path without parent cost");
    check(scalar(db,
                 "SELECT count(*) FROM traceloom_inference_event WHERE "
                 "payload LIKE '%Selected%'") == "0",
          "summary omitted");
    export_trace(db, root / "view.html", root / "view.json", true);
    const auto html = read(root / "view.html"),
               perfetto = read(root / "view.json");
    check(html.find("model.generate") != std::string::npos &&
              html.find("duration unknown") != std::string::npos,
          "HTML states");
    check(perfetto.find("\"ph\":\"i\"") != std::string::npos &&
              perfetto.find("\"dur\":45000") != std::string::npos,
          "Perfetto durations");
    // Roll back valid earlier rows when a later record conflicts.
    const auto start = event(0, "span_start", "0000000000000001",
                             ",\"name\":\"run\",\"kind\":\"pipeline\"");
    const auto end = event(1, "span_end", "0000000000000001",
                           ",\"status\":\"ok\"", 9007199254741000LL);
    auto simple = root / "simple.db";
    write(source, end);
    import_ndjson(source, simple);
    check(scalar(simple, "SELECT status FROM traceloom_v_inference_span") ==
              "missing_start",
          "end before start");
    write(source, start);
    import_ndjson(source, simple);
    check(scalar(simple,
                 "SELECT duration_ns FROM traceloom_v_inference_span") == "7",
          "exact integer");
    write(source, event(2, "metrics", "0000000000000001") +
                      event(1, "span_end", "0000000000000001",
                            ",\"status\":\"error\"", 9007199254741000LL));
    rejects([&] { import_ndjson(source, simple); });
    check(
        scalar(simple, "SELECT count(*) FROM traceloom_inference_event") == "2",
        "transaction rollback");
    // Trailing fragment is deferred; repair and import later.
    write(source, start.substr(0, start.size() - 1));
    auto tail = import_ndjson(source, root / "tail.db");
    check(tail.inserted == 0 && tail.pending_tail_bytes > 0, "pending tail");
    write(source, start);
    check(import_ndjson(source, root / "tail.db").inserted == 1,
          "tail repaired");
    // Explicit summary policy and HTML escaping, no raw unknown attributes
    // persisted.
    auto malicious = event(0, "span_start", "0000000000000001",
                           ",\"name\":\"run\",\"kind\":\"pipeline\","
                           "\"attributes\":{\"prompt\":\"SECRET\"},\"decision_"
                           "summary\":\"</script><script>alert(1)</script>\"");
    write(source, malicious);
    ImportOptions summary;
    summary.include_summaries = true;
    auto privacy = import_ndjson(source, root / "privacy.db", summary);
    check(privacy.discarded_attributes == 1, "discard attr counter");
    export_trace(root / "privacy.db", root / "privacy.html",
                 root / "privacy.json");
    check(
        read(root / "privacy.html").find("<script>alert") == std::string::npos,
        "HTML escaped");
    check(read(root / "privacy.json").find("</script>") == std::string::npos,
          "JSON escaped");
    check(read(root / "privacy.db").find("SECRET") == std::string::npos,
          "no raw storage");
    rejects([&] { import_ndjson(source, root / "privacy.db"); });
    // Bad type, unsupported version, duplicate key, UTF8, negative time,
    // limits.
    for (const auto& bad :
         {std::string("{\"schema_version\":2}\n"),
          std::string("{\"a\":1,\"a\":2}\n"), std::string("{\"SECRET\":1}\n"),
          std::string(16385, 'x') + "\n", std::string("\xff\n")}) {
      write(source, bad);
      rejects([&] { import_ndjson(source, root / "bad.db"); });
    }
    auto badclock = end;
    auto pos = badclock.find("\"clock_id\":\"c\"");
    badclock.replace(pos, 14, "\"clock_id\":\"d\"");
    write(source, start + badclock);
    rejects([&] { import_ndjson(source, root / "clock.db"); });
    write(source, start + event(1, "span_end", "0000000000000001",
                                ",\"status\":\"ok\"", 2));
    rejects([&] { import_ndjson(source, root / "negative.db"); });
    write(source, start + end);
    ImportOptions limited;
    limited.max_events = 1;
    rejects([&] { import_ndjson(source, root / "limited.db", limited); });
    // Parent/dependency cycles reject, missing endpoints remain explicit.
    auto a = event(0, "span_start", "0000000000000001",
                   ",\"name\":\"a\",\"kind\":\"step\",\"parent_span_id\":"
                   "\"0000000000000002\"");
    auto b = event(1, "span_start", "0000000000000002",
                   ",\"name\":\"b\",\"kind\":\"step\",\"parent_span_id\":"
                   "\"0000000000000001\"");
    write(source, a);
    import_ndjson(source, root / "parent.db");
    check(
        scalar(root / "parent.db",
               "SELECT missing_parent FROM traceloom_inference_span_metric") ==
            "1",
        "missing parent retained");
    write(source, b);
    rejects([&] { import_ndjson(source, root / "parent.db"); });
    a = event(
        0, "span_start", "0000000000000001",
        ",\"name\":\"a\",\"kind\":\"step\",\"links\":[\"0000000000000002\"]");
    b = event(
        1, "span_start", "0000000000000002",
        ",\"name\":\"b\",\"kind\":\"step\",\"links\":[\"0000000000000001\"]");
    write(source, a + b);
    rejects([&] { import_ndjson(source, root / "cycle.db"); });
    // Namespaced schema preserves unrelated profiler data; forward migration
    // fails closed.
    sql(simple,
        "CREATE TABLE profiler_receipt(x); INSERT INTO "
        "profiler_receipt VALUES(42)");
    write(source, start);
    import_ndjson(source, simple);
    check(scalar(simple, "SELECT x FROM profiler_receipt") == "42",
          "unrelated tables retained");
    sql(simple, "UPDATE traceloom_inference_meta SET version=2");
    rejects([&] { import_ndjson(source, simple); });
    check(scalar(simple, "SELECT version FROM traceloom_inference_meta") == "2",
          "future schema unchanged");
    rejects([&] { import_ndjson(source, source); });
    rejects([&] { export_trace(db, db, ""); });
    std::cout << "inference contract, privacy, lifecycle, graph and projection "
                 "checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
  return 0;
}
