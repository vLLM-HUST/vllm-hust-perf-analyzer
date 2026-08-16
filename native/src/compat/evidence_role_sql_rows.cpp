#include "traceloom/compat/evidence_role_sql_rows.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <utility>

#include "evidence_role_sql_internal.h"

namespace traceloom::compat {
namespace {

void replace_evidence_role_sql_rows_impl(
    const std::string& sqlite_path,
    const NativeIr& ir,
    FlatAnchorBuildConfig config,
    std::uint32_t db_idx,
    bool materialize_aux_attribution,
    const AuxAttributionSqlRows* prebuilt_aux_attribution,
    bool timing_diagnostics) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  if (config.classification_rules.rules().empty()) {
    config.classification_rules = load_default_signal_classification_ruleset();
  }
  if (!config.classification_overrides.empty()) {
    config.classification_rules = override_signal_classification_ruleset(
        config.classification_rules, config.classification_overrides);
  }
  const auto build_start = std::chrono::steady_clock::now();
  const detail::EvidenceRoleSqlRows rows = detail::build_evidence_role_sql_rows(
      ir, config, db_idx, materialize_aux_attribution,
      prebuilt_aux_attribution);
  const auto write_start = std::chrono::steady_clock::now();
  if (timing_diagnostics) {
    std::cerr << "timing evidence_role_build_rows_ms="
              << std::chrono::duration<double, std::milli>(write_start -
                                                           build_start)
                     .count()
              << "\n";
  }
  detail::write_evidence_role_sql_rows(
      sqlite_path, config.classification_rules, rows, timing_diagnostics);
  if (timing_diagnostics) {
    std::cerr << "timing evidence_role_write_rows_ms="
              << std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - write_start)
                     .count()
              << "\n";
  }
#else
  (void)sqlite_path;
  (void)ir;
  (void)config;
  (void)db_idx;
  (void)materialize_aux_attribution;
  (void)prebuilt_aux_attribution;
  (void)timing_diagnostics;
  throw std::runtime_error("evidence-role SQL requires SQLite support");
#endif
}

}  // namespace

void replace_evidence_role_sql_rows(
    const std::string& sqlite_path,
    const NativeIr& ir,
    FlatAnchorBuildConfig config,
    std::uint32_t db_idx,
    bool materialize_aux_attribution,
    bool timing_diagnostics) {
  replace_evidence_role_sql_rows_impl(
      sqlite_path, ir, std::move(config), db_idx,
      materialize_aux_attribution, nullptr, timing_diagnostics);
}

void replace_evidence_role_sql_rows(
    const std::string& sqlite_path,
    const NativeIr& ir,
    FlatAnchorBuildConfig config,
    std::uint32_t db_idx,
    const AuxAttributionSqlRows& aux_attribution,
    bool timing_diagnostics) {
  replace_evidence_role_sql_rows_impl(
      sqlite_path, ir, std::move(config), db_idx, true, &aux_attribution,
      timing_diagnostics);
}

}  // namespace traceloom::compat
