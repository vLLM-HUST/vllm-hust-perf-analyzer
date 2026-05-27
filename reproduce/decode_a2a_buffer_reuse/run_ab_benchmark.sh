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
tl_prepare_runtime
tl_build_workload_cmd

OUT_ROOT=$(tl_out_root)
REPORT_DIR="$OUT_ROOT/reports/ab_decode_a2a_buffer_reuse_aiv"
mkdir -p "$REPORT_DIR"
if tl_in_container; then
  REMOTE_REPORT_DIR="$(tl_remote_root)/reports/ab_decode_a2a_buffer_reuse_aiv"
  if tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
    echo "+ docker exec $TRACELOOM_CONTAINER sh -lc \"rm -rf '$REMOTE_REPORT_DIR' && mkdir -p '$REMOTE_REPORT_DIR'\""
  else
    tl_container_exec "rm -rf '$REMOTE_REPORT_DIR' && mkdir -p '$REMOTE_REPORT_DIR'"
  fi
fi

run_one() {
  local state=$1
  local tag=$2
  tl_apply_patch_state "$state"
  local output_json="$REPORT_DIR/${tag}.json"
  local log_file="$REPORT_DIR/${tag}.log"
  local runtime_json="$output_json"
  local runtime_log="$log_file"
  if tl_in_container; then
    runtime_json="$REMOTE_REPORT_DIR/${tag}.json"
    runtime_log="$REMOTE_REPORT_DIR/${tag}.log"
  fi
  local command=("${TL_WORKLOAD_CMD[@]}" --output-json "$output_json")
  if tl_in_container; then
    command=("${TL_WORKLOAD_CMD[@]}" --output-json "$runtime_json")
  fi
  local command_text
  command_text=$(tl_shell_join "${command[@]}")
  local runtime_command
  runtime_command="$(tl_runtime_env_prefix) $command_text > '$runtime_log' 2>&1"
  if tl_in_container; then
    printf '+ docker exec %s sh -lc %q\n' "$TRACELOOM_CONTAINER" "$runtime_command"
  else
    echo "+ $runtime_command"
  fi
  if ! tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
    if tl_in_container; then
      tl_container_exec "$runtime_command"
      docker cp "$TRACELOOM_CONTAINER:$runtime_json" "$output_json"
      docker cp "$TRACELOOM_CONTAINER:$runtime_log" "$log_file"
    else
      bash -lc "$runtime_command"
    fi
  fi
}

run_one baseline warmup_baseline
run_one optimized warmup_optimized

for pair in $(seq 1 "${TRACELOOM_AB_PAIRS:-5}"); do
  run_one baseline "baseline_pair${pair}"
  run_one optimized "optimized_pair${pair}"
done

tl_apply_patch_state baseline
echo "A/B reports: $REPORT_DIR"
