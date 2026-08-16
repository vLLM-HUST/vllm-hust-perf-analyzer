#pragma once

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
#include <sqlite3.h>

namespace traceloom::compat::detail {

void materialize_replay_position_views(sqlite3* db);

}  // namespace traceloom::compat::detail
#endif
