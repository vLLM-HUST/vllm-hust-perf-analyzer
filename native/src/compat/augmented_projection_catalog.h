#pragma once

#include "augmented_catalog_materializer.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
#include <sqlite3.h>

namespace traceloom::compat::detail {

void materialize_projection_catalog(
    sqlite3* db,
    const RawPackagingResult& packaging);

}  // namespace traceloom::compat::detail
#endif
