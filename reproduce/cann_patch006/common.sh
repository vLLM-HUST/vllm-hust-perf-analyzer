#!/usr/bin/env bash

set -euo pipefail

TRACELOOM_CANN_SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
TRACELOOM_PROJECT_ROOT=$(cd "$TRACELOOM_CANN_SCRIPT_DIR/../.." && pwd)

tl_load_env() {
  local env_file=$1
  if [[ -f "$env_file" ]]; then
    # shellcheck disable=SC1090
    source "$env_file"
  fi
}

tl_require() {
  local name=$1
  if [[ -z "${!name:-}" ]]; then
    echo "missing required environment variable: $name" >&2
    exit 2
  fi
}

tl_bool_true() {
  case "${1:-0}" in
    1|true|TRUE|yes|YES|on|ON) return 0 ;;
    *) return 1 ;;
  esac
}

tl_run() {
  echo "+ $*"
  if ! tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
    "$@"
  fi
}

tl_run_shell() {
  echo "+ $*"
  if ! tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
    bash -lc "$*"
  fi
}

tl_configure_ascend_env() {
  tl_require TRACELOOM_DEVICES
  export ASCEND_RT_VISIBLE_DEVICES="$TRACELOOM_DEVICES"
  export ASCEND_VISIBLE_DEVICES="$TRACELOOM_DEVICES"
  export HCCL_OP_EXPANSION_MODE="${HCCL_OP_EXPANSION_MODE:-AIV}"
}

tl_check_host() {
  if tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
    return
  fi
  command -v npu-smi >/dev/null 2>&1 || {
    echo "npu-smi not found; activate the Ascend runtime environment first." >&2
    exit 2
  }
  npu-smi info >/dev/null
}

tl_patch_file() {
  echo "${TRACELOOM_PATCH_FILE:-$TRACELOOM_CANN_SCRIPT_DIR/patch_006.diff}"
}

tl_apply_patch_state() {
  local state=$1
  tl_require TRACELOOM_VLLM_ASCEND_DIR
  local patch_file
  patch_file=$(tl_patch_file)
  if tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
    echo "+ cd $TRACELOOM_VLLM_ASCEND_DIR && apply Patch006 state: $state"
    return
  fi

  cd "$TRACELOOM_VLLM_ASCEND_DIR"
  case "$state" in
    baseline)
      if git apply --reverse --check "$patch_file" >/dev/null 2>&1; then
        git apply --reverse "$patch_file"
      fi
      ;;
    patch006)
      if git apply --check "$patch_file" >/dev/null 2>&1; then
        git apply "$patch_file"
      elif git apply --reverse --check "$patch_file" >/dev/null 2>&1; then
        :
      else
        echo "Patch006 cannot be applied or detected in $TRACELOOM_VLLM_ASCEND_DIR" >&2
        exit 3
      fi
      ;;
    *)
      echo "unknown patch state: $state" >&2
      exit 2
      ;;
  esac
  cd "$TRACELOOM_PROJECT_ROOT"
}

tl_build_workload_cmd() {
  tl_require TRACELOOM_MODEL_PATH
  TL_WORKLOAD_CMD=(
    python3 "$TRACELOOM_PROJECT_ROOT/examples/workloads/vllm_ascend_smoke.py"
    --model "$TRACELOOM_MODEL_PATH"
    --tp "${TRACELOOM_TP:-4}"
    --pp "${TRACELOOM_PP:-1}"
    --max-model-len "${TRACELOOM_MAX_MODEL_LEN:-1024}"
    --max-tokens "${TRACELOOM_MAX_TOKENS:-32}"
    --batch-size "${TRACELOOM_BATCH_SIZE:-1}"
    --rounds "${TRACELOOM_ROUNDS:-1}"
    --dispatch-mode "${TRACELOOM_DISPATCH_MODE:-round}"
    --dtype "${TRACELOOM_DTYPE:-bfloat16}"
  )
  if tl_bool_true "${TRACELOOM_TRUST_REMOTE_CODE:-0}"; then
    TL_WORKLOAD_CMD+=(--trust-remote-code)
  fi
}

tl_out_root() {
  echo "${TRACELOOM_OUT_ROOT:-$TRACELOOM_PROJECT_ROOT/out/reproduce/cann_patch006}"
}
