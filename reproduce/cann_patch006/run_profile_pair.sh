#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ENV_FILE="${TRACELOOM_CANN_ENV:-$SCRIPT_DIR/local.env}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --env-file)
      ENV_FILE=$2
      shift 2
      ;;
    --dry-run)
      TRACELOOM_DRY_RUN=1
      shift
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

# shellcheck disable=SC1091
source "$SCRIPT_DIR/common.sh"
tl_load_env "$ENV_FILE"
tl_configure_ascend_env
tl_check_host
tl_build_workload_cmd

OUT_ROOT=$(tl_out_root)
mkdir -p "$OUT_ROOT/profiles"

profile_one() {
  local state=$1
  local tag=$2
  local run_dir="$OUT_ROOT/profiles/$tag"
  local raw_dir="$run_dir/msprof_raw"
  local report_json="$raw_dir/workload_result.json"
  local log_file="$run_dir/workload.log"
  mkdir -p "$raw_dir"

  tl_apply_patch_state "$state"
  local workload=("${TL_WORKLOAD_CMD[@]}" --output-json "$report_json")
  local workload_text
  printf -v workload_text "%q " "${workload[@]}"

  read -r -a extra_msprof_args <<< "${TRACELOOM_MSPROF_ARGS:-}"
  echo "+ msprof --output=$raw_dir --application=$workload_text ${extra_msprof_args[*]:-} > $log_file 2>&1"
  if ! tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
    msprof "--output=$raw_dir" "--application=$workload_text" "${extra_msprof_args[@]}" >"$log_file" 2>&1
  fi
}

profile_one baseline baseline
profile_one patch006 patch006
tl_apply_patch_state baseline

paper_args=(
  "$TRACELOOM_PROJECT_ROOT/reproduce/run_reference.py"
  --out-root "$OUT_ROOT"
  paper-patch006
  --source-root "$OUT_ROOT"
  --mode raw-analysis
  --baseline-run-dir "$OUT_ROOT/profiles/baseline"
  --patch006-run-dir "$OUT_ROOT/profiles/patch006"
)
if tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
  paper_args+=(--dry-run)
fi
tl_run python3 "${paper_args[@]}"

echo "Profile pair: $OUT_ROOT/profiles"
echo "TraceLoom comparison: $OUT_ROOT/paper_patch006"
