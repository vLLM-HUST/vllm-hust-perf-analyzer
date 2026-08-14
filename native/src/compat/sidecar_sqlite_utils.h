#pragma once

#include <cstdint>
#include <string>

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
#include <sqlite3.h>

namespace traceloom::compat::detail {

std::string quote_identifier(const std::string& value);
std::string quote_literal(const std::string& value);

void sqlite_exec(sqlite3* db, const std::string& sql,
                 const std::string& context);

std::uint64_t sqlite_scalar_u64(sqlite3* db, const std::string& sql,
                                const std::string& context);

sqlite3* open_sqlite_readwrite(const std::string& path);

}  // namespace traceloom::compat::detail
#endif
