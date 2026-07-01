#pragma once

#include "traceloom/adapters/aclgraph_fixture_reader.h"
#include "traceloom/adapters/source_adapter.h"

namespace traceloom {

class AclGraphFixtureAdapter final : public SourceAdapter {
 public:
  explicit AclGraphFixtureAdapter(AclGraphSemanticFixture fixture);

  NativeIr load() const override;

 private:
  AclGraphSemanticFixture fixture_;
};

AnchorKind aclgraph_anchor_kind_for_slot_symbol(const std::string& slot_symbol);

}  // namespace traceloom
