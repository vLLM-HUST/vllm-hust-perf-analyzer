// Golden fixture check for the idle-evidence contract counterexample:
// "host wait exists, but visible idle is zero".
//
// Loads the checked-in synthetic fixture
//   tests/fixtures/idle_evidence/host_wait_zero_visible_idle/
// (msprof-schema SQLite database + ground_truth.json, contract section 10)
// and asserts BOTH sides of the counterexample hold simultaneously:
//
//   1. host wait present: the fixture's CANN_API table carries an
//      `aclrtSynchronizeStream` event with positive duration (host-side
//      sync evidence); the host waited while the device was busy.
//   2. visible idle zero: the analyzer's productive timeline over the same
//      fixture is exactly one `productive_active` interval covering the
//      whole analysis span — no `visible_productive_idle` interval.
//
// This is the executable boundary check for the host-wait vs
// visible_productive_idle distinction (contract sections 3, 5, 12): a
// host-side wait must NOT fabricate device visible idle, and
// visible_productive_idle must be zero when the device timeline is fully
// productive. The fixture is synthetic (evidence_label simulation/model);
// it is contract/example evidence and does not replace matched A/B of
// runtime traces.

#include "traceloom/adapters/ascend_sqlite_adapter.h"
#include "traceloom/analysis/idle_explanation.h"
#include "traceloom/analysis/idle_evidence_semantic_rules.h"
#include "traceloom/analysis/productive_timeline.h"
#include "traceloom/analysis/semantic_task_classifier.h"
#include "traceloom/analysis/stream_state_timeline.h"
#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Minimal JSON reader for ground_truth.json (test-local; supports the subset
// the fixture ground truth uses: objects, arrays, strings, numbers, booleans,
// null).
// ---------------------------------------------------------------------------

struct JsonValue {
  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue>;
  // Integers are stored as int64_t (nanosecond timestamps exceed 2^53, where
  // double loses precision); only fractional numbers use double.
  using Storage = std::variant<std::nullptr_t, bool, std::int64_t, double,
                               std::string, Array, Object>;

  Storage storage;
};

class JsonParser {
 public:
  explicit JsonParser(std::string text) : text_(std::move(text)) {}

  JsonValue parse() {
    JsonValue value = parse_value();
    skip_space();
    if (pos_ != text_.size()) {
      throw std::invalid_argument("unexpected trailing JSON content");
    }
    return value;
  }

