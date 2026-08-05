#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "traceloom/analysis/signal_classification_rules.h"

namespace traceloom {

// Semantic task roles describe what a task means for productive/idle
// analysis (idle evidence contract section 4). They are an independent
// dimension from SignalRole (anchor/ignore) and MUST NOT be conflated with
// it: the same task may be structurally `ignore` and semantically
// `visible_wait` at the same time.
enum class SemanticTaskRole {
  kProductiveCompute,
  kProductiveComm,
  kProductiveDataMove,
  kVisibleWait,
  kCaptureControl,
  kRecord,
  kRuntimeControl,
  kUnknown,
};

// Stable contract strings; never hand-write role names at output sites.
std::string_view semantic_task_role_name(SemanticTaskRole role);

struct SemanticTaskRule {
  std::string rule_id;
  std::int32_t priority = 0;
  std::string source_domain;
  SignalMatchField field = SignalMatchField::kBlob;
  SignalMatchKind match = SignalMatchKind::kContains;
  std::string pattern;
  SemanticTaskRole role = SemanticTaskRole::kUnknown;
  std::string note;
  std::size_t source_line = 0;
};

struct SemanticTaskClassificationInput {
  std::string source_domain;
  std::string task_type;
  std::string blob;
  std::string operator_name;
};

struct SemanticTaskMatch {
  SemanticTaskRole role = SemanticTaskRole::kUnknown;
  std::optional<std::string> matched_rule_id;
  std::int32_t matched_priority = 0;
  std::optional<SignalMatchField> matched_field;
  std::optional<SignalMatchKind> matched_kind;
};

class SemanticTaskRuleset {
 public:
  SemanticTaskRuleset(std::vector<SemanticTaskRule> rules,
                      std::string version,
                      std::string sha256);

  const std::vector<SemanticTaskRule>& rules() const { return rules_; }
  const std::string& version() const { return version_; }
  const std::string& sha256() const { return sha256_; }

  // Returns the highest-priority matching rule. When several rules share the
  // highest priority, matching roles pick the first file occurrence and
  // conflicting roles raise std::invalid_argument (never a silent file-order
  // decision). No match returns kUnknown with an empty rule id.
  SemanticTaskMatch classify(
      const SemanticTaskClassificationInput& input) const;

 private:
  std::vector<SemanticTaskRule> rules_;
  std::string version_;
  std::string sha256_;
};

// Loads a ruleset from a tab-separated file. The file MUST declare
// `# ruleset_version: <version>` before the header. sha256 is computed over
// the raw file bytes.
SemanticTaskRuleset load_idle_evidence_semantic_ruleset(
    const std::string& path);
SemanticTaskRuleset load_default_idle_evidence_semantic_ruleset(
    const std::string& executable_path = "");

}  // namespace traceloom
