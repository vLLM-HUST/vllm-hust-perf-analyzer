#include "traceloom/analysis/idle_evidence_semantic_rules.h"
#include "traceloom/analysis/signal_classification_rules.h"
#include "traceloom/testing/test_util.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

void write_ruleset(const std::filesystem::path& path,
                   const std::string& version_line,
                   const std::string& body) {
  std::ofstream out(path);
  out << version_line << "\n"
      << "rule_id\tpriority\tsource_domain\tfield\tmatch\tpattern\trole\tnote\n"
      << body;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  // ---- Default ruleset: version, sha256, and role mappings. ----
  const SemanticTaskRuleset defaults =
      load_default_idle_evidence_semantic_ruleset();
  require(defaults.version() == "idle-evidence-semantic-v1",
          "default ruleset version");
  require(defaults.sha256().size() == 64, "default ruleset sha256 present");
  require(defaults.rules().size() >= 14, "default ruleset has initial rules");

  const auto classify = [&defaults](const std::string& task_type,
                                    const std::string& blob) {
    return defaults.classify(
        SemanticTaskClassificationInput{"task", task_type, blob, blob});
  };

  require(classify("EVENT_WAIT", "event_wait").role ==
              SemanticTaskRole::kVisibleWait,
          "EVENT_WAIT -> visible_wait");
  require(classify("NOTIFY_WAIT", "notify_wait").role ==
              SemanticTaskRole::kVisibleWait,
          "NOTIFY_WAIT -> visible_wait");
  require(classify("MEM_WAIT_VALUE", "mem_wait_value").role ==
              SemanticTaskRole::kVisibleWait,
          "MEM_WAIT_VALUE -> visible_wait");
  require(classify("NOTIFY_RECORD", "notify_record").role ==
              SemanticTaskRole::kRecord,
          "NOTIFY_RECORD -> record");
  require(classify("NOTIFY_RECORD_SQE", "notify_record_sqe").role ==
              SemanticTaskRole::kRecord,
          "NOTIFY_RECORD_SQE -> record");
  require(classify("MEM_WRITE_VALUE", "mem_write_value").role ==
              SemanticTaskRole::kRuntimeControl,
          "MEM_WRITE_VALUE -> runtime_control");
  require(classify("PLACE_HOLDER_SQE", "place_holder_sqe").role ==
              SemanticTaskRole::kRuntimeControl,
          "PLACE_HOLDER_SQE -> runtime_control");
  require(classify("NOP", "nop").role ==
              SemanticTaskRole::kRuntimeControl,
          "NOP -> runtime_control");
  require(classify("CAPTURE_WAIT", "capture_wait").role ==
              SemanticTaskRole::kCaptureControl,
          "CAPTURE_WAIT -> capture_control");
  require(classify("CAPTURE_RECORD", "capture_record").role ==
              SemanticTaskRole::kCaptureControl,
          "CAPTURE_RECORD -> capture_control");
  require(classify("EVENT_RECORD", "event_record").role ==
              SemanticTaskRole::kRecord,
          "EVENT_RECORD -> record");
  require(classify("MEMCPY", "memcpy").role ==
              SemanticTaskRole::kProductiveDataMove,
          "MEMCPY -> productive_data_move");
  require(classify("MEMCPY_ASYNC", "memcpy_async").role ==
              SemanticTaskRole::kProductiveDataMove,
          "MEMCPY_ASYNC -> productive_data_move");
  require(classify("SDMA", "sdma").role ==
              SemanticTaskRole::kProductiveDataMove,
          "SDMA -> productive_data_move");
  require(classify("WRITE_VALUE", "write_value").role ==
              SemanticTaskRole::kRuntimeControl,
          "WRITE_VALUE -> runtime_control");
  require(classify("PROFILING_DISABLE", "profiling_disable").role ==
              SemanticTaskRole::kRuntimeControl,
          "PROFILING_DISABLE -> runtime_control");
  require(classify("AI_CORE", "MatMulV2").role ==
              SemanticTaskRole::kProductiveCompute,
          "MatMul blob -> productive_compute");
  const SemanticTaskMatch fused_moe =
      classify("AI_CORE", "DispatchFFNCombineBF16");
  require(fused_moe.role == SemanticTaskRole::kProductiveCompute &&
              fused_moe.matched_rule_id.has_value() &&
              *fused_moe.matched_rule_id ==
                  "compute.dispatch_ffn_combine" &&
              fused_moe.matched_field == SignalMatchField::kOperator &&
              fused_moe.matched_kind == SignalMatchKind::kExact,
          "fused MoE kernel uses its dedicated semantic rule");
  const SemanticTaskMatch mapped_gather =
      classify("AI_CORE", "KvCacheBlockGather");
  require(mapped_gather.role == SemanticTaskRole::kProductiveCompute &&
              mapped_gather.matched_rule_id.has_value() &&
              *mapped_gather.matched_rule_id ==
                  "compute.kv_cache_block_gather" &&
              mapped_gather.matched_field == SignalMatchField::kOperator &&
              mapped_gather.matched_kind == SignalMatchKind::kExact,
          "mapped gather kernel uses its dedicated semantic rule");
  require(classify("AI_CORE", "allreduce").role ==
              SemanticTaskRole::kProductiveComm,
          "allreduce blob -> productive_comm");
  require(classify("UNKNOWN_FUTURE_TASK", "totally_new_kernel").role ==
              SemanticTaskRole::kUnknown,
          "no match -> unknown");
  require(!classify("UNKNOWN_FUTURE_TASK", "totally_new_kernel")
               .matched_rule_id.has_value(),
          "no match -> empty rule id");

  // ---- matched_rule_id precision. ----
  const SemanticTaskMatch wait_match = classify("EVENT_WAIT", "event_wait");
  require(wait_match.matched_rule_id.has_value() &&
              *wait_match.matched_rule_id == "wait.event_wait",
          "matched_rule_id = wait.event_wait");

  // ---- Priority: exact task_type outranks blob keywords. ----
  const SemanticTaskMatch priority_match = classify("EVENT_WAIT", "memcpy wait");
  require(priority_match.role == SemanticTaskRole::kVisibleWait,
          "exact task_type wins over blob keyword");

  // ---- Independence from the anchor classification rules. ----
  const SignalClassificationRuleset anchor_rules =
      load_default_signal_classification_ruleset();
  require(anchor_rules.classify({"task", "EVENT_WAIT", "event_wait"}) ==
              SignalRole::kIgnore,
          "anchor rules still classify EVENT_WAIT as ignore");
  require(anchor_rules.classify({"task", "AI_CORE", "MatMulV2"}) ==
              SignalRole::kIgnore,
          "anchor rules still classify bare AI_CORE as ignore");

  // ---- Parser validation: duplicate rule_id. ----
  const std::filesystem::path dup =
      std::filesystem::temp_directory_path() / "traceloom-idle-rules-dup.tsv";
  write_ruleset(dup, "# ruleset_version: test-v1",
                "a.rule\t200\ttask\ttask_type\texact\tX\tvisible_wait\tone\n"
                "a.rule\t100\ttask\ttask_type\texact\tY\trecord\ttwo\n");
  bool rejected = false;
  try {
    (void)load_idle_evidence_semantic_ruleset(dup.string());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "duplicate rule_id rejected");
  std::filesystem::remove(dup);

  // ---- Parser validation: missing ruleset_version. ----
  const std::filesystem::path no_version =
      std::filesystem::temp_directory_path() / "traceloom-idle-rules-noversion.tsv";
  write_ruleset(no_version, "# just a comment",
                "a.rule\t200\ttask\ttask_type\texact\tX\tvisible_wait\tone\n");
  rejected = false;
  try {
    (void)load_idle_evidence_semantic_ruleset(no_version.string());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "missing ruleset_version rejected");
  std::filesystem::remove(no_version);

  // ---- Parser validation: illegal role / field / match / priority. ----
  {
    const std::filesystem::path bad =
        std::filesystem::temp_directory_path() / "traceloom-idle-rules-bad.tsv";
    write_ruleset(bad, "# ruleset_version: test-v1",
                  "a.rule\t200\ttask\ttask_type\texact\tX\tnot_a_role\tnote\n");
    rejected = false;
    try {
      (void)load_idle_evidence_semantic_ruleset(bad.string());
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "illegal role rejected");
    std::filesystem::remove(bad);
  }
  {
    const std::filesystem::path bad =
        std::filesystem::temp_directory_path() / "traceloom-idle-rules-badfield.tsv";
    write_ruleset(bad, "# ruleset_version: test-v1",
                  "a.rule\t200\ttask\tnot_a_field\texact\tX\tvisible_wait\tn\n");
    rejected = false;
    try {
      (void)load_idle_evidence_semantic_ruleset(bad.string());
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "illegal field rejected");
    std::filesystem::remove(bad);
  }
  {
    const std::filesystem::path bad =
        std::filesystem::temp_directory_path() / "traceloom-idle-rules-badpriority.tsv";
    write_ruleset(bad, "# ruleset_version: test-v1",
                  "a.rule\tnope\ttask\ttask_type\texact\tX\tvisible_wait\tn\n");
    rejected = false;
    try {
      (void)load_idle_evidence_semantic_ruleset(bad.string());
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "illegal priority rejected");
    std::filesystem::remove(bad);
  }
  {
    const std::filesystem::path bad =
        std::filesystem::temp_directory_path() / "traceloom-idle-rules-short.tsv";
    write_ruleset(bad, "# ruleset_version: test-v1",
                  "a.rule\t200\ttask\ttask_type\texact\tX\tvisible_wait\n");
    rejected = false;
    try {
      (void)load_idle_evidence_semantic_ruleset(bad.string());
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "missing column rejected");
    std::filesystem::remove(bad);
  }
  {
    const std::filesystem::path bad =
        std::filesystem::temp_directory_path() / "traceloom-idle-rules-emptypattern.tsv";
    write_ruleset(bad, "# ruleset_version: test-v1",
                  "a.rule\t200\ttask\ttask_type\texact\t\tvisible_wait\tn\n");
    rejected = false;
    try {
      (void)load_idle_evidence_semantic_ruleset(bad.string());
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "empty pattern rejected");
    std::filesystem::remove(bad);
  }

  // ---- Same-priority role conflict raises at classify time. ----
  const std::filesystem::path conflict =
      std::filesystem::temp_directory_path() / "traceloom-idle-rules-conflict.tsv";
  write_ruleset(conflict, "# ruleset_version: test-v1",
                "x.compute\t100\ttask\tblob\tcontains\tmatmul\tproductive_compute\ta\n"
                "x.control\t100\ttask\tblob\tcontains\tmat\tvisible_wait\tb\n");
  const SemanticTaskRuleset conflicting =
      load_idle_evidence_semantic_ruleset(conflict.string());
  rejected = false;
  try {
    (void)conflicting.classify(
        SemanticTaskClassificationInput{"task", "AI_CORE", "matmul kernel"});
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "same-priority role conflict rejected");
  std::filesystem::remove(conflict);

  // ---- Same-priority same-role picks the first file occurrence. ----
  const std::filesystem::path same =
      std::filesystem::temp_directory_path() / "traceloom-idle-rules-same.tsv";
  write_ruleset(same, "# ruleset_version: test-v1",
                "x.first\t100\ttask\tblob\tcontains\tmatmul\tproductive_compute\ta\n"
                "x.second\t100\ttask\tblob\tcontains\tmat\tproductive_compute\tb\n");
  const SemanticTaskRuleset same_priority =
      load_idle_evidence_semantic_ruleset(same.string());
  const SemanticTaskMatch same_match = same_priority.classify(
      SemanticTaskClassificationInput{"task", "AI_CORE", "matmul kernel"});
  require(same_match.matched_rule_id.has_value() &&
              *same_match.matched_rule_id == "x.first",
          "same-priority same-role picks first occurrence");
  std::filesystem::remove(same);

  // ---- Role name strings are the frozen contract vocabulary. ----
  require(std::string(semantic_task_role_name(
              SemanticTaskRole::kProductiveCompute)) == "productive_compute",
          "role name productive_compute");
  require(std::string(semantic_task_role_name(
              SemanticTaskRole::kUnknown)) == "unknown",
          "role name unknown");

  // ---- Table-driven: every default rule has at least one hit and wins on
  // its own representative input. ----
  for (const SemanticTaskRule& rule : defaults.rules()) {
    SemanticTaskClassificationInput probe;
    probe.source_domain = "task";
    if (rule.field == SignalMatchField::kTaskType) {
      probe.task_type = rule.pattern;
    } else if (rule.field == SignalMatchField::kBlob) {
      probe.blob = rule.pattern;
    } else {
      probe.operator_name = rule.pattern;
    }
    const SemanticTaskMatch hit = defaults.classify(probe);
    require(hit.matched_rule_id.has_value(),
            (std::string("default ruleset: every rule must have a hit: ") +
             rule.rule_id)
                .c_str());
    require(*hit.matched_rule_id == rule.rule_id,
            (std::string("default ruleset: rule wins on its own input: ") +
             rule.rule_id)
                .c_str());
  }

  // ---- Parser validation: illegal match value. ----
  {
    const std::filesystem::path bad =
        std::filesystem::temp_directory_path() / "traceloom-idle-rules-badmatch.tsv";
    write_ruleset(bad, "# ruleset_version: test-v1",
                  "a.rule\t200\ttask\ttask_type\tnot_a_match\tX\tvisible_wait\tn\n");
    rejected = false;
    try {
      (void)load_idle_evidence_semantic_ruleset(bad.string());
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "illegal match value rejected");
    std::filesystem::remove(bad);
  }

  // ---- Parser validation: priority outside int32 range. ----
  {
    const std::filesystem::path bad =
        std::filesystem::temp_directory_path() / "traceloom-idle-rules-hugepriority.tsv";
    write_ruleset(bad, "# ruleset_version: test-v1",
                  "a.rule\t99999999999\ttask\ttask_type\texact\tX\tvisible_wait\tn\n");
    rejected = false;
    try {
      (void)load_idle_evidence_semantic_ruleset(bad.string());
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "priority outside int32 range rejected");
    std::filesystem::remove(bad);
  }

  // ---- Parser validation: duplicate ruleset_version. ----
  {
    const std::filesystem::path bad =
        std::filesystem::temp_directory_path() / "traceloom-idle-rules-dupversion.tsv";
    write_ruleset(bad, "# ruleset_version: test-v1\n# ruleset_version: test-v2",
                  "a.rule\t200\ttask\ttask_type\texact\tX\tvisible_wait\tone\n");
    rejected = false;
    try {
      (void)load_idle_evidence_semantic_ruleset(bad.string());
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "duplicate ruleset_version rejected");
    std::filesystem::remove(bad);
  }

  // ---- Parser validation: ruleset_version after the TSV header. ----
  {
    const std::filesystem::path bad =
        std::filesystem::temp_directory_path() / "traceloom-idle-rules-lateversion.tsv";
    write_ruleset(bad, "# plain comment",
                  "a.rule\t200\ttask\ttask_type\texact\tX\tvisible_wait\tone\n"
                  "# ruleset_version: test-v1\n");
    rejected = false;
    try {
      (void)load_idle_evidence_semantic_ruleset(bad.string());
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "ruleset_version after header rejected");
    std::filesystem::remove(bad);
  }

  // ---- Parser validation: normalized duplicate task_type patterns. ----
  {
    const std::filesystem::path bad =
        std::filesystem::temp_directory_path() / "traceloom-idle-rules-normalizeddup.tsv";
    write_ruleset(bad, "# ruleset_version: test-v1",
                  "a.one\t200\ttask\ttask_type\texact\tEVENT-WAIT\tvisible_wait\tone\n"
                  "a.two\t200\ttask\ttask_type\texact\tEVENT_WAIT\tvisible_wait\ttwo\n");
    rejected = false;
    try {
      (void)load_idle_evidence_semantic_ruleset(bad.string());
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "normalized duplicate task_type patterns rejected");
    std::filesystem::remove(bad);
  }

  // ---- Non-version comment lines are ignored (exact prefix required). ----
  {
    const std::filesystem::path ok =
        std::filesystem::temp_directory_path() / "traceloom-idle-rules-prefixok.tsv";
    write_ruleset(ok, "# not_a_ruleset_version: v1\n# ruleset_version: test-v1",
                  "a.rule\t200\ttask\ttask_type\texact\tX\tvisible_wait\tone\n");
    const SemanticTaskRuleset loaded =
        load_idle_evidence_semantic_ruleset(ok.string());
    require(loaded.version() == "test-v1",
            "non-version comment line ignored, exact prefix enforced");
    std::filesystem::remove(ok);
  }

  // ---- CRLF: loadable but different raw-byte SHA-256 than LF. ----
  {
    const std::filesystem::path lf_path =
        std::filesystem::temp_directory_path() / "traceloom-idle-rules-lf.tsv";
    const std::filesystem::path crlf_path =
        std::filesystem::temp_directory_path() / "traceloom-idle-rules-crlf.tsv";
    const std::string body =
        "rule_id\tpriority\tsource_domain\tfield\tmatch\tpattern\trole\tnote\n"
        "a.rule\t200\ttask\ttask_type\texact\tX\tvisible_wait\tone\n";
    {
      std::ofstream out(lf_path);
      out << "# ruleset_version: test-v1\n" << body;
    }
    {
      std::ofstream out(crlf_path, std::ios::binary);
      std::string crlf = "# ruleset_version: test-v1\r\n";
      for (const char ch : body) {
        if (ch == '\n') {
          crlf += "\r\n";
        } else {
          crlf += ch;
        }
      }
      out << crlf;
    }
    const SemanticTaskRuleset lf_rules =
        load_idle_evidence_semantic_ruleset(lf_path.string());
    const SemanticTaskRuleset crlf_rules =
        load_idle_evidence_semantic_ruleset(crlf_path.string());
    require(lf_rules.sha256() != crlf_rules.sha256(),
            "CRLF changes raw-byte sha256");
    require(crlf_rules.rules().size() == lf_rules.rules().size(),
            "CRLF ruleset still parses");
    std::filesystem::remove(lf_path);
    std::filesystem::remove(crlf_path);
  }
  return 0;
}
