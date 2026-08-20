#pragma once

#include "traceloom/analysis/event_reconciliation.h"

namespace traceloom {

struct NativeIr;

void reconcile_task_communication_observations(
    const NativeIr& ir,
    const EventReconciliationRuleset& ruleset,
    EventReconciliationState& state);

}  // namespace traceloom
