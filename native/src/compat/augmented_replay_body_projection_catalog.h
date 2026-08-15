#pragma once

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
#include <sqlite3.h>

namespace traceloom::compat::detail {

// Extends the base projection catalog without growing the already broad
// general catalog implementation. The caller owns the surrounding catalog
// transaction and creates the catalog tables first.
void materialize_replay_body_projection_catalog(sqlite3* db);

}  // namespace traceloom::compat::detail
#endif
