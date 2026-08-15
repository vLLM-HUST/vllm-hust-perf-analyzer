#include "replay_internal_cost_map_test_cases.h"

int main() {
  using namespace traceloom::testing::replay_internal_cost_map;
  run_happy_path_tests();
  run_contract_tests();
  return 0;
}
