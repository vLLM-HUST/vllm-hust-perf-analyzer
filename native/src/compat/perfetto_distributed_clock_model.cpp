#include "perfetto_export_internal.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include "traceloom/core/sha256.h"

namespace traceloom::compat::perfetto_internal {
namespace {

constexpr const char* kModelFormat =
    "traceloom.distributed-clock-model/v1";
constexpr std::uintmax_t kMaximumReceiptBytes = 16u * 1024u * 1024u;

struct Scalar {
  std::string value;
  bool quoted = false;
};

using FlatObject = std::map<std::string, Scalar>;

void skip_space(const std::string& text, std::size_t& at) {
  while (at < text.size() &&
         (text[at] == ' ' || text[at] == '\t' || text[at] == '\r')) {
    ++at;
  }
}

std::string parse_string(const std::string& text, std::size_t& at,
                         const std::string& context) {
  if (at >= text.size() || text[at] != '"') {
    throw std::invalid_argument(context + ": expected JSON string");
  }
  ++at;
  std::string value;
  while (at < text.size()) {
    const char ch = text[at++];
    if (ch == '"') return value;
    if (static_cast<unsigned char>(ch) < 0x20) {
      throw std::invalid_argument(context +
                                  ": control byte in JSON string");
    }
    if (ch != '\\') {
      value.push_back(ch);
      continue;
    }
    if (at >= text.size()) {
      throw std::invalid_argument(context + ": truncated JSON escape");
    }
    switch (text[at++]) {
      case '"': value.push_back('"'); break;
      case '\\': value.push_back('\\'); break;
      case '/': value.push_back('/'); break;
      case 'b': value.push_back('\b'); break;
      case 'f': value.push_back('\f'); break;
      case 'n': value.push_back('\n'); break;
      case 'r': value.push_back('\r'); break;
      case 't': value.push_back('\t'); break;
      case 'u':
        throw std::invalid_argument(
            context + ": Unicode escapes are outside the ASCII receipt contract");
      default:
        throw std::invalid_argument(context + ": invalid JSON escape");
    }
  }
  throw std::invalid_argument(context + ": unterminated JSON string");
}

FlatObject parse_flat_object(const std::string& line,
                             const std::string& context) {
  std::size_t at = 0;
  skip_space(line, at);
  if (at >= line.size() || line[at++] != '{') {
    throw std::invalid_argument(context + ": expected a JSON object");
  }
  FlatObject object;
  while (true) {
    skip_space(line, at);
    if (at < line.size() && line[at] == '}') {
      ++at;
      break;
    }
    const std::string key = parse_string(line, at, context);
    skip_space(line, at);
    if (at >= line.size() || line[at++] != ':') {
      throw std::invalid_argument(context + ": expected ':' after " + key);
    }
    skip_space(line, at);
    Scalar scalar;
    if (at < line.size() && line[at] == '"') {
      scalar.value = parse_string(line, at, context);
      scalar.quoted = true;
    } else {
      const std::size_t begin = at;
      while (at < line.size() && line[at] != ',' && line[at] != '}') ++at;
      std::size_t end = at;
      while (end > begin &&
             (line[end - 1] == ' ' || line[end - 1] == '\t' ||
              line[end - 1] == '\r')) {
        --end;
      }
      if (begin == end || line[begin] == '[' || line[begin] == '{') {
        throw std::invalid_argument(context +
                                    ": values must be flat JSON scalars");
      }
      scalar.value = line.substr(begin, end - begin);
    }
    if (!object.emplace(key, std::move(scalar)).second) {
      throw std::invalid_argument(context + ": duplicate field " + key);
    }
    skip_space(line, at);
    if (at >= line.size()) {
      throw std::invalid_argument(context + ": unterminated JSON object");
    }
    if (line[at] == ',') {
      ++at;
      continue;
    }
    if (line[at] == '}') {
      ++at;
      break;
    }
    throw std::invalid_argument(context + ": expected ',' or '}'");
  }
  skip_space(line, at);
  if (at != line.size()) {
    throw std::invalid_argument(context + ": trailing JSON content");
  }
  return object;
}

const Scalar& required(const FlatObject& object, const std::string& name,
                       bool quoted, const std::string& context) {
  const auto found = object.find(name);
  if (found == object.end()) {
    throw std::invalid_argument(context + ": missing field " + name);
  }
  if (found->second.quoted != quoted) {
    throw std::invalid_argument(context + ": field " + name +
                                (quoted ? " must be a string" :
                                          " must be numeric"));
  }
  return found->second;
}

std::int64_t integer_field(const FlatObject& object, const std::string& name,
                           const std::string& context) {
  const std::string& value = required(object, name, false, context).value;
  std::size_t consumed = 0;
  long long parsed = 0;
  try {
    parsed = std::stoll(value, &consumed, 10);
  } catch (const std::exception&) {
    throw std::invalid_argument(context + ": invalid integer field " + name);
  }
  if (consumed != value.size()) {
    throw std::invalid_argument(context + ": invalid integer field " + name);
  }
  return static_cast<std::int64_t>(parsed);
}

long double decimal_field(const FlatObject& object, const std::string& name,
                          const std::string& context) {
  const std::string& value = required(object, name, false, context).value;
  char* end = nullptr;
  errno = 0;
  const long double parsed = std::strtold(value.c_str(), &end);
  if (errno == ERANGE || end != value.c_str() + value.size() ||
      !std::isfinite(parsed)) {
    throw std::invalid_argument(context + ": invalid decimal field " + name);
  }
  return parsed;
}

ClockCalibrationStatus parse_status(const std::string& value,
                                    const std::string& context) {
  if (value == "candidate_only") return ClockCalibrationStatus::kCandidateOnly;
  if (value == "calibrated") return ClockCalibrationStatus::kCalibrated;
  throw std::invalid_argument(
      context + ": status must be candidate_only or calibrated");
}

}  // namespace

DistributedClockModelSet load_distributed_clock_models(
    const std::string& path,
    const std::vector<PerfettoDistributedRankInput>& ranks,
    int reference_rank) {
  if (path.empty()) {
    throw std::invalid_argument("distributed clock-model path must not be empty");
  }
  std::error_code size_error;
  const std::uintmax_t size = std::filesystem::file_size(path, size_error);
  if (size_error) {
    throw std::invalid_argument("cannot inspect distributed clock-model receipt: " +
                                path + ": " + size_error.message());
  }
  if (size == 0 || size > kMaximumReceiptBytes) {
    throw std::invalid_argument(
        "distributed clock-model receipt must be between 1 byte and 16 MiB: " +
        path);
  }
  std::ifstream input(path);
  if (!input) {
    throw std::invalid_argument("cannot open distributed clock-model receipt: " +
                                path);
  }

  std::set<int> expected;
  bool reference_present = false;
  for (const auto& rank : ranks) {
    if (rank.rank == reference_rank) {
      reference_present = true;
    } else {
      expected.insert(rank.rank);
    }
  }
  if (!reference_present) {
    throw std::invalid_argument(
        "distributed clock-model reference rank was not provided");
  }

  DistributedClockModelSet result;
  result.path = path;
  std::string target_clock_domain;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) continue;
    const std::string context = path + ":" + std::to_string(line_number);
    const FlatObject object = parse_flat_object(line, context);
    if (required(object, "format", true, context).value != kModelFormat) {
      throw std::invalid_argument(context + ": unsupported model format");
    }
    const std::int64_t rank_value = integer_field(object, "rank", context);
    const std::int64_t model_reference =
        integer_field(object, "reference_rank", context);
    if (rank_value < 0 ||
        rank_value > static_cast<std::int64_t>(std::numeric_limits<int>::max()) ||
        model_reference != reference_rank) {
      throw std::invalid_argument(context +
                                  ": rank/reference_rank does not match export");
    }
    const int rank = static_cast<int>(rank_value);
    const std::string metric = required(object, "metric", true, context).value;
    if (metric != "end") continue;
    if (rank == reference_rank || expected.count(rank) == 0) {
      throw std::invalid_argument(context +
                                  ": end model does not name a non-reference input rank");
    }

