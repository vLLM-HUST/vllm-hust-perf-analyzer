// Executable contract matrix for the non-positive idle-evidence fixture
// classes required by contract section 10.  Every expected interval is read
// from the checked-in ground_truth.json; the test does not carry a second
// hard-coded oracle.

#include "traceloom/adapters/ascend_sqlite_adapter.h"
#include "traceloom/adapters/clock_marker_tsv.h"
#include "traceloom/analysis/host_api_rules.h"
#include "traceloom/analysis/idle_evidence_pipeline.h"
#include "traceloom/analysis/idle_evidence_semantic_rules.h"
#include "traceloom/testing/test_util.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

struct JsonValue {
  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue>;
  using Storage = std::variant<std::nullptr_t, bool, std::int64_t, double,
                               std::string, Array, Object>;
  Storage storage;
};

class JsonParser {
 public:
  explicit JsonParser(std::string text) : text_(std::move(text)) {}

  JsonValue parse() {
    JsonValue result = value();
    space();
    if (position_ != text_.size()) {
      throw std::invalid_argument("trailing JSON content");
    }
    return result;
  }

 private:
  JsonValue value() {
    space();
    if (position_ == text_.size()) {
      throw std::invalid_argument("unexpected JSON end");
    }
    const char ch = text_[position_];
    if (ch == '{') return JsonValue{object()};
    if (ch == '[') return JsonValue{array()};
    if (ch == '"') return JsonValue{string()};
    if (ch == 't') {
      literal("true");
      return JsonValue{true};
    }
    if (ch == 'f') {
      literal("false");
      return JsonValue{false};
    }
    if (ch == 'n') {
      literal("null");
      return JsonValue{nullptr};
    }
    return number();
  }

  JsonValue::Object object() {
    expect('{');
    JsonValue::Object result;
    space();
    if (peek('}')) {
      ++position_;
      return result;
    }
    while (true) {
      space();
      const std::string key = string();
      expect(':');
      result.emplace(key, value());
      space();
      if (peek('}')) {
        ++position_;
        return result;
      }
      expect(',');
    }
  }

  JsonValue::Array array() {
    expect('[');
    JsonValue::Array result;
    space();
    if (peek(']')) {
      ++position_;
      return result;
    }
    while (true) {
      result.push_back(value());
      space();
      if (peek(']')) {
        ++position_;
        return result;
      }
      expect(',');
    }
  }

  std::string string() {
    expect('"');
    std::string result;
    while (position_ < text_.size()) {
      const char ch = text_[position_++];
      if (ch == '"') return result;
      if (ch != '\\') {
        result.push_back(ch);
        continue;
      }
      if (position_ == text_.size()) {
        throw std::invalid_argument("unterminated JSON escape");
      }
      const char escaped = text_[position_++];
      switch (escaped) {
        case '"': case '\\': case '/': result.push_back(escaped); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        default: throw std::invalid_argument("unsupported JSON escape");
      }
    }
    throw std::invalid_argument("unterminated JSON string");
  }

  JsonValue number() {
    const std::size_t begin = position_;
    if (peek('-')) ++position_;
    while (position_ < text_.size() &&
           std::isdigit(static_cast<unsigned char>(text_[position_]))) {
      ++position_;
    }
    bool fractional = false;
    if (peek('.')) {
      fractional = true;
      ++position_;
      while (position_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[position_]))) {
        ++position_;
      }
    }
    const std::string token = text_.substr(begin, position_ - begin);
    if (token.empty() || token == "-") {
      throw std::invalid_argument("invalid JSON number");
    }
    return fractional ? JsonValue{std::stod(token)}
                      : JsonValue{std::stoll(token)};
  }

  void literal(const char* expected) {
    const std::string token(expected);
    if (text_.compare(position_, token.size(), token) != 0) {
      throw std::invalid_argument("invalid JSON literal");
    }
    position_ += token.size();
  }
  void expect(char expected) {
    space();
    if (!peek(expected)) throw std::invalid_argument("unexpected JSON token");
    ++position_;
  }
  bool peek(char ch) const {
    return position_ < text_.size() && text_[position_] == ch;
  }
  void space() {
    while (position_ < text_.size() &&
           std::isspace(static_cast<unsigned char>(text_[position_]))) {
      ++position_;
    }
  }

  std::string text_;
  std::size_t position_ = 0;
};

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::invalid_argument("cannot open " + path.string());
  std::ostringstream result;
  result << input.rdbuf();
  return result.str();
}

