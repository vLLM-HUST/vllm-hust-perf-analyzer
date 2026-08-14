#include "traceloom/compat/evidence_role_sql_rows.h"

#include <stdexcept>

#include "evidence_role_sql_internal.h"

namespace traceloom::compat {

void replace_evidence_role_sql_rows(
    const std::string& sqlite_path,
    const NativeIr& ir,
    FlatAnchorBuildConfig config,
    std::uint32_t db_idx,
    bool materialize_aux_attribution) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  if (config.classification_rules.rules().empty()) {
    config.classification_rules = load_default_signal_classification_ruleset();
  }
  if (!config.classification_overrides.empty()) {
    config.classification_rules = override_signal_classification_ruleset(
        config.classification_rules, config.classification_overrides);
  }
  const detail::EvidenceRoleSqlRows rows = detail::build_evidence_role_sql_rows(
      ir, config, db_idx, materialize_aux_attribution);
  detail::write_evidence_role_sql_rows(
      sqlite_path, config.classification_rules, rows);
#else
  (void)sqlite_path;
  (void)ir;
  (void)config;
  (void)db_idx;
  (void)materialize_aux_attribution;
  throw std::runtime_error("evidence-role SQL requires SQLite support");
#endif
}

}  // namespace traceloom::compat
