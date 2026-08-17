#pragma once

#include <cstdint>
#include <string>

#include "traceloom/compat/sidecar_writer.h"

namespace traceloom::compat::detail {

std::string public_runtime_api_family(const std::string& api_name);
void materialize_host_activity_rows(RuntimeDeviceSqlRows& rows,
                                    std::uint64_t max_activity_rows);

}  // namespace traceloom::compat::detail
