#pragma once

#include "traceloom/analysis/signal_classification_rules.h"

#include <string>

namespace traceloom::signal_classification_detail {

inline constexpr char kManifestSchema[] =
    "traceloom.evidence-role-policy/v1";

std::string trim(std::string value);
std::string canonical_rule_key(const SignalClassificationRule& rule);
void validate_metadata(const SignalClassificationPolicyMetadata& metadata);

}  // namespace traceloom::signal_classification_detail
