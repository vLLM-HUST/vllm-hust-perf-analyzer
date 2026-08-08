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
  ca-certificates cmake g++ libsqlite3-dev make python3 sqlite3

build_dir=/tmp/traceloom-native-golden-check
rm -rf "$build_dir"
cmake -S native -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=ON
cmake --build "$build_dir" -j "$(nproc)" \
  --target traceloom_native_idle_evidence_golden_fixture_tests \
           traceloom_native_idle_evidence_fixture_matrix_tests
ctest --test-dir "$build_dir" \
  -R 'traceloom_native_idle_evidence_(golden_fixture|fixture_matrix)_tests' \
  --output-on-failure

# Fixture drift guard: fixture.sql must regenerate a database whose content
# matches the checked-in golden .db, otherwise the fixture and its SQL source
# have drifted. Compare normalized .dump output, not raw bytes: SQLite binary
# layout can differ across versions.
fixture_dir=native/tests/fixtures/idle_evidence/host_wait_zero_visible_idle
regenerated=/tmp/golden_regenerated.db
rm -f "$regenerated"
sqlite3 "$regenerated" < "$fixture_dir/fixture.sql"
sqlite3 "$regenerated" .dump > /tmp/golden_regenerated.dump
sqlite3 "$fixture_dir/host_wait_zero_visible_idle.db" .dump \
  > /tmp/golden_checked_in.dump
diff -u /tmp/golden_checked_in.dump /tmp/golden_regenerated.dump
echo "fixture.sql regenerates the checked-in golden db (no drift)"

for fixture_name in adjacent_overlap event_loss clock_drift; do
  fixture_dir="native/tests/fixtures/idle_evidence/$fixture_name"
  regenerated="/tmp/${fixture_name}_regenerated.db"
  rm -f "$regenerated"
  sqlite3 "$regenerated" < "$fixture_dir/fixture.sql"
  sqlite3 "$regenerated" .dump > "/tmp/${fixture_name}_regenerated.dump"
  sqlite3 "$fixture_dir/$fixture_name.db" .dump \
    > "/tmp/${fixture_name}_checked_in.dump"
  diff -u "/tmp/${fixture_name}_checked_in.dump" \
    "/tmp/${fixture_name}_regenerated.dump"
done
echo "all four fixture SQL sources regenerate their checked-in databases"

python3 tools/idle_evidence_evaluation.py \
  --manifest native/tests/fixtures/idle_evidence/evaluation_matrix.json \
  --json /tmp/idle-evidence-evaluation.json \
  --csv /tmp/idle-evidence-curve.csv \
  --markdown /tmp/idle-evidence-evaluation.md
test -s /tmp/idle-evidence-evaluation.json
test -s /tmp/idle-evidence-curve.csv
test -s /tmp/idle-evidence-evaluation.md
echo "Coverage/FAR/confusion/boundary/overhead report matrix passed"
