#pragma once

#include <string>
#include <vector>

#include "augmented_catalog_materializer.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
namespace traceloom::compat::detail {

RawPackagingResult package_sqlite_sources(
    const std::vector<std::string>& source_paths,
    const std::string& destination_path);

}  // namespace traceloom::compat::detail
#endif