    ClockCalibrationModel model;
    model.status = parse_status(required(object, "status", true, context).value,
                                context);
    model.source_clock_domain =
        required(object, "source_clock_domain", true, context).value;
    model.target_clock_domain =
        required(object, "target_clock_domain", true, context).value;
    model.marker_contract =
        required(object, "marker_contract", true, context).value;
    model.scale = decimal_field(object, "scale", context);
    model.reference_source_ns =
        decimal_field(object, "reference_source_ns", context);
    model.reference_target_ns =
        decimal_field(object, "reference_target_ns", context);
    if (model.source_clock_domain.empty() || model.target_clock_domain.empty() ||
        model.marker_contract.empty() || model.scale <= 0.0L) {
      throw std::invalid_argument(context +
                                  ": clock domains, marker contract, and positive scale are required");
    }
    if (!result.models.emplace(rank, model).second) {
      throw std::invalid_argument(context + ": duplicate end model for rank " +
                                  std::to_string(rank));
    }
    const std::string status(clock_calibration_status_name(model.status));
    if (result.evidence_status.empty()) {
      result.evidence_status = status;
      result.marker_contract = model.marker_contract;
      target_clock_domain = model.target_clock_domain;
    } else if (result.evidence_status != status ||
               result.marker_contract != model.marker_contract ||
               target_clock_domain != model.target_clock_domain) {
      throw std::invalid_argument(
          context +
          ": all end models must share status, marker contract, and target clock domain");
    }
  }
  if (!input.eof()) {
    throw std::runtime_error("failed while reading distributed clock-model receipt: " +
                             path);
  }
  std::set<int> actual;
  for (const auto& item : result.models) actual.insert(item.first);
  if (actual != expected) {
    throw std::invalid_argument(
        "distributed clock-model receipt must contain exactly one end model for every non-reference rank");
  }
  result.sha256 = sha256_file_hex(path);
  return result;
}

}  // namespace traceloom::compat::perfetto_internal
