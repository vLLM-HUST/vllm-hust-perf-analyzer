#pragma once
struct sqlite3;
namespace traceloom::compat {
// Extend host/provider candidates through exact, launch-specific membership.
// Called before step ambiguity is resolved; no temporal ownership inference.
void bind_scheduler_replay_context(sqlite3* db);
}
