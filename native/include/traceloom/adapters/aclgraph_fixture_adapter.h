#pragma once

#include "traceloom/adapters/aclgraph_fixture_reader.h"
#include "traceloom/analysis/anchor_internal_cost_breakdown.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom {

class AclGraphFixtureAdapter final {
 public:
  explicit AclGraphFixtureAdapter(AclGraphSemanticFixture fixture);

  NativeIr load() const;

 private:
  AclGraphSemanticFixture fixture_;
};

AnchorKind aclgraph_anchor_kind_for_slot_symbol(const std::string& slot_symbol);

AnchorInternalCostBreakdown build_aclgraph_fixture_anchor_cost_breakdown(
    const AclGraphSemanticFixture& fixture,
    const NativeIr& ir);

}  // namespace traceloom