const JsonValue& member(const JsonValue& value, const std::string& key) {
  const auto* object = std::get_if<JsonValue::Object>(&value.storage);
  if (object == nullptr || object->count(key) == 0) {
    throw std::invalid_argument("missing JSON member: " + key);
  }
  return object->at(key);
}

std::string string_member(const JsonValue& value, const std::string& key) {
  const auto* result = std::get_if<std::string>(&member(value, key).storage);
  if (result == nullptr) throw std::invalid_argument(key + " is not a string");
  return *result;
}

std::int64_t integer_member(const JsonValue& value, const std::string& key) {
  const auto& storage = member(value, key).storage;
  if (const auto* result = std::get_if<std::int64_t>(&storage)) return *result;
  throw std::invalid_argument(key + " is not an integer");
}

const JsonValue::Array& array_member(const JsonValue& value,
                                    const std::string& key) {
  const auto* result =
      std::get_if<JsonValue::Array>(&member(value, key).storage);
  if (result == nullptr) throw std::invalid_argument(key + " is not an array");
  return *result;
}

std::string nullable_string_member(const JsonValue& value,
                                   const std::string& key) {
  const auto& storage = member(value, key).storage;
  if (std::holds_alternative<std::nullptr_t>(storage)) return {};
  if (const auto* result = std::get_if<std::string>(&storage)) return *result;
  throw std::invalid_argument(key + " is neither a string nor null");
}

std::set<std::string> string_set_member(const JsonValue& value,
                                        const std::string& key) {
  std::set<std::string> result;
  for (const JsonValue& item : array_member(value, key)) {
    const auto* text = std::get_if<std::string>(&item.storage);
    if (text == nullptr) throw std::invalid_argument(key + " item is not text");
    result.insert(*text);
  }
  return result;
}

traceloom::CollectionStatus parse_collection_status(const std::string& value) {
  if (value == "complete") return traceloom::CollectionStatus::kComplete;
  if (value == "incomplete") return traceloom::CollectionStatus::kIncomplete;
  if (value == "unknown") return traceloom::CollectionStatus::kUnknown;
  if (value == "invalid") return traceloom::CollectionStatus::kInvalid;
  throw std::invalid_argument("unknown collection_status: " + value);
}

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

std::string source_key(const traceloom::NativeIr& ir,
                       const traceloom::HostEvidenceSourceLink& link) {
  const traceloom::HostApiEventRow& api =
      ir.host_api_events.row(link.host_api_event_id);
  const traceloom::SourceRefRow& source = ir.source_refs.row(api.source_ref_id);
  return source.table_name + ":" + std::to_string(api.source_row_id);
}

struct PartitionRow {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::string interval_kind;
  std::string category;
  std::string evidence_level;
  std::string evidence_relation;
  std::string alignment_status;
  std::set<std::string> source_keys;
};

void require_text(bool condition, const std::string& message) {
  traceloom::testing::require(condition, message.c_str());
}

PartitionRow expected_row(const JsonValue& value) {
  return PartitionRow{
      integer_member(value, "start_ns"),
      integer_member(value, "end_ns"),
      string_member(value, "interval_kind"),
      nullable_string_member(value, "explanation_category"),
      nullable_string_member(value, "evidence_level"),
      nullable_string_member(value, "evidence_relation"),
      string_member(value, "alignment_status"),
      string_set_member(value, "expected_source_keys")};
}