 private:
  JsonValue parse_value() {
    skip_space();
    if (pos_ >= text_.size()) {
      throw std::invalid_argument("unexpected end of JSON input");
    }
    const char ch = text_[pos_];
    if (ch == '{') {
      return JsonValue{parse_object()};
    }
    if (ch == '[') {
      return JsonValue{parse_array()};
    }
    if (ch == '"') {
      return JsonValue{parse_string()};
    }
    if (ch == 't') {
      consume_literal("true");
      return JsonValue{true};
    }
    if (ch == 'f') {
      consume_literal("false");
      return JsonValue{false};
    }
    if (ch == 'n') {
      consume_literal("null");
      return JsonValue{nullptr};
    }
    if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
      return parse_number();
    }
    throw std::invalid_argument("invalid JSON value");
  }

  JsonValue::Object parse_object() {
    expect('{');
    JsonValue::Object object;
    skip_space();
    if (peek('}')) {
      ++pos_;
      return object;
    }
    while (true) {
      skip_space();
      const std::string key = parse_string();
      skip_space();
      expect(':');
      object.emplace(key, parse_value());
      skip_space();
      if (peek('}')) {
        ++pos_;
        return object;
      }
      expect(',');
    }
  }

  JsonValue::Array parse_array() {
    expect('[');
    JsonValue::Array array;
    skip_space();
    if (peek(']')) {
      ++pos_;
      return array;
    }
    while (true) {
      array.push_back(parse_value());
      skip_space();
      if (peek(']')) {
        ++pos_;
        return array;
      }
      expect(',');
    }
  }

  std::string parse_string() {
    expect('"');
    std::string value;
    while (pos_ < text_.size()) {
      const char ch = text_[pos_++];
      if (ch == '"') {
        return value;
      }
      if (ch != '\\') {
        value.push_back(ch);
        continue;
      }
      if (pos_ >= text_.size()) {
        throw std::invalid_argument("unterminated JSON escape");
      }
      const char escaped = text_[pos_++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          value.push_back(escaped);
          break;
        case 'b':
          value.push_back('\b');
          break;
        case 'f':
          value.push_back('\f');
          break;
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        default:
          throw std::invalid_argument("unsupported JSON escape");
      }
    }
    throw std::invalid_argument("unterminated JSON string");
  }

  JsonValue parse_number() {
    const std::size_t begin = pos_;
    if (peek('-')) {
      ++pos_;
    }
    bool fractional = false;
    while (pos_ < text_.size() &&
           std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
      ++pos_;
    }
    if (peek('.')) {
      fractional = true;
      ++pos_;
      while (pos_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
        ++pos_;
      }
    }
    const std::string token = text_.substr(begin, pos_ - begin);
    if (fractional) {
      return JsonValue{std::stod(token)};
    }
    return JsonValue{std::stoll(token)};
  }

  void consume_literal(const char* literal) {
    const std::string expected(literal);
    if (text_.compare(pos_, expected.size(), expected) != 0) {
      throw std::invalid_argument("invalid JSON literal");
    }
    pos_ += expected.size();
  }

  void expect(char expected) {
    skip_space();
    if (pos_ >= text_.size() || text_[pos_] != expected) {
      throw std::invalid_argument("unexpected JSON token");
    }
    ++pos_;
  }

  bool peek(char expected) const {
    return pos_ < text_.size() && text_[pos_] == expected;
  }

  void skip_space() {
    while (pos_ < text_.size() &&
           std::isspace(static_cast<unsigned char>(text_[pos_]))) {
      ++pos_;
    }
  }

  std::string text_;
  std::size_t pos_ = 0;
};

const JsonValue* find_member(const JsonValue& value, const char* key) {
  const auto* object = std::get_if<JsonValue::Object>(&value.storage);
  if (object == nullptr) {
    return nullptr;
  }
  const auto found = object->find(key);
  return found == object->end() ? nullptr : &found->second;
}

std::int64_t member_int64(const JsonValue& value, const char* key) {
  const JsonValue* member = find_member(value, key);
  if (member == nullptr) {
    throw std::invalid_argument(std::string("missing JSON member: ") + key);
  }
  const auto* integer = std::get_if<std::int64_t>(&member->storage);
  if (integer != nullptr) {
    return *integer;
  }
  const auto* number = std::get_if<double>(&member->storage);
  if (number == nullptr) {
    throw std::invalid_argument(std::string("JSON member is not a number: ") +
                                key);
  }
  return static_cast<std::int64_t>(*number);
}

std::string member_string(const JsonValue& value, const char* key) {
  const JsonValue* member = find_member(value, key);
  if (member == nullptr) {
    throw std::invalid_argument(std::string("missing JSON member: ") + key);
  }
  const auto* string = std::get_if<std::string>(&member->storage);
  if (string == nullptr) {
    throw std::invalid_argument(std::string("JSON member is not a string: ") +
                                key);
  }
  return *string;
}

