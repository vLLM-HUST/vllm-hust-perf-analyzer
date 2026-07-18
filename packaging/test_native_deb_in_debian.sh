#!/bin/sh
set -eu

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  ca-certificates cmake g++ libsqlite3-dev make

build_dir=/tmp/traceloom-native-package
rm -rf "$build_dir"
cmake -S native -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF
cmake --build "$build_dir" -j "$(nproc)"
cpack --config "$build_dir/CPackConfig.cmake" -B "$build_dir"

package=$(find "$build_dir" -maxdepth 1 -name 'traceloom-native_*.deb' -print -quit)
test -n "$package"
dpkg -i "$package"

traceloom-native-analyze-db --version
traceloom-native-analyze-db --help

sample=examples/kickstart_smoke/msprof_raw/PROF_000001_20260609064648517_AJJGNKPPJMEGGLFA/msprof_20260609064817.db
traceloom-native-analyze-db "$sample" \
  --threads 2 \
  --loop-tree-out /tmp/loop_tree_v2.md \
  --out /tmp/native_result.json
test -s /tmp/loop_tree_v2.md
test -s /tmp/native_result.json

dpkg -r traceloom-native
if command -v traceloom-native-analyze-db >/dev/null 2>&1; then
  echo "traceloom-native-analyze-db remains installed after package removal" >&2
  exit 1
fi
