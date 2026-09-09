#include <algorithm>
#include <set>

#include "internal.h"

namespace traceloom::inference::detail {
namespace {
struct Value {
  std::string type, value;
};
using Object = std::map<std::string, Value>;

bool utf8(const std::string& s) {
  for (std::size_t i = 0; i < s.size();) {
    auto c = static_cast<unsigned char>(s[i++]);
    if (c < 128) {
      if (c == 0) return false;
      continue;
    }
    unsigned count = 0, code = 0, minimum = 0;
    if (c >= 0xc2 && c <= 0xdf) {
      count = 1;
      code = c & 31;
      minimum = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
      count = 2;
      code = c & 15;
      minimum = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
      count = 3;
      code = c & 7;
      minimum = 0x10000;
    } else
      return false;
    if (i + count > s.size()) return false;
    while (count--) {
      c = static_cast<unsigned char>(s[i++]);
      if ((c & 0xc0) != 0x80) return false;
      code = (code << 6) | (c & 63);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
      return false;
  }
  return true;
}
bool label(const std::string& s) {
  return !s.empty() && s.size() <= 128 &&
         std::all_of(s.begin(), s.end(), [](char c) {
           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '.' || c == ':' ||
                  c == '-';
         });
}
bool hex_id(const std::string& s, std::size_t n) {
  return s.size() == n && s.find_first_not_of('0') != std::string::npos &&
         std::all_of(s.begin(), s.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
Object object(sqlite3* db, const std::string& input) {
  auto q = prepare(db, "SELECT key,type,value FROM json_each(?)");
  bind_text(q.get(), 1, input);
  Object out;
  int rc;
  while ((rc = sqlite3_step(q.get())) == SQLITE_ROW) {
    const std::string type = text(q.get(), 1);
    if (type == "integer")
      require(sqlite3_column_type(q.get(), 2) == SQLITE_INTEGER &&
                  sqlite3_column_int64(q.get(), 2) >= 0,
              "invalid nonnegative integer");
    require(out.emplace(text(q.get(), 0), Value{type, text(q.get(), 2)}).second,
            "duplicate JSON object key");
  }
  require(rc == SQLITE_DONE, "invalid JSON object");
  return out;
}
std::string string(const Object& o, const std::string& key,
                   bool required = true) {
  auto it = o.find(key);
  if (it == o.end()) {
    require(!required, "missing required inference field");
    return "";
  }
  require(it->second.type == "text", "inference field requires a string");
  return it->second.value;
}
std::string integer(const Object& o, const std::string& key) {
  auto it = o.find(key);
  require(it != o.end() && it->second.type == "integer",
          "inference field requires an integer");
  return it->second.value;
}
std::string serialize(const std::map<std::string, std::string>& fields) {
  std::string out = "{";
  for (const auto& [key, value] : fields) {
    if (out.size() > 1) out += ',';
    out += quote(key) + ":" + value;
  }
  return out + '}';
}
std::string array(sqlite3* db, const Object& o, const std::string& key,
                  bool ids) {
  auto it = o.find(key);
  if (it == o.end()) return "[]";
  require(it->second.type == "array", "inference references require an array");
  auto q = prepare(db, "SELECT type,value FROM json_each(?)");
  bind_text(q.get(), 1, it->second.value);
  std::set<std::string> values;
  std::size_t count = 0;
  int rc;
  while ((rc = sqlite3_step(q.get())) == SQLITE_ROW) {
    auto value = text(q.get(), 1);
    require(++count <= 16 && text(q.get(), 0) == "text" &&
                (ids ? hex_id(value, 16) : label(value)),
            "invalid inference reference");
    require(values.insert(value).second, "duplicate inference reference");
  }
  require(rc == SQLITE_DONE, "invalid inference reference array");
  std::string out = "[";
  for (const auto& s : values) {
    if (out.size() > 1) out += ',';
    out += quote(s);
  }
  return out + ']';
}
}  // namespace

std::string normalize(sqlite3* db, const std::string& line, bool summaries,
                      std::size_t& discarded) {
  require(utf8(line), "invalid UTF-8 record");
  auto valid = prepare(db, "SELECT json_valid(?)");
  bind_text(valid.get(), 1, line);
  require(sqlite3_step(valid.get()) == SQLITE_ROW &&
              sqlite3_column_int(valid.get(), 0),
          "invalid inference JSON");
  auto tree = prepare(db, "SELECT parent,key,type FROM json_tree(?)");
  bind_text(tree.get(), 1, line);
  require(
      sqlite3_step(tree.get()) == SQLITE_ROW && text(tree.get(), 2) == "object",
      "inference record must be an object");
  std::set<std::pair<sqlite3_int64, std::string>> keys;
  while (next_row(tree.get())) {
    require(
        keys.emplace(sqlite3_column_int64(tree.get(), 0), text(tree.get(), 1))
            .second,
        "duplicate JSON key");
  }
  const Object o = object(db, line);
  static const std::set<std::string> allowed = {
      "schema_version", "event_id",     "trace_id",     "span_id",
      "parent_span_id", "producer_id",  "clock_id",     "sequence",
      "event_type",     "wall_time_ns", "monotonic_ns", "name",
      "kind",           "status",       "attributes",   "decision_summary",
      "evidence_refs",  "links"};
  for (const auto& [k, v] : o) {
    (void)v;
    require(allowed.count(k), "unknown inference envelope field");
  }
  require(integer(o, "schema_version") == "1",
          "unsupported inference wire version");
  std::map<std::string, std::string> f;
  for (const auto* k :
       {"schema_version", "sequence", "wall_time_ns", "monotonic_ns"})
    f[k] = integer(o, k);
  for (const auto* k : {"event_id", "producer_id", "clock_id"}) {
    auto s = string(o, k);
    require(label(s), "invalid inference identifier");
    f[k] = quote(s);
  }
  for (const auto* k : {"trace_id", "span_id"}) {
    auto s = string(o, k);
    require(hex_id(s, std::string(k) == "trace_id" ? 32 : 16),
            "invalid trace/span ID");
    f[k] = quote(s);
  }
  const auto type = string(o, "event_type");
  require(type == "span_start" || type == "span_end" || type == "span_event" ||
              type == "trace_end" || type == "metrics",
          "unsupported inference event type");
  f["event_type"] = quote(type);
  if (o.count("parent_span_id") && o.at("parent_span_id").type != "null") {
    auto s = string(o, "parent_span_id");
    require(hex_id(s, 16) && s != string(o, "span_id"), "invalid parent span");
    f["parent_span_id"] = quote(s);
  }
  if (type == "span_start" || o.count("name")) {
    auto s = string(o, "name");
    require(label(s), "invalid inference name");
    f["name"] = quote(s);
  }
  if (type == "span_start" || o.count("kind")) {
    auto s = string(o, "kind");
    require(s == "pipeline" || s == "model" || s == "retrieval" ||
                s == "tool" || s == "data" || s == "step",
            "invalid span kind");
    f["kind"] = quote(s);
  }
  if (type == "span_end" || type == "trace_end" || o.count("status")) {
    auto s = string(o, "status");
    require(s == "ok" || s == "error" || s == "cancelled",
            "invalid span status");
    f["status"] = quote(s);
  }
  std::map<std::string, std::string> attrs;
  if (o.count("attributes")) {
    require(o.at("attributes").type == "object",
            "attributes must be an object");
    const auto a = object(db, o.at("attributes").value);
    require(a.size() <= 32, "too many attributes");
    static const std::set<std::string> labels = {"operation", "model",
                                                 "provider", "error_type"};
    static const std::set<std::string> counts = {
        "attempt",       "input_bytes",     "output_bytes",  "input_tokens",
        "output_tokens", "retrieved_count", "dropped_events"};
    for (const auto& [k, v] : a) {
      if (labels.count(k)) {
        auto checked = v.value;
        if (k == "model") {
          require(checked.empty() || (checked.front() != '/' &&
                                      checked.find("//") == std::string::npos),
                  "invalid model label");
          std::replace(checked.begin(), checked.end(), '/', '_');
        }
        require(v.type == "text" && label(checked), "invalid metadata label");
        attrs[k] = quote(v.value);
      } else if (counts.count(k)) {
        require(v.type == "integer", "invalid metadata count");
        attrs[k] = v.value;
      } else
        ++discarded;
    }
  }
  f["attributes"] = serialize(attrs);
  if (o.count("decision_summary")) {
    auto s = string(o, "decision_summary");
    require(s.size() <= 256 && utf8(s), "invalid public summary");
    if (summaries) f["decision_summary"] = quote(s);
  }
  f["evidence_refs"] = array(db, o, "evidence_refs", false);
  f["links"] = array(db, o, "links", true);

  return serialize(f);
}
}  // namespace traceloom::inference::detail
