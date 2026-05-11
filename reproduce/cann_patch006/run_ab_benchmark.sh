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
REPORT_DIR="$OUT_ROOT/reports/ab_patch006_aiv"
mkdir -p "$REPORT_DIR"

run_one() {
  local state=$1
  local tag=$2
  tl_apply_patch_state "$state"
  local output_json="$REPORT_DIR/${tag}.json"
  local log_file="$REPORT_DIR/${tag}.log"
  local command=("${TL_WORKLOAD_CMD[@]}" --output-json "$output_json")
  echo "+ ${command[*]} > $log_file 2>&1"
  if ! tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
    "${command[@]}" >"$log_file" 2>&1
  fi
}

run_one baseline warmup_baseline
run_one patch006 warmup_patch006

for pair in $(seq 1 "${TRACELOOM_AB_PAIRS:-5}"); do
  run_one baseline "baseline_pair${pair}"
  run_one patch006 "patch006_pair${pair}"
done

tl_apply_patch_state baseline
echo "A/B reports: $REPORT_DIR"
