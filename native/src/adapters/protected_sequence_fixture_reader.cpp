#include "traceloom/adapters/protected_sequence_fixture_reader.h"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
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

std::size_t as_size(const JsonValue& value, const std::string& context) {
  const std::int64_t integer = as_i64(value, context);
  if (integer < 0) {
    throw std::invalid_argument(context + " must be non-negative");
  }
  return static_cast<std::size_t>(integer);
}

ProtectedIntervalKind parse_interval_kind(const std::string& kind) {
  if (kind == "replay_unit") {
    return ProtectedIntervalKind::kGraphReplayUnit;
  }
  if (kind == "unknown") {
    return ProtectedIntervalKind::kUnknown;
  }
  if (kind == "user_window") {
    return ProtectedIntervalKind::kUserWindow;
  }
  throw std::invalid_argument("unsupported protected interval kind: " + kind);
}

BoundaryPolicy parse_boundary_policy(const JsonValue::Object& interval) {
  const std::string boundary_type =
      as_string(required_field(interval, "boundary_type", "protected interval"),
                "protected interval boundary_type");
  if (boundary_type == "ambiguous") {
    return BoundaryPolicy::kBlockAnyOverlap;
  }
  if (boundary_type != "hard") {
    throw std::invalid_argument("unsupported boundary_type: " + boundary_type);
  }
  const JsonValue* allow_enclosing_value =
      optional_field(interval, "allow_enclosing");
  const bool allow_enclosing =
      allow_enclosing_value != nullptr &&
      as_bool(*allow_enclosing_value, "protected interval allow_enclosing");
  return allow_enclosing ? BoundaryPolicy::kAllowEnclosing
                         : BoundaryPolicy::kNoCross;
}

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("failed to open fixture: " + path);
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

ProtectedSequenceFixture load_protected_sequence_fixture(
    const std::string& path) {
  JsonParser parser(read_file(path));
  const JsonValue root = parser.parse();
  const JsonValue::Object& object = as_object(root, "fixture root");

  const std::string schema_version =
      as_string(required_field(object, "schema_version", "fixture root"),
                "fixture schema_version");
  if (schema_version != "protected-sequence-fixture-v1") {
    throw std::invalid_argument(
        "unsupported protected sequence fixture schema: " + schema_version);
  }

  ProtectedSequenceFixture fixture;
  fixture.fixture_id =
      as_string(required_field(object, "fixture_id", "fixture root"),
                "fixture fixture_id");
  fixture.input.source_kind = "protected_sequence_fixture";
  fixture.input.source_path = path;

  const JsonValue::Array& tokens =
      as_array(required_field(object, "tokens", "fixture root"),
               "fixture tokens");
  fixture.input.tokens.reserve(tokens.size());
  for (const JsonValue& token_value : tokens) {
    const JsonValue::Object& token = as_object(token_value, "fixture token");
    fixture.input.tokens.push_back(FixtureToken{
        as_string(required_field(token, "symbol", "fixture token"),
                  "fixture token symbol"),
        AnchorKind::kDeviceEvent,
        0,
        0,
        as_i64(required_field(token, "start_ns", "fixture token"),
               "fixture token start_ns"),
        as_i64(required_field(token, "end_ns", "fixture token"),
               "fixture token end_ns")});
  }

  if (const JsonValue* intervals_value =
          optional_field(object, "protected_intervals")) {
    const JsonValue::Array& intervals =
        as_array(*intervals_value, "fixture protected_intervals");
    fixture.input.protected_intervals.reserve(intervals.size());
    for (const JsonValue& interval_value : intervals) {
      const JsonValue::Object& interval =
          as_object(interval_value, "protected interval");
      fixture.input.protected_intervals.push_back(FixtureProtectedInterval{
          parse_interval_kind(as_string(
              required_field(interval, "kind", "protected interval"),
              "protected interval kind")),
          parse_boundary_policy(interval),
          as_size(required_field(interval, "first_token_idx",
                                 "protected interval"),
                  "protected interval first_token_idx"),
          as_size(required_field(interval, "last_token_idx",
                                 "protected interval"),
                  "protected interval last_token_idx")});
    }
  }

  const JsonValue::Object& config =
      as_object(required_field(object, "config", "fixture root"),
                "fixture config");
  fixture.partition_config.target_tokens_per_partition =
      as_size(required_field(config, "target_tokens_per_partition",
                             "fixture config"),
              "fixture config target_tokens_per_partition");
  fixture.partition_config.halo_tokens =
      as_size(required_field(config, "max_halo_tokens", "fixture config"),
              "fixture config max_halo_tokens");
  fixture.candidate_scan_config.min_length =
      as_size(required_field(config, "candidate_min_len", "fixture config"),
              "fixture config candidate_min_len");
  fixture.candidate_scan_config.max_length =
      as_size(required_field(config, "candidate_max_len", "fixture config"),
              "fixture config candidate_max_len");
  return fixture;
}

}  // namespace traceloom