std::vector<PartitionRow> actual_partition(
    const traceloom::NativeIr& ir,
    const traceloom::IdleEvidencePipelineResult& pipeline) {
  using namespace traceloom;
  std::vector<PartitionRow> result;
  for (const DeviceTimelineResult& device : pipeline.productive_timeline.devices) {
    for (const DeviceIntervalRow& interval : device.intervals) {
      if (interval.kind != DeviceIntervalKind::kProductiveActive) continue;
      PartitionRow row;
      row.start_ns = interval.start_ns;
      row.end_ns = interval.end_ns;
      row.interval_kind = "productive_active";
      row.alignment_status = "not_required";
      for (const ProductiveSourceLink& link : interval.source_links) {
        row.source_keys.insert(source_key(ir, link));
      }
      result.push_back(std::move(row));
    }
  }
  for (const IdleExplanationDeviceResult& device :
       pipeline.idle_explanations.devices) {
    for (const IdleExplanationRow& explanation : device.explanations) {
      PartitionRow row;
      row.start_ns = explanation.start_ns;
      row.end_ns = explanation.end_ns;
      row.interval_kind = "visible_productive_idle";
      row.category = idle_explanation_category_name(explanation.category);
      row.evidence_level = idle_evidence_level_name(explanation.evidence_level);
      row.evidence_relation =
          idle_evidence_relation_name(explanation.evidence_relation);
      row.alignment_status = explanation.alignment_status;
      for (const IdleExplanationSourceLink& link : explanation.source_links) {
        row.source_keys.insert(source_key(ir, link.source));
      }
      for (const HostEvidenceSourceLink& link :
           explanation.host_source_links) {
        row.source_keys.insert(source_key(ir, link));
      }
      result.push_back(std::move(row));
    }
  }
  std::sort(result.begin(), result.end(), [](const PartitionRow& lhs,
                                             const PartitionRow& rhs) {
    return std::tie(lhs.start_ns, lhs.end_ns, lhs.interval_kind) <
           std::tie(rhs.start_ns, rhs.end_ns, rhs.interval_kind);
  });
  return result;
}

void require_equal(const PartitionRow& actual,
                   const PartitionRow& expected,
                   const std::string& fixture,
                   std::size_t index) {
  using traceloom::testing::require;
  const std::string label = fixture + " interval " + std::to_string(index);
  require_text(actual.start_ns == expected.start_ns &&
                   actual.end_ns == expected.end_ns,
               label + " boundary differs from ground truth");
  require_text(actual.interval_kind == expected.interval_kind,
               label + " kind differs from ground truth");
  require_text(actual.category == expected.category,
               label + " category differs from ground truth");
  require_text(actual.evidence_level == expected.evidence_level,
               label + " evidence level differs from ground truth");
  require_text(actual.evidence_relation == expected.evidence_relation,
               label + " evidence relation differs from ground truth");
  require_text(actual.alignment_status == expected.alignment_status,
               label + " alignment status differs from ground truth");
  require_text(actual.source_keys == expected.source_keys,
               label + " source lineage differs from ground truth");
}

