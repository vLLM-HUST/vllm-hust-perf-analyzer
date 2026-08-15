#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat::detail {

struct EvidenceRolePlacementRow {
  std::uint32_t placement_order = 0;
  std::string placement_kind;
  std::string placement_id;
  std::string owner_id;
  std::string relation_name;
  std::string support_state = "supported";
  std::string reason_code;
};

struct EvidenceRoleIssueRow {
  std::string code;
  std::string support_state;
  std::string related_ids;
};

struct ProtectedIntervalSqlRow {
  std::string protected_interval_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string kind;
  std::string boundary_policy;
  std::string first_anchor_id;
  std::string last_anchor_id;
  std::string replay_unit_id;
  std::uint32_t evidence_source_ref_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::string support_state;
  std::string reason_code;
};

struct EvidenceRoleDecisionRow {
  std::string decision_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::int64_t task_id = -1;
  std::string event_id;
  std::uint32_t source_ref_id = 0;
  std::string source_domain;
  std::string input_provider_scope;
  std::string policy_id;
  std::string policy_version;
  std::string manifest_sha256;
  std::string policy_role;
  std::string final_role;
  std::string rule_id;
  std::string rule_class;
  bool matched_rule = false;
  std::int32_t priority = 0;
  std::uint64_t declaration_order = 0;
  std::string policy_structural_participation;
  std::string effective_structural_participation;
  std::string support_state;
  std::string reason_code;
  std::string available_fields;
  std::string required_fields;
  std::string missing_required_fields;
  std::string missing_capability_rule_ids;
  std::string cost_treatment;
  std::string context_treatment;
  std::string provenance_treatment;
  std::string source_table;
  std::string source_key;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::int64_t duration_ns = 0;
  std::vector<EvidenceRolePlacementRow> placements;
  std::vector<EvidenceRoleIssueRow> issues;
};

struct ReplayUnitAnchorSqlRow {
  std::string replay_unit_id;
  std::string anchor_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
};

struct EvidenceRoleSqlRows {
  std::vector<EvidenceRoleDecisionRow> decisions;
  std::vector<ProtectedIntervalSqlRow> protected_intervals;
  std::vector<ReplayUnitAnchorSqlRow> replay_unit_anchors;
};

EvidenceRoleSqlRows build_evidence_role_sql_rows(
    const NativeIr& ir,
    const FlatAnchorBuildConfig& config,
    std::uint32_t db_idx,
    bool materialize_aux_attribution,
    const AuxAttributionSqlRows* prebuilt_aux_attribution = nullptr);

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
void write_evidence_role_sql_rows(
    const std::string& sqlite_path,
    const SignalClassificationRuleset& ruleset,
    const EvidenceRoleSqlRows& rows,
    bool timing_diagnostics = false);
#endif

}  // namespace traceloom::compat::detail
