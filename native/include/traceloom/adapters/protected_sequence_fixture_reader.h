#pragma once

#include <string>

#include "traceloom/adapters/fixture_adapter.h"
#include "traceloom/pattern/candidate_scan.h"
#include "traceloom/sequence/partition_plan.h"

namespace traceloom {

struct ProtectedSequenceFixture {
  std::string fixture_id;
  FixtureInput input;
  PartitionPlanConfig partition_config;
  CandidateScanConfig candidate_scan_config;
};

ProtectedSequenceFixture load_protected_sequence_fixture(
    const std::string& path);

}  // namespace traceloom
