#include "source_locator_json.h"

#include <filesystem>
#include <sstream>

namespace traceloom::compat::detail {
namespace {

std::string complete_json_escape(const std::string& value) {
  std::ostringstream out;
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\':
        out << "\\\\";
        break;
      case '"':
        out << "\\\"";
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
          const char* digits = "0123456789abcdef";
          out << "\\u00" << digits[(ch >> 4) & 0xf] << digits[ch & 0xf];
        } else {
          out << static_cast<char>(ch);
        }
    }
  }
  return out.str();
}

std::string locator_json_escape(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    if (ch == '\\' || ch == '"') out.push_back('\\');
    out.push_back(ch);
  }
  return out;
}

}  // namespace

std::string event_source_lineage_json(const SourceRefRow& source) {
  std::error_code ec;
  const std::filesystem::path absolute =
      std::filesystem::absolute(source.source_path, ec).lexically_normal();
  const std::string source_path = ec ? source.source_path : absolute.string();
  std::ostringstream out;
  out << "{\"source_ref_id\":" << source.id.value()
      << ",\"source_path\":\"" << complete_json_escape(source_path)
      << "\"}";
  return out.str();
}

std::string relation_source_locator_json(const SourceRefRow& source,
                                         const char* relation_object) {
  std::string source_path = source.source_path;
  std::error_code ec;
  const std::filesystem::path observed_path(source_path);
  if (std::filesystem::is_regular_file(observed_path, ec)) {
    const std::filesystem::path absolute_path =
        std::filesystem::absolute(observed_path, ec);
    if (!ec) source_path = absolute_path.lexically_normal().string();
  }
  std::ostringstream out;
  out << "{\"source_ref_id\":" << source.id.value()
      << ",\"source_path\":\"" << locator_json_escape(source_path) << "\""
      << ",\"relation_object\":\"" << relation_object << "\"}";
  return out.str();
}

}  // namespace traceloom::compat::detail
