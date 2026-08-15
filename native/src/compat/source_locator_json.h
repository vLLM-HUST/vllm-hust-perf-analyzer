#pragma once

#include <string>

#include "traceloom/ir/native_ir.h"

namespace traceloom::compat::detail {

// Event lineage historically canonicalizes any resolvable path, while
// runtime/device locators canonicalize only observed regular files. Keep the
// two contracts explicit while sharing their filesystem/JSON implementation.
std::string event_source_lineage_json(const SourceRefRow& source);
std::string relation_source_locator_json(const SourceRefRow& source,
                                         const char* relation_object);

}  // namespace traceloom::compat::detail
