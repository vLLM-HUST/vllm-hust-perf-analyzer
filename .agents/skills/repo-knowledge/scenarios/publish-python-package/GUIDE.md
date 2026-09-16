# Build and publish the unified Python package

`traceloom` is the user-facing installation: bundled native executable and YAML,
Python analysis/query/export interface, console CLI, and a dependency on the
small `traceloom-vllm-context` observer distribution. Do not replace this with a
Python launcher requiring a separately installed analyzer. The runtime dependency
alone is not the full product. Importing `traceloom` must not load torch or vLLM;
`traceloom.vllm` deliberately imports runtime adapters only when selected.

Packaging uses scikit-build-core and `packaging/python/CMakeLists.txt`. The
production native directory is added EXCLUDE_FROM_ALL; only the analyzer target
and runtime assets are installed into the wheel. Keep `_native/bin/traceloom`
next to `_native/share/traceloom`: native default-rule lookup is executable-relative.
The Python API delegates to that exact binary, not PATH, and preserves native
atomic output and provenance. Its first contract accepts a single SQLite input;
full directory discovery and all remaining flags stay available through the CLI.

Run `python/tests/test_api.py` against an INSTALLED wheel, outside a source import
path. It verifies native execution, query limits, read-only access, failure and
input protection, YAML, gzip export and timeout behavior. Existing scheduler CPU
contract tests remain under integrations/vllm/tests. A 2026-09-16 installed-wheel
check also reanalyzed the approved real Qwen linked capture and reproduced all
HPO occurrence/member and tree-cost rows, then exported Perfetto. It did not run
new accelerator work. Local receipt: runs/traceloom-python-api in Fletcher's
/root/my-ascend-workspace.

Use the GitHub `Python package` workflow for x86_64 and aarch64 manylinux_2_28
wheels. Each architecture builds in its native container, repairs bundled native
dependencies with auditwheel, and installs/tests its own wheel. Do not publish a
local linux_aarch64 wheel with a fabricated manylinux tag: the local openEuler
binary requires glibc 2.38. The first portable build exposed a missing pthread
link, hidden by glibc >=2.34's merged libc; native core now declares Threads::Threads.
Keep third-party license notices with bundled libraries.

Build the sdist from the same source revision and actually build a wheel from it.
Exclude raw DBs, experiments, build trees and private evidence. PyPI publication
uses twine's config-file handling of the existing ~/.pypirc; never print its
contents or put a token in argv. Inspect package metadata/artifact content and
run twine check before upload. Validate published filenames/digests and a clean
public-index installation after upload. Preserve public release provenance, not
private credentials. Website copy must match the package actually published.
