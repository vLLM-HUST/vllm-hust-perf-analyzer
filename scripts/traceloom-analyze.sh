#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  scripts/traceloom-analyze.sh <run-dir-or-msprof-raw-dir> [out-dir] [extra traceloom args...]

Environment:
  PYTHON                         Python executable. Default: python3
  TRACELOOM_TOP_DEVICES          Default --top-devices-global. Default: 4
  TRACELOOM_MAX_MAIN_EVENTS      Default --max-main-events-per-device. Default: 0
  TRACELOOM_MAX_MACRO_DEFS       Default --max-macro-defs. Default: 0
  TRACELOOM_READABLE_MACRO_MODE  Default --readable-macro-mode. Default: inline
  TRACELOOM_OUTPUT_MODE          Default --output-mode. Default: bundle
USAGE
}

if [[ $# -lt 1 || "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
traceloom_root="$(cd "${script_dir}/.." && pwd)"
python_bin="${PYTHON:-python3}"

input_path="$1"
shift

out_dir=""
if [[ $# -gt 0 && "${1:-}" != --* ]]; then
  out_dir="$1"
  shift
fi

export PYTHONPATH="${traceloom_root}${PYTHONPATH:+:${PYTHONPATH}}"

command=("${python_bin}" -m traceloom analyze "${input_path}")
if [[ -n "${out_dir}" ]]; then
  command+=(--out-dir "${out_dir}")
fi
exec "${command[@]}" \
  --top-devices-global "${TRACELOOM_TOP_DEVICES:-4}" \
  --max-main-events-per-device "${TRACELOOM_MAX_MAIN_EVENTS:-0}" \
  --max-macro-defs "${TRACELOOM_MAX_MACRO_DEFS:-0}" \
  --readable-macro-mode "${TRACELOOM_READABLE_MACRO_MODE:-inline}" \
  --output-mode "${TRACELOOM_OUTPUT_MODE:-bundle}" \
  "$@"
