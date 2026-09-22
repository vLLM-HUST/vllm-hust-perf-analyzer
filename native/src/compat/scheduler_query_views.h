#pragma once
struct sqlite3;
namespace traceloom::compat {
// Consumer-facing request coordinates and observed scheduler context.
// Does not infer request completion, semantic phases or device ownership.
void materialize_scheduler_query_views(sqlite3* db);
}
