#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace traceloom {

enum class SignalRole {
  kAnchor,
  kIgnore,
};

enum class SignalMatchField {
  kTaskType,
  kBlob,
  kOperator,
};

enum class SignalMatchKind {
  kExact,
  kContains,
};

struct SignalClassificationRule {
  std::int32_t priority = 0;
  std::string source_domain;
  SignalMatchField field = SignalMatchField::kBlob;
  SignalMatchKind match = SignalMatchKind::kContains;
  std::string pattern;
  SignalRole role = SignalRole::kIgnore;
  std::string note;
  std::size_t source_line = 0;
};

struct SignalClassificationInput {
  std::string source_domain;
  std::string task_type;
  std::string blob;
  std::string operator_name;
};

class SignalClassificationRuleset {
 public:
  SignalClassificationRuleset() = default;
  explicit SignalClassificationRuleset(
      std::vector<SignalClassificationRule> rules);

  const std::vector<SignalClassificationRule>& rules() const { return rules_; }
  std::optional<SignalRole> classify(
      const SignalClassificationInput& input) const;

 private:
  std::vector<SignalClassificationRule> rules_;
};

SignalClassificationRuleset load_signal_classification_ruleset(
    const std::string& path);
SignalClassificationRuleset load_default_signal_classification_ruleset(
    const std::string& executable_path = "");
SignalClassificationRuleset extend_signal_classification_ruleset(
    const SignalClassificationRuleset& base,
    const SignalClassificationRuleset& extension);

}  // namespace traceloom
