#pragma once
#include "traceloom/ir/native_ir.h"
namespace traceloom {
// Reorder only eligible token runs; keep anchor IDs and device geometry fixed.
// Missing/ambiguous/reused evidence is a barrier, never an event filter.
// Exact replay protection is left untouched (no reordering in that case).
void apply_host_launch_order(NativeIr& ir);
}  // namespace traceloom
