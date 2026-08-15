#pragma once

#include <filesystem>

namespace traceloom::test {

void materialize_sqlite_fixture(const std::filesystem::path& database_path,
                                const std::filesystem::path& script_path);

void apply_sqlite_fixture_mutation(
    const std::filesystem::path& database_path,
    const std::filesystem::path& script_path);

}  // namespace traceloom::test