void run_fixture(const std::filesystem::path& root,
                 const std::string& fixture_name) {
  using namespace traceloom;
  using traceloom::testing::require;
  const std::filesystem::path fixture = root / fixture_name;
  const std::filesystem::path db_path = fixture / (fixture_name + ".db");
  const JsonValue truth =
      JsonParser(read_file(fixture / "ground_truth.json")).parse();
  require_text(string_member(truth, "fixture_class") == fixture_name,
               fixture_name + " class must match its directory");

  const JsonValue& span = member(truth, "analysis_span");
  const std::int64_t span_start = integer_member(span, "start_ns");
  const std::int64_t span_end = integer_member(span, "end_ns");
  const std::int64_t expected_idle =
      integer_member(truth, "visible_idle_total_ns");

  const AscendSQLiteAdapter adapter(
      AscendSQLiteAdapterOptions{db_path.string(), "golden_fixture_matrix"});
  NativeIr ir = adapter.load();
  const std::filesystem::path marker_path = fixture / "clock_markers.tsv";
  if (std::filesystem::exists(marker_path)) {
    const ClockMarkerTsvLoadResult loaded =
        load_clock_marker_tsv(marker_path.string(), ir);
    require_text(loaded.marker_count == 11 && loaded.rejected_marker_count == 0,
                 fixture_name + " marker input must be complete");
  }

  const SemanticTaskRuleset semantic_rules =
      load_default_idle_evidence_semantic_ruleset();
  const HostApiRuleset host_rules = load_default_idle_evidence_host_api_ruleset();
  IdleEvidencePipelineOptions options;
  options.productive_timeline.explicit_span_start_ns = span_start;
  options.productive_timeline.explicit_span_end_ns = span_end;
  options.idle_explanation.collection_status = parse_collection_status(
      string_member(truth, "collection_status"));
  options.host_api_rules = &host_rules;
  options.clock_alignment.synthetic_fixture =
      std::filesystem::exists(marker_path);
  const IdleEvidencePipelineResult pipeline =
      run_idle_evidence_pipeline(ir, semantic_rules, options);
  require_text(pipeline.productive_timeline.status == AnalysisStatus::kOk &&
                   pipeline.stream_states.status == AnalysisStatus::kOk &&
                   pipeline.idle_explanations.status == AnalysisStatus::kOk,
               fixture_name + " must be an analysis_status=ok fixture");

  std::int64_t actual_idle = 0;
  for (const DeviceTimelineResult& device :
       pipeline.productive_timeline.devices) {
    for (const DeviceIntervalRow& interval : device.intervals) {
      if (interval.kind == DeviceIntervalKind::kVisibleProductiveIdle) {
        actual_idle += interval.end_ns - interval.start_ns;
      }
    }
  }
  require_text(actual_idle == expected_idle,
               fixture_name +
                   " visible idle total differs from ground truth");

  const JsonValue::Array& truth_rows = array_member(truth, "intervals");
  const std::vector<PartitionRow> actual = actual_partition(ir, pipeline);
  require_text(actual.size() == truth_rows.size(),
               fixture_name +
                   " final partition row count differs from ground truth");
  std::int64_t cursor = span_start;
  std::int64_t duration = 0;
  for (std::size_t index = 0; index < actual.size(); ++index) {
    const PartitionRow expected = expected_row(truth_rows[index]);
    require_equal(actual[index], expected, fixture_name, index);
    require_text(actual[index].start_ns == cursor &&
                     actual[index].end_ns > actual[index].start_ns,
                 fixture_name +
                     " final partition must be adjacent and positive");
    duration += actual[index].end_ns - actual[index].start_ns;
    cursor = actual[index].end_ns;
  }
  require_text(cursor == span_end && duration == span_end - span_start,
               fixture_name +
                   " final partition must conserve the analysis span");

  if (fixture_name == "clock_drift") {
    require(pipeline.clock_alignment.models.size() == 1,
            "clock_drift must produce one model");
    const ClockModel& model = pipeline.clock_alignment.models.front();
    require(model.alignment_status == AlignmentStatus::kSyntheticOnly &&
                std::fabs(model.scale - 2.0L) < 1e-12L &&
                model.epsilon_ns == 5,
            "clock_drift must retain synthetic-only affine calibration and epsilon");
    require(pipeline.host_correlation.evidence_intervals.size() == 1 &&
                pipeline.host_correlation.candidates.empty(),
            "clock_drift must yield exactly one robust host-sync interval");
  }

  std::cout << "PASS: " << fixture_name << " (" << actual.size()
            << " rows, " << actual_idle << " idle ns)\n";
}

}  // namespace

int main() {
  const std::filesystem::path root(TRACELOOM_GOLDEN_FIXTURE_ROOT);
  run_fixture(root, "adjacent_overlap");
  run_fixture(root, "event_loss");
  run_fixture(root, "clock_drift");
  return 0;
}
