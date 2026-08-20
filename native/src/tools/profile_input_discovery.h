#pragma once

#include <string>
#include <vector>

namespace traceloom::tools {

std::vector<std::string> discover_profile_dbs(
    const std::string& input,
    const std::string& source_kind);

std::string input_format_for(const std::string& source_db,
                             const std::string& source_kind);

}  // namespace traceloom::tools
