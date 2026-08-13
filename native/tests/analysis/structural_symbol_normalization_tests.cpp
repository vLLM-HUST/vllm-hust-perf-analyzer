#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/analysis/structural_symbol_normalization.h"
#include "traceloom/testing/test_util.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path temp_manifest(const std::string& suffix) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("traceloom_symbol_rules_" + std::to_string(now) + suffix + ".tsv");
}

void write_file(const std::filesystem::path& path,
                const std::string& content) {
  std::ofstream out(path);
  traceloom::testing::require(out.good());
  out << content;
}

traceloom::NativeIr build_one_task(const std::string& provider,
                                   const std::string& symbol) {
  using namespace traceloom;
  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append(provider, "profile.db", "TASK", 0);
  const SymbolId ai_core = ir.symbols.intern("AI_CORE");
  const SymbolId op = ir.symbols.intern(symbol);
  const TraceEventId event =
      ir.trace_events.append(source, 1, 0, 3, 100, 200, op);
  ir.tasks.append(source, event, 1, 1, -1, ai_core, SymbolId::invalid(), op,
                  SymbolId::invalid(), SymbolId::invalid());
  return ir;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  const StructuralSymbolNormalizationRuleset defaults =
      load_default_structural_symbol_ruleset();
  require(defaults.policy_id() == "traceloom.default-structural-symbols");
  require(defaults.policy_version() == "1");
  require(defaults.rules().size() == 8);
  require(defaults.manifest_sha256().size() == 64);

  const std::filesystem::path custom = temp_manifest("_custom");
  write_file(
      custom,
      "# policy_id=experiment.custom-symbols\n"
      "# policy_version=7\n"
      "priority\trule_id\tprovider_scope\tsource_domain\tfield\tmatch\t"
      "pattern\tstructural_symbol\tnote\n"
      "200\texperiment.novel-fused\tascend\ttask\tselected\texact\t"
      "NovelFusedKernel\tFusedBlock\tTest extension\n");
  const StructuralSymbolNormalizationRuleset extension =
      load_structural_symbol_ruleset(custom.string());
  require(extension.policy_id() == "experiment.custom-symbols");
  require(extension.policy_version() == "7");
  require(extension.rules().front().rule_origin == custom.string());
  require(extension.rules().front().source_line == 4);

  const StructuralSymbolNormalizationRuleset composed =
      extend_structural_symbol_ruleset(defaults, extension);
  require(composed.policy_id().find("+") != std::string::npos);
  require(composed.manifest_sha256().size() == 64);

  FlatAnchorBuildConfig config;
  config.structural_symbol_rules = composed;
  NativeIr ascend = build_one_task("ascend_sqlite_hot_path",
                                   "NovelFusedKernel");
  build_flat_anchors(ascend, config);
  require(ascend.symbols.value(ascend.anchors.row(AnchorId(0)).symbol_id) ==
          "FusedBlock");
  require(ascend.anchors.row(AnchorId(0)).symbol_decision.rule_id ==
          "experiment.novel-fused");
  require(ascend.structural_symbol_policy.policy_id == composed.policy_id());
  require(ascend.structural_symbol_policy.manifest_sha256 ==
          composed.manifest_sha256());

  NativeIr cuda = build_one_task("cuda_nsys_sqlite", "NovelFusedKernel");
  build_flat_anchors(cuda, config);
  require(cuda.symbols.value(cuda.anchors.row(AnchorId(0)).symbol_id) ==
          "NovelFusedKernel");
  require(cuda.anchors.row(AnchorId(0)).symbol_decision.rule_id ==
          "fallback.identity-preserve");

  const std::filesystem::path duplicate = temp_manifest("_duplicate");
  write_file(
      duplicate,
      "# policy_id=invalid\n# policy_version=1\n"
      "priority\trule_id\tprovider_scope\tsource_domain\tfield\tmatch\t"
      "pattern\tstructural_symbol\tnote\n"
      "10\tduplicate\tascend\ttask\tselected\texact\tA\tX\tone\n"
      "20\tduplicate\tascend\ttask\tselected\texact\tB\tY\ttwo\n");
  bool rejected_duplicate_id = false;
  try {
    (void)load_structural_symbol_ruleset(duplicate.string());
  } catch (const std::invalid_argument&) {
    rejected_duplicate_id = true;
  }
  require(rejected_duplicate_id);

  const std::filesystem::path conflict = temp_manifest("_conflict");
  write_file(
      conflict,
      "# policy_id=invalid\n# policy_version=1\n"
      "priority\trule_id\tprovider_scope\tsource_domain\tfield\tmatch\t"
      "pattern\tstructural_symbol\tnote\n"
      "10\tone\tascend\ttask\tselected\texact\tA\tX\tone\n"
      "10\ttwo\tascend\ttask\tselected\texact\tA\tY\ttwo\n");
  bool rejected_conflict = false;
  try {
    (void)load_structural_symbol_ruleset(conflict.string());
  } catch (const std::invalid_argument&) {
    rejected_conflict = true;
  }
  require(rejected_conflict);

  const std::filesystem::path runtime_conflict =
      temp_manifest("_runtime_conflict");
  write_file(
      runtime_conflict,
      "# policy_id=ambiguous\n# policy_version=1\n"
      "priority\trule_id\tprovider_scope\tsource_domain\tfield\tmatch\t"
      "pattern\tstructural_symbol\tnote\n"
      "200\tambiguous.contains\tascend\ttask\tselected\tcontains_ci\t"
      "Novel\tFamilyA\tone\n"
      "200\tambiguous.exact\tascend\ttask\tselected\texact\t"
      "NovelFusedKernel\tFamilyB\ttwo\n");
  const StructuralSymbolNormalizationRuleset ambiguous =
      load_structural_symbol_ruleset(runtime_conflict.string());
  FlatAnchorBuildConfig ambiguous_config;
  ambiguous_config.structural_symbol_rules = ambiguous;
  NativeIr ambiguous_ir = build_one_task("ascend_sqlite_hot_path",
                                         "NovelFusedKernel");
  build_flat_anchors(ambiguous_ir, ambiguous_config);
  const AnchorRow& ambiguous_anchor = ambiguous_ir.anchors.row(AnchorId(0));
  require(ambiguous_ir.symbols.value(ambiguous_anchor.symbol_id) ==
          "NovelFusedKernel");
  require(ambiguous_anchor.symbol_decision.outcome ==
          StructuralSymbolOutcome::kConflict);
  require(ambiguous_anchor.symbol_decision.rule_id ==
          "fallback.rule-conflict");
  require(ambiguous_anchor.symbol_decision.candidate_rule_ids ==
          "ambiguous.contains|ambiguous.exact");

  const std::filesystem::path identity_only =
      temp_manifest("_identity_only");
  write_file(
      identity_only,
      "# policy_id=identity-only\n# policy_version=1\n"
      "priority\trule_id\tprovider_scope\tsource_domain\tfield\tmatch\t"
      "pattern\tstructural_symbol\tnote\n");
  FlatAnchorBuildConfig identity_config;
  identity_config.structural_symbol_rules =
      load_structural_symbol_ruleset(identity_only.string());
  require(!identity_config.structural_symbol_rules.empty());
  NativeIr identity_ir = build_one_task("ascend_sqlite_hot_path", "MatMulV2");
  build_flat_anchors(identity_ir, identity_config);
  require(identity_ir.symbols.value(
              identity_ir.anchors.row(AnchorId(0)).symbol_id) == "MatMulV2");
  require(identity_ir.anchors.row(AnchorId(0)).symbol_decision.outcome ==
          StructuralSymbolOutcome::kIdentity);
  require(identity_ir.structural_symbol_policy.policy_id == "identity-only");

  std::filesystem::remove(custom);
  std::filesystem::remove(duplicate);
  std::filesystem::remove(conflict);
  std::filesystem::remove(runtime_conflict);
  std::filesystem::remove(identity_only);
  return 0;
}
