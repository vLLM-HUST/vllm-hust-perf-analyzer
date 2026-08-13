#include "traceloom/analysis/event_reconciliation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "traceloom/core/sha256.h"
#include "traceloom/ir/native_ir.h"

#ifndef TRACELOOM_SOURCE_DEFAULT_EVENT_RECONCILIATION_RULESET_PATH
#define TRACELOOM_SOURCE_DEFAULT_EVENT_RECONCILIATION_RULESET_PATH ""
#endif

#ifndef TRACELOOM_INSTALL_DEFAULT_EVENT_RECONCILIATION_RULESET_PATH
#define TRACELOOM_INSTALL_DEFAULT_EVENT_RECONCILIATION_RULESET_PATH ""
#endif

namespace traceloom {
namespace {

constexpr const char* kManifestSchema =
    "traceloom.event-reconciliation-policy/v1";

std::string trim(std::string value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

std::string lower_ascii(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

std::vector<std::string> split_tsv(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    const std::size_t end = line.find('\t', start);
    fields.push_back(trim(line.substr(start, end - start)));
    if (end == std::string::npos) return fields;
    start = end + 1;
  }
}

std::string metadata_value(const std::vector<std::string>& lines,
                           const std::string& key) {
  const std::string prefix = "# " + key + "=";
  const std::string compact_prefix = "#" + key + "=";
  for (const std::string& raw : lines) {
    const std::string line = trim(raw);
    if (line.rfind(prefix, 0) == 0) return trim(line.substr(prefix.size()));
    if (line.rfind(compact_prefix, 0) == 0) {
      return trim(line.substr(compact_prefix.size()));
    }
  }
  throw std::invalid_argument(
      "event-reconciliation manifest missing metadata: " + key);
}

std::int64_t parse_i64(const std::string& value,
                       const std::string& field,
                       std::uint64_t line) {
  std::size_t consumed = 0;
  const long long parsed = std::stoll(value, &consumed, 10);
  if (consumed != value.size()) {
    throw std::invalid_argument("invalid " + field + " at line " +
                                std::to_string(line));
  }
  return static_cast<std::int64_t>(parsed);
}

std::int32_t parse_priority(const std::string& value, std::uint64_t line) {
  const std::int64_t parsed = parse_i64(value, "priority", line);
  if (parsed < std::numeric_limits<std::int32_t>::min() ||
      parsed > std::numeric_limits<std::int32_t>::max()) {
    throw std::invalid_argument("priority out of range at line " +
                                std::to_string(line));
  }
  return static_cast<std::int32_t>(parsed);
}

double parse_fraction(const std::string& value, std::uint64_t line) {
  std::size_t consumed = 0;
  const double fraction = std::stod(value, &consumed);
  if (consumed != value.size() || !std::isfinite(fraction) || fraction < 0.0 ||
      fraction > 1.0) {
    throw std::invalid_argument(
        "min_contained_fraction must be in [0,1] at line " +
        std::to_string(line));
  }
  return fraction;
}

void validate_rules(const std::vector<EventReconciliationRule>& rules) {
  std::unordered_set<std::string> ids;
  for (const auto& rule : rules) {
    if (rule.rule_id.empty() || rule.provider_scope.empty() ||
        rule.source_domain != "task" || rule.task_type.empty()) {
      throw std::invalid_argument(
          "event-reconciliation rule has invalid required fields at line " +
          std::to_string(rule.source_line));
    }
    if (rule.provider_scope != "ascend" && rule.provider_scope != "any") {
      throw std::invalid_argument(
          "unsupported event-reconciliation provider_scope at line " +
          std::to_string(rule.source_line));
    }
    if (rule.generic_context_id == rule.concrete_context_id) {
      throw std::invalid_argument(
          "event-reconciliation contexts must differ at line " +
          std::to_string(rule.source_line));
    }
    if (!ids.insert(rule.rule_id).second) {
      throw std::invalid_argument(
          "duplicate event-reconciliation rule_id: " + rule.rule_id);
    }
  }
}

std::string provider_scope(const NativeIr& ir, const TaskRow& task) {
  if (!task.source_ref_id.valid() ||
      task.source_ref_id.value() >= ir.source_refs.size()) {
    return "any";
  }
  const std::string kind =
      lower_ascii(ir.source_refs.row(task.source_ref_id).source_kind);
  if (kind.find("ascend") != std::string::npos ||
      kind.find("cann") != std::string::npos) {
    return "ascend";
  }
  if (kind.find("cuda") != std::string::npos ||
      kind.find("nsys") != std::string::npos) {
    return "cuda";
  }
  if (kind.find("hygon") != std::string::npos ||
      kind.find("hip") != std::string::npos) {
    return "hygon";
  }
  return "any";
}

std::string symbol_text(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

bool has_operator_detail(const TaskRow& task) {
  return task.op_name_symbol_id.valid() || task.op_type_symbol_id.valid() ||
         task.compute_task_type_symbol_id.valid() ||
         task.comm_name_symbol_id.valid();
}

const EventReconciliationRule* matching_rule(
    const NativeIr& ir,
    const TaskRow& task,
    const EventReconciliationRuleset& ruleset) {
  const std::string provider = provider_scope(ir, task);
  const std::string task_type = symbol_text(ir, task.task_type_symbol_id);
  for (const auto& rule : ruleset.rules()) {
    if ((rule.provider_scope == "any" || rule.provider_scope == provider) &&
        rule.source_domain == "task" && rule.task_type == task_type) {
      return &rule;
    }
  }
  return nullptr;
}

struct GroupKey {
  std::string rule_id;
  SourceRefId::value_type source_ref_id = 0;
  std::uint32_t device_id = 0;
  std::uint32_t stream_id = 0;
  std::uint64_t raw_task_id = 0;
  std::int64_t raw_connection_id = -1;
  std::string task_type;

  auto tie() const {
    return std::tie(rule_id, source_ref_id, device_id, stream_id, raw_task_id,
                    raw_connection_id, task_type);
  }
  bool operator<(const GroupKey& other) const { return tie() < other.tie(); }
};

struct Group {
  const EventReconciliationRule* rule = nullptr;
  std::vector<const TaskRow*> tasks;
};

double contained_fraction(const TraceEventRow& outer,
                          const TraceEventRow& inner) {
  const std::int64_t inner_duration =
      std::max<std::int64_t>(0, inner.end_ns - inner.start_ns);
  if (inner_duration == 0) {
    return outer.start_ns <= inner.start_ns && outer.end_ns >= inner.end_ns
               ? 1.0
               : 0.0;
  }
  const std::int64_t intersection = std::max<std::int64_t>(
      0, std::min(outer.end_ns, inner.end_ns) -
             std::max(outer.start_ns, inner.start_ns));
  return static_cast<double>(intersection) /
         static_cast<double>(inner_duration);
}

std::vector<ReplayUnitId::value_type> replay_membership(
    const NativeIr& ir,
    const TraceEventRow& event) {
  std::vector<ReplayUnitId::value_type> ids;
  for (const ReplayUnitRow& replay : ir.replay_units.rows()) {
    if (!replay.launch_trace_event_id.valid()) continue;
    const TraceEventRow& envelope =
        ir.trace_events.row(replay.launch_trace_event_id);
    if (event.device_id == envelope.device_id &&
        event.start_ns >= envelope.start_ns && event.end_ns <= envelope.end_ns) {
      ids.push_back(replay.id.value());
    }
  }
  return ids;
}

void append_members(EventReconciliationState& state,
                    EventReconciliationDecisionId decision_id,
                    const std::vector<const TaskRow*>& tasks,
                    EventReconciliationMemberRole role) {
  for (const TaskRow* task : tasks) {
    state.members.push_back({decision_id, task->id, task->trace_event_id, role,
                             false, false, false, true});
  }
}

}  // namespace

const char* event_reconciliation_status_name(
    EventReconciliationStatus status) {
  switch (status) {
    case EventReconciliationStatus::kReconciled: return "reconciled";
    case EventReconciliationStatus::kIndependent: return "independent";
    case EventReconciliationStatus::kAmbiguous: return "ambiguous";
    case EventReconciliationStatus::kConflict: return "conflict";
  }
  return "conflict";
}

const char* event_reconciliation_member_role_name(
    EventReconciliationMemberRole role) {
  switch (role) {
    case EventReconciliationMemberRole::kTimingEnvelope:
      return "timing_envelope";
    case EventReconciliationMemberRole::kSemanticDetail:
      return "semantic_detail";
    case EventReconciliationMemberRole::kIndependentCandidate:
      return "independent_candidate";
    case EventReconciliationMemberRole::kConflictingCandidate:
      return "conflicting_candidate";
  }
  return "conflicting_candidate";
}

EventReconciliationRuleset::EventReconciliationRuleset(
    std::string manifest_schema,
    std::string policy_id,
    std::string policy_version,
    std::string source_manifest,
    std::string manifest_sha256,
    std::string unmatched_behavior,
    std::vector<EventReconciliationRule> rules)
    : manifest_schema_(std::move(manifest_schema)),
      policy_id_(std::move(policy_id)),
      policy_version_(std::move(policy_version)),
      source_manifest_(std::move(source_manifest)),
      manifest_sha256_(std::move(manifest_sha256)),
      unmatched_behavior_(std::move(unmatched_behavior)),
      rules_(std::move(rules)) {
  if (manifest_schema_ != kManifestSchema || policy_id_.empty() ||
      policy_version_.empty() || unmatched_behavior_ != "independent") {
    throw std::invalid_argument(
        "invalid event-reconciliation policy metadata");
  }
  validate_rules(rules_);
  std::stable_sort(rules_.begin(), rules_.end(),
                   [](const auto& lhs, const auto& rhs) {
                     return lhs.priority > rhs.priority;
                   });
}

EventReconciliationPolicySnapshot EventReconciliationRuleset::snapshot()
    const {
  EventReconciliationPolicySnapshot out;
  out.manifest_schema = manifest_schema_;
  out.policy_id = policy_id_;
  out.policy_version = policy_version_;
  out.source_manifest = source_manifest_;
  out.manifest_sha256 = manifest_sha256_;
  out.unmatched_behavior = unmatched_behavior_;
  for (const auto& rule : rules_) {
    out.rules.push_back({rule.rule_id,
                         rule.priority,
                         rule.provider_scope,
                         rule.source_domain,
                         rule.task_type,
                         rule.generic_context_id,
                         rule.concrete_context_id,
                         rule.min_contained_fraction,
                         rule.rule_origin,
                         rule.rule_origin_sha256,
                         rule.source_line,
                         rule.note});
  }
  return out;
}

EventReconciliationRuleset load_event_reconciliation_ruleset(
    const std::string& path) {
  std::ifstream stream(path);
  if (!stream) {
    throw std::invalid_argument(
        "cannot open event-reconciliation manifest: " + path);
  }
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(stream, line)) lines.push_back(line);

  const std::vector<std::string> expected{
      "priority", "rule_id", "provider_scope", "source_domain", "task_type",
      "generic_context_id", "concrete_context_id",
      "min_contained_fraction", "note"};
  bool saw_header = false;
  std::vector<EventReconciliationRule> rules;
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const std::string stripped = trim(lines[index]);
    if (stripped.empty() || stripped.front() == '#') continue;
    const auto fields = split_tsv(lines[index]);
    if (!saw_header) {
      if (fields != expected) {
        throw std::invalid_argument(
            "invalid event-reconciliation manifest header at line " +
            std::to_string(index + 1));
      }
      saw_header = true;
      continue;
    }
    if (fields.size() != expected.size()) {
      throw std::invalid_argument(
          "event-reconciliation rule must contain nine fields at line " +
          std::to_string(index + 1));
    }
    const auto source_line = static_cast<std::uint64_t>(index + 1);
    rules.push_back({parse_priority(fields[0], source_line),
                     fields[1],
                     fields[2],
                     fields[3],
                     fields[4],
                     parse_i64(fields[5], "generic_context_id", source_line),
                     parse_i64(fields[6], "concrete_context_id", source_line),
                     parse_fraction(fields[7], source_line),
                     fields[8],
                     path,
                     "",
                     source_line});
  }
  if (!saw_header) {
    throw std::invalid_argument(
        "event-reconciliation manifest is empty: " + path);
  }
  const std::string sha = sha256_file_hex(path);
  for (auto& rule : rules) rule.rule_origin_sha256 = sha;
  return {metadata_value(lines, "manifest_schema"),
          metadata_value(lines, "policy_id"),
          metadata_value(lines, "policy_version"),
          path,
          sha,
          metadata_value(lines, "unmatched_behavior"),
          std::move(rules)};
}

EventReconciliationRuleset load_default_event_reconciliation_ruleset(
    const std::string& executable_path) {
  const char* environment = std::getenv("TRACELOOM_EVENT_RECONCILIATION_RULES");
  if (environment != nullptr && *environment != '\0') {
    return load_event_reconciliation_ruleset(environment);
  }
  std::vector<std::string> candidates;
  if (!executable_path.empty()) {
    const auto executable = std::filesystem::absolute(executable_path);
    candidates.push_back(
        (executable.parent_path().parent_path() / "share" / "traceloom" /
         "default_event_reconciliation_rules.tsv")
            .string());
  }
  candidates.push_back(
      TRACELOOM_SOURCE_DEFAULT_EVENT_RECONCILIATION_RULESET_PATH);
  candidates.push_back(
      TRACELOOM_INSTALL_DEFAULT_EVENT_RECONCILIATION_RULESET_PATH);
  candidates.push_back(
      "/usr/share/traceloom/default_event_reconciliation_rules.tsv");
  candidates.push_back(
      "/usr/local/share/traceloom/default_event_reconciliation_rules.tsv");
  for (const std::string& candidate : candidates) {
    if (candidate.empty()) continue;
    std::ifstream probe(candidate);
    if (probe.good()) return load_event_reconciliation_ruleset(candidate);
  }
  throw std::invalid_argument(
      "default event-reconciliation manifest not found; set "
      "TRACELOOM_EVENT_RECONCILIATION_RULES or pass "
      "--event-reconciliation-rules");
}

EventReconciliationRuleset overlay_event_reconciliation_ruleset(
    const EventReconciliationRuleset& base,
    const EventReconciliationRuleset& overlay) {
  std::vector<EventReconciliationRule> merged = base.rules();
  for (const auto& replacement : overlay.rules()) {
    merged.erase(std::remove_if(merged.begin(), merged.end(),
                                [&](const auto& existing) {
                                  return existing.rule_id ==
                                         replacement.rule_id;
                                }),
                 merged.end());
    merged.push_back(replacement);
  }
  const std::string digest =
      sha256_hex(base.manifest_sha256() + ":" + overlay.manifest_sha256());
  return {kManifestSchema,
          base.policy_id() + "+overlay:" + overlay.policy_id(),
          base.policy_version() + "+" + overlay.policy_version(),
          base.source_manifest() + ";" + overlay.source_manifest(),
          digest,
          base.unmatched_behavior(),
          std::move(merged)};
}

EventReconciliationState reconcile_event_observations(
    const NativeIr& ir,
    const EventReconciliationRuleset& ruleset) {
  EventReconciliationState state;
  state.policy = ruleset.snapshot();
  std::map<GroupKey, Group> groups;
  for (const TaskRow& task : ir.tasks.rows()) {
    const EventReconciliationRule* rule = matching_rule(ir, task, ruleset);
    if (rule == nullptr || !task.trace_event_id.valid() ||
        task.trace_event_id.value() >= ir.trace_events.size() ||
        task.raw_connection_id < 0) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
    GroupKey key{rule->rule_id,
                 task.source_ref_id.value(),
                 event.device_id,
                 event.stream_id,
                 task.raw_task_id,
                 task.raw_connection_id,
                 symbol_text(ir, task.task_type_symbol_id)};
    Group& group = groups[key];
    group.rule = rule;
    group.tasks.push_back(&task);
  }

  for (const auto& item : groups) {
    const Group& group = item.second;
    const auto decision_id =
        checked_next_id<EventReconciliationDecisionId>(state.decisions.size());
    EventReconciliationDecisionRow decision;
    decision.id = decision_id;
    decision.rule_id = group.rule->rule_id;

    std::vector<const TaskRow*> generic;
    std::vector<const TaskRow*> detail;
    std::vector<const TaskRow*> other;
    for (const TaskRow* task : group.tasks) {
      const bool metadata = has_operator_detail(*task);
      if (task->raw_context_id == group.rule->generic_context_id &&
          !metadata) {
        generic.push_back(task);
      } else if (task->raw_context_id == group.rule->concrete_context_id &&
                 metadata) {
        detail.push_back(task);
      } else {
        other.push_back(task);
      }
    }

    if (!other.empty() || generic.size() > 1 || detail.size() > 1) {
      decision.status = EventReconciliationStatus::kAmbiguous;
      decision.reason_code = "non_unique_or_incompatible_candidates";
      append_members(state, decision_id, group.tasks,
                     EventReconciliationMemberRole::kConflictingCandidate);
      state.decisions.push_back(std::move(decision));
      continue;
    }
    if (generic.size() != 1 || detail.size() != 1) {
      decision.status = EventReconciliationStatus::kIndependent;
      decision.reason_code = generic.empty() ? "missing_timing_envelope_peer"
                                             : "missing_semantic_detail_peer";
      append_members(state, decision_id, group.tasks,
                     EventReconciliationMemberRole::kIndependentCandidate);
      state.decisions.push_back(std::move(decision));
      continue;
    }

    const TraceEventRow& envelope =
        ir.trace_events.row(generic.front()->trace_event_id);
    const TraceEventRow& semantic =
        ir.trace_events.row(detail.front()->trace_event_id);
    decision.contained_fraction = contained_fraction(envelope, semantic);
    if (decision.contained_fraction < group.rule->min_contained_fraction) {
      decision.status = EventReconciliationStatus::kConflict;
      decision.reason_code = "insufficient_interval_containment";
      append_members(state, decision_id, group.tasks,
                     EventReconciliationMemberRole::kConflictingCandidate);
      state.decisions.push_back(std::move(decision));
      continue;
    }
    if (replay_membership(ir, envelope) != replay_membership(ir, semantic)) {
      decision.status = EventReconciliationStatus::kConflict;
      decision.reason_code = "protected_replay_membership_mismatch";
      append_members(state, decision_id, group.tasks,
                     EventReconciliationMemberRole::kConflictingCandidate);
      state.decisions.push_back(std::move(decision));
      continue;
    }

    decision.status = EventReconciliationStatus::kReconciled;
    decision.reason_code = "unique_identity_pair_with_contained_detail";
    decision.canonical_task_id = detail.front()->id;
    decision.canonical_event_id = detail.front()->trace_event_id;
    decision.envelope_event_id = generic.front()->trace_event_id;
    // The rule identifies this member as the profiler's timing envelope. The
    // containment threshold tolerates tiny timestamp disagreement, but it does
    // not authorize the semantic-detail row to expand the charged interval.
    decision.canonical_start_ns = envelope.start_ns;
    decision.canonical_end_ns = envelope.end_ns;
    state.members.push_back(
        {decision_id, generic.front()->id, generic.front()->trace_event_id,
         EventReconciliationMemberRole::kTimingEnvelope, true, false, true,
         true});
    state.members.push_back(
        {decision_id, detail.front()->id, detail.front()->trace_event_id,
         EventReconciliationMemberRole::kSemanticDetail, false, true, false,
         true});
    state.decisions.push_back(std::move(decision));
  }
  return state;
}

}  // namespace traceloom
