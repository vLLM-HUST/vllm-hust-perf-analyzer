#pragma once

struct sqlite3;

namespace traceloom::compat::detail {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
void materialize_position_projection_catalog(sqlite3* db);
#endif

}  // namespace traceloom::compat::detail
