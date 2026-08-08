#pragma once

#include <string>

#include "traceloom/analysis/idle_evidence_pipeline.h"
#include "traceloom/compat/idle_explanation_rows.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

struct IdleEvidenceSqlRowOptions {
  std::uint32_t db_idx = 0;
  std::string source_kind = "native_ir";
  std::string source_path;
  std::string contract_version = "idle-evidence-contract-v4.4";
  std::string host_api_rules_version = "not_loaded";
  std::string host_api_rules_sha256;
};

IdleEvidenceSqlRows build_idle_evidence_sql_rows(
    const NativeIr& ir,
    const IdleEvidencePipelineResult& pipeline,
    const IdleExplanationAttributionRows& attribution,
    const IdleEvidenceSqlRowOptions& options = {});

}  // namespace traceloom::compat
