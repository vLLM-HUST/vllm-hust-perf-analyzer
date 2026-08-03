#!/bin/sh
# Run the checked-in idle-evidence golden fixture check in a clean Debian
# container (idle evidence contract sections 5, 10):
#   "host wait exists, but visible idle is zero".
# The check loads the synthetic fixture
#   native/tests/fixtures/idle_evidence/host_wait_zero_visible_idle/
# and asserts BOTH sides of the counterexample: a host sync API
# (aclrtSynchronizeStream) is present in the fixture, and the productive
# timeline over the same fixture reports zero visible_productive_idle.
set -eu

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  ca-certificates cmake g++ libsqlite3-dev make

build_dir=/tmp/traceloom-native-golden-check
rm -rf "$build_dir"
cmake -S native -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=ON
cmake --build "$build_dir" -j "$(nproc)" \
  --target traceloom_native_idle_evidence_golden_fixture_tests
ctest --test-dir "$build_dir" \
  -R traceloom_native_idle_evidence_golden_fixture_tests \
  --output-on-failure
