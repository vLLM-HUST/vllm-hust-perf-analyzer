#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace traceloom::test {

std::string temp_ascend_db_path(std::string_view suffix);

std::filesystem::path temp_ascend_profile_dir(std::string_view suffix);

void materialize_ascend_minimal_fixture(
    const std::filesystem::path& database_path);

void materialize_ascend_graph_fixture(
    const std::filesystem::path& output_dir, std::string_view fixture_name);

void materialize_ascend_graph_split_fixture(
    const std::filesystem::path& output_dir, std::string_view fixture_name);

void materialize_ascend_split_golden_profiles(
    const std::filesystem::path& output_dir,
    const std::filesystem::path& monolithic_path);

void apply_ascend_fixture_mutation(
    const std::filesystem::path& database_path,
    std::string_view fixture_name, std::string_view mutation_name);

std::filesystem::path ascend_sqlite_fixture_dir(
    std::string_view fixture_name);

}  // namespace traceloom::test
