#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace traceloom {

enum class HostApiFamily {
  kHostSync,
  kEnqueue,
};

std::string_view host_api_family_name(HostApiFamily family);

struct HostApiRule {
  std::string api_pattern;
  HostApiFamily family = HostApiFamily::kHostSync;
  std::string note;
  std::size_t source_line = 0;
};

struct HostApiMatch {
  HostApiFamily family = HostApiFamily::kHostSync;
  std::string api_pattern;
};

class HostApiRuleset {
 public:
  HostApiRuleset(std::vector<HostApiRule> rules,
                 std::string version,
                 std::string sha256);

  const std::vector<HostApiRule>& rules() const { return rules_; }
  const std::string& version() const { return version_; }
  const std::string& sha256() const { return sha256_; }
  std::optional<HostApiMatch> classify(std::string_view api_name) const;

 private:
  std::vector<HostApiRule> rules_;
  std::string version_;
  std::string sha256_;
};

HostApiRuleset load_idle_evidence_host_api_ruleset(const std::string& path);
HostApiRuleset load_default_idle_evidence_host_api_ruleset(
    const std::string& executable_path = "");

}  // namespace traceloom
