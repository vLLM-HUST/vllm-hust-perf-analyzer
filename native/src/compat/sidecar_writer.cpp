#include "traceloom/compat/sidecar_writer.h"

#include <stdexcept>
#include <string>

#include "sqlite_support.h"

namespace traceloom::compat {

void materialize_compatibility_schema(const std::string& sqlite_path) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path, compatibility_table_schemas());
#else
  (void)sqlite_path;
  throw std::runtime_error(
      "compatibility sidecar writer requires SQLite support");
#endif
}

void materialize_compatibility_schema(
    const std::string& sqlite_path,
    const std::vector<CompatTableSchema>& schemas) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    for (const CompatTableSchema& schema : schemas) {
      db.exec(sqlite_create_table_sql(schema));
      if (schema.name == "traceloom_viz_node_anchor") {
        ensure_viz_node_anchor_cost_columns(db);
      }
    }
    db.exec("COMMIT");
  } catch (...) {
    try {
      db.exec("ROLLBACK");
    } catch (...) {
    }
    throw;
  }
#else
  (void)sqlite_path;
  (void)schemas;
  throw std::runtime_error(
      "compatibility sidecar writer requires SQLite support");
#endif
}

}  // namespace traceloom::compat
