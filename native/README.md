# TraceLoom Native

Native C++ analyzer for TraceLoom. This is the recommended entry point for
current Ascend `msprof` profiles.

## Install

### Debian package

Build the installable `traceloom-native` package on Debian or Ubuntu:

```bash
cmake -S native -B build/traceloom-native-package \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF
cmake --build build/traceloom-native-package -j "$(nproc)"
cpack --config build/traceloom-native-package/CPackConfig.cmake \
  -B build/traceloom-native-package
sudo apt install ./build/traceloom-native-package/traceloom-native_*.deb
```

The package installs one stable entry point: `/usr/bin/traceloom`. Verify the
installation with:

```bash
traceloom --version
traceloom --help
```

Remove it with:

```bash
sudo apt remove traceloom-native
```

### Install from source

From the TraceLoom repository root:

```bash
cmake --preset dev
cmake --build --preset dev -j "$(nproc)"
cmake --install build/native --prefix "$HOME/.local"
```

This installs the native command as:

```bash
traceloom
```

The `dev` preset searches `$HOME/.local` through `CMAKE_PREFIX_PATH`, so a
user-local SQLite install such as `~/.local/include/sqlite3.h` and
`~/.local/lib/libsqlite3.a` is enough to enable Ascend `msprof` DB loading.

## Analyze A Profile

Pass either one Huawei `msprof_*.db` file:

```bash
traceloom examples/kickstart_smoke/msprof_raw/PROF_000001_20260609064648517_AJJGNKPPJMEGGLFA/msprof_20260609064817.db
```

or a Huawei profiler bundle directory containing `PROF_*/msprof_*.db` files:

```bash
traceloom examples/kickstart_smoke/msprof_raw
```

TraceLoom discovers the DB files automatically and writes reports under a
neighboring `traceloom/` directory:

```text
PROF_.../traceloom/loop_tree_v2.md
msprof_raw/traceloom/device0_loop_tree_v2.md
msprof_raw/traceloom/device1_loop_tree_v2.md
```

Use more or fewer worker threads when needed:

```bash
traceloom /path/to/msprof_output --threads 48
```

## Developer Commands

Build with native tests enabled:

```bash
cmake --preset dev-tests
cmake --build --preset dev-tests -j "$(nproc)"
ctest --preset dev-tests
```

Some deep regression tests use external design fixtures from the larger
research workspace. Fresh standalone clones skip those tests when the fixtures
are not present; normal unit tests still run.

Advanced analyzer output and the inventory tool remain available for
development:

```bash
build/native/native/traceloom \
  --source-db examples/kickstart_smoke/msprof_raw/.../msprof_*.db \
  --out native_result.json

build/native/native/traceloom-native-ascend-sqlite-inventory \
  examples/kickstart_smoke/msprof_raw/.../msprof_*.db
```

To test the core build without SQLite support:

```bash
cmake --preset dev-no-sqlite
cmake --build --preset dev-no-sqlite -j "$(nproc)"
```