bool member_bool(const JsonValue& value, const char* key) {
  const JsonValue* member = find_member(value, key);
  if (member == nullptr) {
    throw std::invalid_argument(std::string("missing JSON member: ") + key);
  }
  const auto* boolean = std::get_if<bool>(&member->storage);
  if (boolean == nullptr) {
    throw std::invalid_argument(std::string("JSON member is not a boolean: ") +
                                key);
  }
  return *boolean;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::invalid_argument("cannot open: " + path.string());
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// Host-side evidence, read directly from the fixture: any CANN_API row named
// aclrtSynchronizeStream with a positive duration counts as a host wait.
struct HostWaitEvidence {
  bool present = false;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

HostWaitEvidence read_host_wait(const std::filesystem::path& fixture_db) {
  sqlite3* db = nullptr;
  const int rc = sqlite3_open_v2(fixture_db.string().c_str(), &db,
                                 SQLITE_OPEN_READONLY, nullptr);
  if (rc != SQLITE_OK) {
    throw std::invalid_argument("cannot open fixture db: " +
                                fixture_db.string());
  }
  HostWaitEvidence evidence;
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT startNs, endNs FROM CANN_API "
      "JOIN STRING_IDS ON CANN_API.name = STRING_IDS.id "
      "WHERE STRING_IDS.value = 'aclrtSynchronizeStream' "
      "ORDER BY startNs, endNs";
  const int prepare_rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
  if (prepare_rc != SQLITE_OK) {
    // Capture the error message before closing the handle; sqlite3_errmsg
    // after sqlite3_close is use-after-close.
    const std::string error = sqlite3_errmsg(db);
    sqlite3_close(db);
    throw std::invalid_argument("cannot query CANN_API: " + error);
  }
  while (true) {
    const int step_rc = sqlite3_step(stmt);
    if (step_rc == SQLITE_DONE) {
      break;
    }
    if (step_rc != SQLITE_ROW) {
      const std::string error = sqlite3_errmsg(db);
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      throw std::invalid_argument("cannot step CANN_API query: " + error);
    }
    const std::int64_t start_ns = sqlite3_column_int64(stmt, 0);
    const std::int64_t end_ns = sqlite3_column_int64(stmt, 1);
    if (end_ns > start_ns) {
      if (!evidence.present || start_ns < evidence.start_ns) {
        evidence.start_ns = start_ns;
      }
      if (!evidence.present || end_ns > evidence.end_ns) {
        evidence.end_ns = end_ns;
      }
      evidence.present = true;
    }
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return evidence;
}

// Device-side lineage key "TABLE:<source_row_id>", matching the contract's
// expected_source_keys vocabulary.
std::string source_key(const traceloom::NativeIr& ir,
                       const traceloom::ProductiveSourceLink& link) {
  const traceloom::TraceEventRow& event = ir.trace_events.row(link.trace_event_id);
  const traceloom::SourceRefRow& source = ir.source_refs.row(event.source_ref_id);
  return source.table_name + ":" + std::to_string(event.source_row_id);
}

std::string source_key(const traceloom::NativeIr& ir,
                       const traceloom::StreamStateSourceLink& link) {
  const traceloom::TraceEventRow& event = ir.trace_events.row(link.trace_event_id);
  const traceloom::SourceRefRow& source = ir.source_refs.row(event.source_ref_id);
  return source.table_name + ":" + std::to_string(event.source_row_id);
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  const std::filesystem::path fixture_dir(TRACELOOM_GOLDEN_FIXTURE_DIR);
  const std::filesystem::path fixture_db =
      fixture_dir / "host_wait_zero_visible_idle.db";
  const std::filesystem::path ground_truth_path =
      fixture_dir / "ground_truth.json";
  require(std::filesystem::exists(fixture_db),
          "golden fixture db missing");
  require(std::filesystem::exists(ground_truth_path),
          "golden fixture ground_truth.json missing");

  // --- Ground truth (contract section 10 format). ---
  const JsonValue ground_truth =
      JsonParser(read_file(ground_truth_path)).parse();
  const JsonValue* span = find_member(ground_truth, "analysis_span");
  require(span != nullptr, "ground truth missing analysis_span");
  const std::int64_t expected_span_start = member_int64(*span, "start_ns");
  const std::int64_t expected_span_end = member_int64(*span, "end_ns");
  const std::int64_t expected_visible_idle_total =
      member_int64(ground_truth, "visible_idle_total_ns");
  const JsonValue* host_wait = find_member(ground_truth, "host_wait");
  require(host_wait != nullptr, "ground truth missing host_wait");
  require(member_bool(*host_wait, "present"),
          "ground truth expects host wait present");
  require(member_string(*host_wait, "api") == "aclrtSynchronizeStream",
          "ground truth host wait api mismatch");

  // --- 1. Host wait exists (host-side evidence from the fixture). ---
  const HostWaitEvidence host_wait_evidence = read_host_wait(fixture_db);
  require(host_wait_evidence.present,
          "fixture must contain aclrtSynchronizeStream host wait");
  require(host_wait_evidence.start_ns == member_int64(*host_wait, "start_ns") &&
              host_wait_evidence.end_ns == member_int64(*host_wait, "end_ns"),
          "fixture host wait interval differs from ground truth");
  require(host_wait_evidence.start_ns < expected_span_start &&
              host_wait_evidence.end_ns > expected_span_end,
          "host wait must span the whole analysis span");

  // --- 2. Device side: run the real analysis on the same fixture. ---
  const AscendSQLiteAdapter adapter(
      AscendSQLiteAdapterOptions{fixture_db.string(), "golden_fixture"});
  const NativeIr ir = adapter.load();
  const SemanticTaskRuleset ruleset =
      load_default_idle_evidence_semantic_ruleset();
  const SemanticTaskClassificationResult classification =
      classify_semantic_tasks(ir, ruleset);
  const ProductiveTimelineRunResult run =
      build_productive_timelines(ir, classification);

  require(run.status == AnalysisStatus::kOk,
          "fixture run status must be ok");
  require(run.devices.size() == 1, "fixture must produce one device timeline");
  const DeviceTimelineResult& timeline = run.devices[0];
  require(timeline.status == AnalysisStatus::kOk,
          "device timeline status must be ok");
  require(timeline.span_start_ns.has_value() &&
              timeline.span_end_ns.has_value(),
          "device timeline must report span boundaries");
  require(*timeline.span_start_ns == expected_span_start &&
              *timeline.span_end_ns == expected_span_end,
          "device timeline span differs from ground truth");

  // Visible idle must be zero: exactly one productive interval covering the
  // span, no visible_productive_idle interval anywhere.
  require(timeline.intervals.size() == 1,
          "fully productive span must yield exactly one interval");
  require(timeline.intervals[0].kind == DeviceIntervalKind::kProductiveActive &&
              timeline.intervals[0].start_ns == expected_span_start &&
              timeline.intervals[0].end_ns == expected_span_end,
          "the only interval must be productive_active covering the span");
  std::int64_t visible_idle_total = 0;
  for (const DeviceIntervalRow& row : timeline.intervals) {
    if (row.kind == DeviceIntervalKind::kVisibleProductiveIdle) {
      visible_idle_total += row.end_ns - row.start_ns;
    }
  }
  require(visible_idle_total == 0,
          "visible_productive_idle total must be zero");
  require(visible_idle_total == expected_visible_idle_total,
          "visible idle total differs from ground truth");

  // Coverage invariant (contract section 3.3): productive union plus gap
  // union covers the analysis span exactly.
  std::int64_t covered_ns = 0;
  for (const DeviceIntervalRow& row : timeline.intervals) {
    covered_ns += row.end_ns - row.start_ns;
  }
  require(covered_ns == *timeline.span_end_ns - *timeline.span_start_ns,
          "productive + gap must cover the span exactly");

  // Lineage: productive interval source keys must match ground truth.
  const JsonValue* intervals_member = find_member(ground_truth, "intervals");
  require(intervals_member != nullptr, "ground truth missing intervals");
  const auto* interval_array =
      std::get_if<JsonValue::Array>(&intervals_member->storage);
  require(interval_array != nullptr && interval_array->size() == 1,
          "ground truth must list exactly one interval");
  const JsonValue* expected_keys =
      find_member((*interval_array)[0], "expected_source_keys");
  require(expected_keys != nullptr, "ground truth interval missing source keys");
  const auto* key_array = std::get_if<JsonValue::Array>(&expected_keys->storage);
  require(key_array != nullptr, "expected_source_keys must be an array");
  std::set<std::string> expected_key_set;
  for (const JsonValue& key : *key_array) {
    const auto* key_string = std::get_if<std::string>(&key.storage);
    require(key_string != nullptr, "expected source key must be a string");
    expected_key_set.insert(*key_string);
  }
  std::set<std::string> actual_key_set;
  for (const ProductiveSourceLink& link : timeline.intervals[0].source_links) {
    actual_key_set.insert(source_key(ir, link));
  }
  require(actual_key_set == expected_key_set,
          "productive interval source lineage differs from ground truth");

  // --- 3. E3: per-stream observable state timelines over the same fixture.
  // The fixture's three adjacent compute tasks on stream 3 must yield three
  // adjacent running_compute intervals, NOT merged: adjacent same-state
  // segments with different source lineage stay separate (contract 3.3). ---
  const StreamStateRunResult stream_run =
      build_stream_state_timelines(ir, classification, run);
  require(stream_run.status == AnalysisStatus::kOk,
          "stream state run status must be ok");
  require(stream_run.stream_universe_size == 1,
          "one observed stream in the universe");
  require(stream_run.devices.size() == 1, "one stream state device result");
  const StreamStateDeviceResult& device_states = stream_run.devices[0];
  require(device_states.status == AnalysisStatus::kOk,
          "stream state device status must be ok");
  require(device_states.span_start_ns.has_value() &&
              device_states.span_end_ns.has_value() &&
              *device_states.span_start_ns == expected_span_start &&
              *device_states.span_end_ns == expected_span_end,
          "stream state span must equal the analysis span");
  require(device_states.timelines.size() == 1,
          "exactly one stream timeline");
  const StreamStateTimeline& stream_timeline = device_states.timelines[0];
  require(stream_timeline.stream_id == 3, "timeline belongs to stream 3");
  require(stream_timeline.intervals.size() == 3,
          "three adjacent intervals, not merged across lineage");
  for (std::size_t index = 0; index < stream_timeline.intervals.size();
       ++index) {
    const StreamStateInterval& interval = stream_timeline.intervals[index];
    require(interval.state == StreamState::kRunningCompute &&
                interval.start_ns ==
                    1000 + static_cast<std::int64_t>(index) * 1000 &&
                interval.end_ns ==
                    2000 + static_cast<std::int64_t>(index) * 1000 &&
                interval.source_links.size() == 1,
            "interval is running_compute with exactly one source");
    require(interval.source_links[0].kind ==
                StreamStateSourceLink::Kind::kTask &&
                source_key(ir, interval.source_links[0]) ==
                    "TASK:" + std::to_string(index + 1),
            "interval lineage is its own TASK row");
  }
  require(stream_timeline.diagnostics.empty(),
          "no stream state diagnostics on the fixture");
  std::int64_t stream_cursor = stream_timeline.span_start_ns;
  for (const StreamStateInterval& interval : stream_timeline.intervals) {
    require(interval.end_ns > interval.start_ns &&
                interval.start_ns == stream_cursor,
            "stream state intervals partition the span");
    stream_cursor = interval.end_ns;
  }
  require(stream_cursor == stream_timeline.span_end_ns,
          "stream state intervals cover the span exactly");

  // --- 4. E4: a host wait cannot fabricate a device idle explanation.
  // Collection is complete by fixture construction, but there is no gap to
  // explain, so the official explanation partition is empty. ---
  IdleExplanationOptions explanation_options;
  explanation_options.collection_status = CollectionStatus::kComplete;
  const IdleExplanationRunResult explanations = build_idle_explanations(
      run, stream_run, explanation_options);
  require(explanations.status == AnalysisStatus::kOk &&
              explanations.devices.size() == 1 &&
              explanations.devices[0].explanations.empty(),
          "host wait with zero visible idle produces no E4 explanation");

  // --- The counterexample, asserted as a unit: host wait exists AND
  // visible idle is zero. ---
  require(host_wait_evidence.present && visible_idle_total == 0,
          "counterexample failed: host wait present but visible idle not zero");
  std::cout << "PASS: idle-evidence golden fixture counterexample\n"
            << "  host wait: aclrtSynchronizeStream ["
            << host_wait_evidence.start_ns << ", "
            << host_wait_evidence.end_ns << ") present\n"
            << "  visible_productive_idle total: " << visible_idle_total
            << " ns\n"
            << "  productive interval: [" << timeline.intervals[0].start_ns
            << ", " << timeline.intervals[0].end_ns << ")\n"
            << "  verified: host wait exists but visible idle is zero\n";
  return 0;
}
