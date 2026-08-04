#include "traceloom/analysis/signal_classification_rules.h"
#include "traceloom/testing/test_util.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  const SignalClassificationRuleset defaults =
      load_default_signal_classification_ruleset();
  require(defaults.classify({"task", "AI_CORE", "MatMulV2"}) ==
          SignalRole::kIgnore);
  require(defaults.classify({"task", "AICORE_KERNEL", "MatMulV2"}) ==
          SignalRole::kAnchor);
  require(defaults.classify({"task", "CUDA_KERNEL", "unknown_kernel"}) ==
          SignalRole::kAnchor);
  require(defaults.classify({"task", "EVENT_WAIT", "event_wait"}) ==
          SignalRole::kIgnore);
  require(defaults.classify(
              {"task", "NOTIFY_RECORD_SQE", "notify_record_sqe"}) ==
          SignalRole::kIgnore);
  require(defaults.classify(
              {"task", "PLACE_HOLDER_SQE", "place_holder_sqe"}) ==
          SignalRole::kIgnore);
  require(defaults.classify({"task", "MEM_WAIT_VALUE", "mem_wait_value"}) ==
          SignalRole::kIgnore);
  require(defaults.classify({"task", "NOP", "nop"}) == SignalRole::kIgnore);
  require(!defaults.classify({"task", "UNKNOWN", "unknown"}).has_value());

  const std::filesystem::path fixture =
      std::filesystem::temp_directory_path() / "traceloom-rules-test.tsv";
  {
    std::ofstream out(fixture);
    out << "priority\tsource_domain\tfield\tmatch\tpattern\trole\tnote\n"
        << "200\ttask\tblob\tcontains\tcustom_kernel\tanchor\ttest override\n";
  }
  const SignalClassificationRuleset extension =
      load_signal_classification_ruleset(fixture.string());
  const SignalClassificationRuleset merged =
      extend_signal_classification_ruleset(defaults, extension);
  require(merged.classify({"task", "UNKNOWN", "custom_kernel"}) ==
          SignalRole::kAnchor);
  std::filesystem::remove(fixture);

  const std::filesystem::path invalid =
      std::filesystem::temp_directory_path() / "traceloom-rules-invalid.tsv";
  {
    std::ofstream out(invalid);
    out << "priority\tsource_domain\tfield\tmatch\tpattern\trole\tnote\n"
        << "10\ttask\tunknown\texact\tx\tanchor\tbad field\n";
  }
  bool rejected = false;
  try {
    (void)load_signal_classification_ruleset(invalid.string());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected);
  std::filesystem::remove(invalid);
  return 0;
}
