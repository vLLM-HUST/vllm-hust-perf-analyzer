#include "support/ascend_sqlite_fixture.h"

#include "support/sqlite_fixture.h"

#include <chrono>
#include <initializer_list>
#include <string>

namespace traceloom::test {
namespace {

void materialize_sqlite_profile(
    const std::filesystem::path& output_root,
    const std::filesystem::path& fixture_root,
    std::initializer_list<const char*> relative_scripts) {
  for (const char* relative_script : relative_scripts) {
    std::filesystem::path relative_database(relative_script);
    relative_database.replace_extension(".db");
    materialize_sqlite_fixture(output_root / relative_database,
                               fixture_root / relative_script);
  }
}

}  // namespace

std::string temp_ascend_db_path(std::string_view suffix) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return (std::filesystem::temp_directory_path() /
          ("traceloom_ascend_sqlite_adapter_" + std::to_string(now) +
           std::string(suffix) + ".db"))
      .string();
}

std::filesystem::path temp_ascend_profile_dir(std::string_view suffix) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("traceloom_ascend_sqlite_adapter_" + std::to_string(now) +
          std::string(suffix));
}

std::filesystem::path ascend_sqlite_fixture_dir(
    std::string_view fixture_name) {
  return std::filesystem::path(TRACELOOM_NATIVE_FIXTURE_ROOT) /
         "ascend_sqlite" / std::string(fixture_name);
}

void materialize_ascend_minimal_fixture(
    const std::filesystem::path& database_path) {
  materialize_sqlite_fixture(
      database_path,
      ascend_sqlite_fixture_dir("minimal_smoke") / "msprof.sql");
}

void materialize_ascend_graph_fixture(
    const std::filesystem::path& output_dir, std::string_view fixture_name) {
  const std::filesystem::path fixture =
      ascend_sqlite_fixture_dir(fixture_name);
  materialize_sqlite_fixture(output_dir / "msprof.db",
                             fixture / "msprof.sql");
  materialize_sqlite_fixture(
      output_dir / "host" / "sqlite" / "stream_info.db",
      fixture / "host" / "sqlite" / "stream_info.sql");
}

void materialize_ascend_graph_split_fixture(
    const std::filesystem::path& output_dir, std::string_view fixture_name) {
  materialize_sqlite_profile(
      output_dir, ascend_sqlite_fixture_dir(fixture_name) / "split",
      {"device_0/sqlite/ascend_task.sql",
       "device_0/sqlite/hccl_single_device.sql",
       "host/sqlite/api_event.sql", "host/sqlite/ge_info.sql",
       "host/sqlite/stream_info.sql"});
}

void materialize_ascend_split_golden_profiles(
    const std::filesystem::path& output_dir,
    const std::filesystem::path& monolithic_path) {
  const std::filesystem::path fixture =
      ascend_sqlite_fixture_dir("split_golden");
  materialize_sqlite_fixture(monolithic_path, fixture / "monolithic.sql");
  materialize_sqlite_profile(
      output_dir, fixture,
      {"device_0/sqlite/ascend_task.sql",
       "device_0/sqlite/hccl_single_device.sql",
       "host/sqlite/api_event.sql", "host/sqlite/ge_info.sql",
       "host/sqlite/hccl.sql", "host/sqlite/runtime.sql"});
}

void apply_ascend_fixture_mutation(
    const std::filesystem::path& database_path,
    std::string_view fixture_name, std::string_view mutation_name) {
  apply_sqlite_fixture_mutation(
      database_path, ascend_sqlite_fixture_dir(fixture_name) / "mutations" /
                         std::string(mutation_name));
}

}  // namespace traceloom::test
