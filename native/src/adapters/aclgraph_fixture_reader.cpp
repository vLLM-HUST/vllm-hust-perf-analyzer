#include "traceloom/adapters/aclgraph_fixture_reader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace traceloom {
namespace {

struct JsonValue {
  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue>;
  using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array,
                               Object>;

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
      return JsonValue{parse_number()};
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

  double parse_number() {
    const std::size_t begin = pos_;
    if (peek('-')) {
      ++pos_;
    }
    while (pos_ < text_.size() &&
           std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
      ++pos_;
    }
    if (peek('.')) {
      ++pos_;
      while (pos_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
        ++pos_;
      }
    }
    return std::stod(text_.substr(begin, pos_ - begin));
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

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("failed to open ACLGraph fixture: " + path);
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

const JsonValue::Object& as_object(const JsonValue& value,
                                   const std::string& context) {
  if (!std::holds_alternative<JsonValue::Object>(value.storage)) {
    throw std::invalid_argument(context + " must be a JSON object");
  }
  return std::get<JsonValue::Object>(value.storage);
}

const JsonValue::Array& as_array(const JsonValue& value,
                                 const std::string& context) {
  if (!std::holds_alternative<JsonValue::Array>(value.storage)) {
    throw std::invalid_argument(context + " must be a JSON array");
  }
  return std::get<JsonValue::Array>(value.storage);
}

const JsonValue& required_field(const JsonValue::Object& object,
                                const std::string& key,
                                const std::string& context) {
  const auto iter = object.find(key);
  if (iter == object.end()) {
    throw std::invalid_argument(context + " missing required field " + key);
  }
  return iter->second;
}

const JsonValue* optional_field(const JsonValue::Object& object,
                                const std::string& key) {
  const auto iter = object.find(key);
  if (iter == object.end()) {
    return nullptr;
  }
  return &iter->second;
}

std::string as_string(const JsonValue& value, const std::string& context) {
  if (!std::holds_alternative<std::string>(value.storage)) {
    throw std::invalid_argument(context + " must be a JSON string");
  }
  return std::get<std::string>(value.storage);
}

bool as_bool(const JsonValue& value, const std::string& context) {
  if (!std::holds_alternative<bool>(value.storage)) {
    throw std::invalid_argument(context + " must be a JSON bool");
  }
  return std::get<bool>(value.storage);
}

std::int64_t as_i64(const JsonValue& value, const std::string& context) {
  if (!std::holds_alternative<double>(value.storage)) {
    throw std::invalid_argument(context + " must be a JSON number");
  }
  const double number = std::get<double>(value.storage);
  const auto integer = static_cast<std::int64_t>(number);
  if (static_cast<double>(integer) != number) {
    throw std::invalid_argument(context + " must be an integer");
  }
  return integer;
}

std::uint32_t as_u32(const JsonValue& value, const std::string& context) {
  const std::int64_t integer = as_i64(value, context);
  if (integer < 0) {
    throw std::invalid_argument(context + " must be non-negative");
  }
  return static_cast<std::uint32_t>(integer);
}

std::string optional_string(const JsonValue::Object& object,
                            const std::string& key,
                            const std::string& context) {
  const JsonValue* value = optional_field(object, key);
  return value == nullptr ? std::string() : as_string(*value, context);
}

std::uint32_t optional_u32(const JsonValue::Object& object,
                           const std::string& key,
                           const std::string& context) {
  const JsonValue* value = optional_field(object, key);
  return value == nullptr ? 0 : as_u32(*value, context);
}

std::int64_t optional_i64(const JsonValue::Object& object,
                          const std::string& key,
                          const std::string& context) {
  const JsonValue* value = optional_field(object, key);
  return value == nullptr ? 0 : as_i64(*value, context);
}

bool optional_bool(const JsonValue::Object& object,
                   const std::string& key,
                   const std::string& context) {
  const JsonValue* value = optional_field(object, key);
  return value != nullptr && as_bool(*value, context);
}

std::vector<std::string> optional_string_array(const JsonValue::Object& object,
                                               const std::string& key,
                                               const std::string& context) {
  const JsonValue* value = optional_field(object, key);
  if (value == nullptr) {
    return {};
  }
  std::vector<std::string> out;
  for (const JsonValue& item : as_array(*value, context)) {
    out.push_back(as_string(item, context + " item"));
  }
  return out;
}

std::vector<std::uint32_t> optional_u32_array(const JsonValue::Object& object,
                                              const std::string& key,
                                              const std::string& context) {
  const JsonValue* value = optional_field(object, key);
  if (value == nullptr) {
    return {};
  }
  std::vector<std::uint32_t> out;
  for (const JsonValue& item : as_array(*value, context)) {
    out.push_back(as_u32(item, context + " item"));
  }
  return out;
}

std::vector<std::int64_t> optional_i64_array(const JsonValue::Object& object,
                                             const std::string& key,
                                             const std::string& context) {
  const JsonValue* value = optional_field(object, key);
  if (value == nullptr) {
    return {};
  }
  std::vector<std::int64_t> out;
  for (const JsonValue& item : as_array(*value, context)) {
    out.push_back(as_i64(item, context + " item"));
  }
  return out;
}

std::map<std::string, std::uint32_t> optional_diagnostic_counts(
    const JsonValue::Object& object) {
  const JsonValue* value = optional_field(object, "diagnostic_codes");
  if (value == nullptr) {
    return {};
  }
  std::map<std::string, std::uint32_t> out;
  for (const auto& entry : as_object(*value, "golden diagnostic_codes")) {
    out.emplace(entry.first,
                as_u32(entry.second, "golden diagnostic count " + entry.first));
  }
  return out;
}

std::map<std::string, std::string> optional_split_confidence(
    const JsonValue::Object& object) {
  const JsonValue* value = optional_field(object, "split_confidence");
  if (value == nullptr) {
    return {};
  }
  std::map<std::string, std::string> out;
  for (const auto& entry : as_object(*value, "golden split_confidence")) {
    out.emplace(entry.first,
                as_string(entry.second, "golden split confidence value"));
  }
  return out;
}

template <typename Row, typename Parser>
std::vector<Row> parse_row_array(const JsonValue::Object& assets,
                                 const std::string& key,
                                 Parser parser) {
  const JsonValue::Array& values =
      as_array(required_field(assets, key, "ACLGraph assets"),
               "ACLGraph assets " + key);
  std::vector<Row> rows;
  rows.reserve(values.size());
  for (const JsonValue& value : values) {
    rows.push_back(parser(as_object(value, "ACLGraph " + key + " row")));
  }
  return rows;
}

AclGraphFixtureGolden parse_golden(const JsonValue::Object& golden) {
  AclGraphFixtureGolden out;
  out.capture_slot_count =
      optional_u32(golden, "capture_slot_count", "golden capture_slot_count");
  out.capture_group_count =
      optional_u32(golden, "capture_group_count", "golden capture_group_count");
  out.capture_group_size =
      optional_u32(golden, "capture_group_size", "golden capture_group_size");
  out.capture_dictionary_count = optional_u32(
      golden, "capture_dictionary_count", "golden capture_dictionary_count");
  out.dictionary_sequence =
      optional_string(golden, "dictionary_sequence", "golden dictionary");
  out.replay_activity_count = optional_u32(
      golden, "replay_activity_count", "golden replay_activity_count");
  out.replay_unit_count =
      optional_u32(golden, "replay_unit_count", "golden replay_unit_count");
  out.boundary_effective_unit_count =
      optional_u32(golden, "boundary_effective_unit_count",
                   "golden boundary_effective_unit_count");
  out.hlt_anchor_count =
      optional_u32(golden, "hlt_anchor_count", "golden hlt_anchor_count");
  out.flat_hlt_sequence =
      optional_string(golden, "flat_hlt_sequence", "golden flat_hlt_sequence");
  out.normal_flat_hlt_sequence = optional_string(
      golden, "normal_flat_hlt_sequence", "golden normal_flat_hlt_sequence");
  out.launch_anchor_count =
      optional_u32(golden, "launch_anchor_count", "golden launch_anchor_count");
  out.launch_boundary_used_as_anchor =
      optional_bool(golden, "launch_boundary_used_as_anchor",
                    "golden launch_boundary_used_as_anchor");
  out.unique_launch_activity_ids_in_anchors =
      optional_u32(golden, "unique_launch_activity_ids_in_anchors",
                   "golden unique_launch_activity_ids_in_anchors");
  out.unique_replay_unit_ids_in_anchors =
      optional_u32(golden, "unique_replay_unit_ids_in_anchors",
                   "golden unique_replay_unit_ids_in_anchors");
  out.replay_tiling_subslot_count =
      optional_u32(golden, "replay_tiling_subslot_count",
                   "golden replay_tiling_subslot_count");
  out.replay_tiling_matched_count =
      optional_u32(golden, "replay_tiling_matched_count",
                   "golden replay_tiling_matched_count");
  out.replay_tiling_unmatched_count =
      optional_u32(golden, "replay_tiling_unmatched_count",
                   "golden replay_tiling_unmatched_count");
  out.layer_unique_match_signature_count =
      optional_u32(golden, "layer_unique_match_signature_count",
                   "golden layer_unique_match_signature_count");
  out.split_confidence = optional_split_confidence(golden);
  out.diagnostic_codes = optional_diagnostic_counts(golden);
  return out;
}

}  // namespace

AclGraphSemanticFixture load_aclgraph_semantic_fixture(
    const std::string& path) {
  JsonParser parser(read_file(path));
  const JsonValue root = parser.parse();
  const JsonValue::Object& object = as_object(root, "ACLGraph fixture root");

  const std::string schema_version =
      as_string(required_field(object, "schema_version", "ACLGraph fixture"),
                "ACLGraph fixture schema_version");
  if (schema_version != "aclgraph-fixture-v1") {
    throw std::invalid_argument("unsupported ACLGraph fixture schema: " +
                                schema_version);
  }

  AclGraphSemanticFixture fixture;
  fixture.fixture_id =
      as_string(required_field(object, "fixture_id", "ACLGraph fixture"),
                "ACLGraph fixture fixture_id");
  fixture.description =
      optional_string(object, "description", "ACLGraph fixture description");

  const JsonValue::Object& assets =
      as_object(required_field(object, "assets", "ACLGraph fixture"),
                "ACLGraph assets");

  fixture.capture_slots =
      parse_row_array<AclGraphCaptureSlotFixtureRow>(
          assets, "capture_slots", [](const JsonValue::Object& row) {
            AclGraphCaptureSlotFixtureRow out;
            out.capture_slot_id =
                as_string(required_field(row, "capture_slot_id",
                                         "capture slot"),
                          "capture slot id");
            out.capture_slot_idx =
                optional_u32(row, "capture_slot_idx", "capture slot idx");
            out.capture_group_idx =
                optional_u32(row, "capture_group_idx", "capture group idx");
            out.capture_group_size =
                optional_u32(row, "capture_group_size", "capture group size");
            out.capture_slot_in_group = optional_u32(
                row, "capture_slot_in_group", "capture slot in group");
            out.slot_kind =
                optional_string(row, "slot_kind", "capture slot kind");
            out.slot_symbol =
                optional_string(row, "slot_symbol", "capture slot symbol");
            out.start_ns = optional_i64(row, "start_ns", "capture start_ns");
            out.end_ns = optional_i64(row, "end_ns", "capture end_ns");
            out.body_match_signature =
                optional_string(row, "body_match_signature",
                                "capture body_match_signature");
            return out;
          });

  fixture.capture_dictionary =
      parse_row_array<AclGraphCaptureDictionaryFixtureRow>(
          assets, "capture_dictionary", [](const JsonValue::Object& row) {
            AclGraphCaptureDictionaryFixtureRow out;
            out.capture_dictionary_id =
                as_string(required_field(row, "capture_dictionary_id",
                                         "capture dictionary"),
                          "capture dictionary id");
            out.dictionary_idx =
                optional_u32(row, "dictionary_idx", "dictionary idx");
            out.slot_kind =
                optional_string(row, "slot_kind", "dictionary slot kind");
            out.slot_symbol =
                optional_string(row, "slot_symbol", "dictionary slot symbol");
            out.capture_slot_ids = optional_string_array(
                row, "capture_slot_ids", "dictionary capture slot ids");
            out.capture_slot_count =
                optional_u32(row, "capture_slot_count",
                             "dictionary capture_slot_count");
            out.unique_match_signature_count =
                optional_u32(row, "unique_match_signature_count",
                             "dictionary unique_match_signature_count");
            out.variation_summary =
                optional_string(row, "variation_summary",
                                "dictionary variation_summary");
            return out;
          });

  fixture.replay_activities =
      parse_row_array<AclGraphReplayActivityFixtureRow>(
          assets, "replay_activities", [](const JsonValue::Object& row) {
            AclGraphReplayActivityFixtureRow out;
            out.replay_activity_id =
                as_string(required_field(row, "replay_activity_id",
                                         "replay activity"),
                          "replay activity id");
            out.activity_idx =
                optional_u32(row, "activity_idx", "activity idx");
            out.start_ns = optional_i64(row, "start_ns", "activity start_ns");
            out.end_ns = optional_i64(row, "end_ns", "activity end_ns");
            out.stream_ids =
                optional_u32_array(row, "stream_ids", "activity stream_ids");
            out.raw_child_task_count =
                optional_u32(row, "raw_child_task_count",
                             "activity raw_child_task_count");
            return out;
          });

  fixture.replay_unit_boundaries =
      parse_row_array<AclGraphReplayUnitBoundaryFixtureRow>(
          assets, "replay_unit_boundaries", [](const JsonValue::Object& row) {
            AclGraphReplayUnitBoundaryFixtureRow out;
            out.boundary_set_id =
                as_string(required_field(row, "boundary_set_id",
                                         "replay unit boundary"),
                          "boundary set id");
            out.replay_activity_id =
                as_string(required_field(row, "replay_activity_id",
                                         "replay unit boundary"),
                          "boundary replay activity id");
            out.expected_unit_count =
                optional_u32(row, "expected_unit_count",
                             "boundary expected_unit_count");
            out.effective_unit_count =
                optional_u32(row, "effective_unit_count",
                             "boundary effective_unit_count");
            out.unit_source =
                optional_string(row, "unit_source", "boundary unit_source");
            out.split_source =
                optional_string(row, "split_source", "boundary split_source");
            out.confidence =
                optional_string(row, "confidence", "boundary confidence");
            out.boundary_ns =
                optional_i64_array(row, "boundary_ns", "boundary_ns");
            return out;
          });

  fixture.replay_units =
      parse_row_array<AclGraphReplayUnitFixtureRow>(
          assets, "replay_units", [](const JsonValue::Object& row) {
            AclGraphReplayUnitFixtureRow out;
            out.replay_unit_id =
                as_string(required_field(row, "replay_unit_id",
                                         "replay unit"),
                          "replay unit id");
            out.replay_activity_id =
                as_string(required_field(row, "replay_activity_id",
                                         "replay unit"),
                          "replay unit activity id");
            out.boundary_set_id =
                optional_string(row, "boundary_set_id", "unit boundary_set_id");
            out.unit_idx_global =
                optional_u32(row, "unit_idx_global", "unit idx global");
            out.unit_idx_in_activity =
                optional_u32(row, "unit_idx_in_activity",
                             "unit idx in activity");
            out.unit_count_in_activity =
                optional_u32(row, "unit_count_in_activity",
                             "unit count in activity");
            out.start_ns = optional_i64(row, "start_ns", "unit start_ns");
            out.end_ns = optional_i64(row, "end_ns", "unit end_ns");
            return out;
          });

  fixture.replay_tilings =
      parse_row_array<AclGraphReplayTilingFixtureRow>(
          assets, "replay_tilings", [](const JsonValue::Object& row) {
            AclGraphReplayTilingFixtureRow out;
            out.replay_tiling_id =
                as_string(required_field(row, "replay_tiling_id",
                                         "replay tiling"),
                          "replay tiling id");
            out.replay_unit_id =
                as_string(required_field(row, "replay_unit_id",
                                         "replay tiling"),
                          "replay tiling unit id");
            out.policy = optional_string(row, "policy", "tiling policy");
            out.subslot_count =
                optional_u32(row, "subslot_count", "tiling subslot_count");
            out.sequence = optional_string(row, "sequence", "tiling sequence");
            out.matched_count =
                optional_u32(row, "matched_count", "tiling matched_count");
            out.unmatched_count =
                optional_u32(row, "unmatched_count", "tiling unmatched_count");
            out.coverage = optional_string(row, "coverage", "tiling coverage");
            out.top_mismatches =
                optional_string(row, "top_mismatches", "tiling mismatches");
            return out;
          });

  fixture.replay_subslots =
      parse_row_array<AclGraphReplaySubslotFixtureRow>(
          assets, "replay_subslots", [](const JsonValue::Object& row) {
            AclGraphReplaySubslotFixtureRow out;
            out.subslot_id =
                as_string(required_field(row, "subslot_id",
                                         "replay subslot"),
                          "replay subslot id");
            out.replay_tiling_id =
                as_string(required_field(row, "replay_tiling_id",
                                         "replay subslot"),
                          "replay subslot tiling id");
            out.subslot_idx =
                optional_u32(row, "subslot_idx", "subslot idx");
            out.slot_kind =
                optional_string(row, "slot_kind", "subslot slot_kind");
            out.slot_symbol =
                optional_string(row, "slot_symbol", "subslot slot_symbol");
            out.matched = optional_bool(row, "matched", "subslot matched");
            out.start_ns = optional_i64(row, "start_ns", "subslot start_ns");
            out.end_ns = optional_i64(row, "end_ns", "subslot end_ns");
            out.stream_id = optional_u32(row, "stream_id", "subslot stream_id");
            out.raw_child_task_count =
                optional_u32(row, "raw_child_task_count",
                             "subslot raw_child_task_count");
            out.raw_top_ops =
                optional_string(row, "raw_top_ops", "subslot raw_top_ops");
            out.body_match_signature =
                optional_string(row, "body_match_signature",
                                "subslot body_match_signature");
            return out;
          });

  fixture.hlt_anchor_seeds =
      parse_row_array<AclGraphHltAnchorSeedFixtureRow>(
          assets, "hlt_anchor_seeds", [](const JsonValue::Object& row) {
            AclGraphHltAnchorSeedFixtureRow out;
            out.anchor_seed_id =
                as_string(required_field(row, "anchor_seed_id",
                                         "HLT anchor seed"),
                          "HLT anchor seed id");
            out.replay_unit_id =
                as_string(required_field(row, "replay_unit_id",
                                         "HLT anchor seed"),
                          "HLT anchor seed replay_unit_id");
            out.subslot_id =
                optional_string(row, "subslot_id", "HLT anchor subslot_id");
            out.launch_activity_id =
                optional_string(row, "launch_activity_id",
                                "HLT anchor launch_activity_id");
            out.symbol = as_string(
                required_field(row, "symbol", "HLT anchor seed"),
                "HLT anchor seed symbol");
            out.slot_symbol =
                optional_string(row, "slot_symbol", "HLT anchor slot_symbol");
            out.semantic_role =
                optional_string(row, "semantic_role", "HLT semantic_role");
            out.start_ns = optional_i64(row, "start_ns", "HLT start_ns");
            out.end_ns = optional_i64(row, "end_ns", "HLT end_ns");
            out.raw_child_task_count =
                optional_u32(row, "raw_child_task_count",
                             "HLT raw_child_task_count");
            out.raw_top_ops =
                optional_string(row, "raw_top_ops", "HLT raw_top_ops");
            out.body_match_signature =
                optional_string(row, "body_match_signature",
                                "HLT body_match_signature");
            return out;
          });

  fixture.golden =
      parse_golden(as_object(required_field(object, "golden",
                                           "ACLGraph fixture"),
                             "ACLGraph golden"));
  return fixture;
}

std::string flat_hlt_anchor_sequence(const AclGraphSemanticFixture& fixture) {
  std::string out;
  for (const AclGraphHltAnchorSeedFixtureRow& seed : fixture.hlt_anchor_seeds) {
    if (!out.empty()) {
      out += " ";
    }
    out += seed.symbol;
  }
  return out;
}

std::map<std::string, std::uint32_t> derive_aclgraph_diagnostic_counts(
    const AclGraphSemanticFixture& fixture) {
  std::map<std::string, std::uint32_t> counts;

  for (const AclGraphCaptureDictionaryFixtureRow& row :
       fixture.capture_dictionary) {
    if (row.unique_match_signature_count > 1) {
      counts["capture_dictionary_variation"] += 1;
    }
  }
  for (const AclGraphReplayTilingFixtureRow& row : fixture.replay_tilings) {
    if (row.unmatched_count > 0) {
      counts["replay_tiling_partial_coverage"] += 1;
    }
  }
  for (const AclGraphReplayUnitBoundaryFixtureRow& row :
       fixture.replay_unit_boundaries) {
    if (row.split_source == "unsplit" || row.confidence == "failed") {
      counts["replay_unit_unsplit"] += 1;
    }
  }

  return counts;
}

}  // namespace traceloom
