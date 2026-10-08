#include <algorithm>
#include <cctype>

#include "internal.h"

namespace traceloom::inference::detail {
// This is a rejection boundary, not a claim that regexes can redact arbitrary
// model output. Producers must emit public operation labels/opaque references.
bool public_text(const std::string& value) {
  std::string lower, compact;
  for (unsigned char c : value) {
    if ((c < 32 && c != '\n' && c != '\t') || c == 127) return false;
    const char normalized = static_cast<char>(std::tolower(c));
    lower += normalized;
    if (std::isalnum(c)) compact += normalized;
  }
  for (const auto* marker :
       {"authorization", "bearer", "apikey", "password", "secret",
        "accesstoken", "refreshtoken", "privatekey", "rawprompt", "rawoutput",
        "hiddenreasoning", "chainofthought", "systemprompt", "traceback"})
    if (compact.find(marker) != std::string::npos) return false;
  if (compact == "token" || compact == "prompt" || compact == "output" ||
      compact == "reasoning")
    return false;
  for (const auto* marker :
       {"sk-",  "://",   "http:",  "https:", "file:",  "javascript:", "data:",
        "ssh:", "s3:",   "gs:",    "../",    "..\\",   "\\",          "<",
        ">",    "/etc/", "/home/", "/tmp/",  "/proc/", "~/"})
    if (lower.find(marker) != std::string::npos) return false;
  if (lower.size() >= 3 && std::isalpha(static_cast<unsigned char>(lower[0])) &&
      lower[1] == ':' && lower[2] == '/')
    return false;
  // Common JWT framing must not be accepted as an opaque identifier.
  if (lower.rfind("eyj", 0) == 0 && lower.find('.') != std::string::npos)
    return false;
  return true;
}

void validate_projection(sqlite3* db) {
  check_version(db);
  auto count = prepare(
      db, R"SQL(SELECT count(*),coalesce(sum(length(CAST(payload AS BLOB))),0),
    count(CASE WHEN event_type='span_event' THEN 1 END)
    FROM traceloom_inference_event)SQL");
  require(next_row(count.get()) &&
              sqlite3_column_int64(count.get(), 0) <= 100000 &&
              sqlite3_column_int64(count.get(), 1) <= 16 * 1024 * 1024 &&
              sqlite3_column_int64(count.get(), 2) <= 10000,
          "inference projection exceeds event/payload budget (100000 / 16 MiB "
          "/ 10000 observations)");
  count = prepare(db,
                  "SELECT count(*) FROM (SELECT trace_id,span_id FROM "
                  "traceloom_inference_event WHERE event_type NOT IN "
                  "('metrics','trace_end') GROUP BY trace_id,span_id)");
  require(
      next_row(count.get()) && sqlite3_column_int64(count.get(), 0) <= 10000,
      "inference projection exceeds 10000 spans; query SQLite or split the "
      "artifact");
  auto policy =
      prepare(db, "SELECT include_summaries FROM traceloom_inference_meta");
  require(next_row(policy.get()), "missing inference content policy");
  const bool summaries = sqlite3_column_int(policy.get(), 0) != 0;
  // A DB supplied by another process, or created by an older importer, must not
  // bypass the current output content boundary. Reject rather than display it.
  auto records = prepare(db, "SELECT payload FROM traceloom_inference_event");
  while (next_row(records.get())) {
    const auto payload = text(records.get(), 0);
    std::size_t discarded = 0;
    require(
        normalize(db, payload, summaries, discarded) == payload &&
            discarded == 0,
        "database contains observations outside the current content policy");
  }
}
}  // namespace traceloom::inference::detail
