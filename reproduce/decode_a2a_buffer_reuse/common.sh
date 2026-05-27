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

tl_in_container() {
  [[ -n "${TRACELOOM_CONTAINER:-}" ]]
}

tl_remote_root() {
  echo "${TRACELOOM_REMOTE_ROOT:-/tmp/traceloom_decode_a2a_buffer_reuse}"
}

tl_container_exec() {
  docker exec "$TRACELOOM_CONTAINER" sh -lc "$*"
}

tl_shell_join() {
  printf "%q " "$@"
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
  if tl_in_container; then
    command -v docker >/dev/null 2>&1 || {
      echo "docker not found but TRACELOOM_CONTAINER is set." >&2
      exit 2
    }
    docker ps --format '{{.Names}}' | grep -Fx "$TRACELOOM_CONTAINER" >/dev/null || {
      echo "container is not running: $TRACELOOM_CONTAINER" >&2
      exit 2
    }
    tl_container_exec "command -v msprof >/dev/null && command -v npu-smi >/dev/null && npu-smi info >/dev/null" || {
      echo "msprof/npu-smi check failed inside container: $TRACELOOM_CONTAINER" >&2
      exit 2
    }
    return
  fi
  command -v npu-smi >/dev/null 2>&1 || {
    echo "npu-smi not found; activate the Ascend runtime environment first." >&2
    exit 2
  }
  command -v msprof >/dev/null 2>&1 || {
    echo "msprof not found; activate the Ascend profiler environment first." >&2
    exit 2
  }
  npu-smi info >/dev/null
}

tl_prepare_runtime() {
  if ! tl_in_container; then
    return
  fi
  if tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
    local remote_root
    remote_root=$(tl_remote_root)
    echo "+ docker exec $TRACELOOM_CONTAINER sh -lc \"mkdir -p '$remote_root' '$remote_root/patches' '$remote_root/workloads'\""
    echo "+ docker cp $TRACELOOM_PROJECT_ROOT/examples/workloads/vllm_ascend_smoke.py $TRACELOOM_CONTAINER:$remote_root/workloads/vllm_ascend_smoke.py"
    echo "+ docker cp $(tl_patch_file) $TRACELOOM_CONTAINER:$remote_root/patches/decode_a2a_buffer_reuse.diff"
    return
  fi
  command -v docker >/dev/null 2>&1 || {
    echo "docker not found but TRACELOOM_CONTAINER is set." >&2
    exit 2
  }
  docker ps --format '{{.Names}}' | grep -Fx "$TRACELOOM_CONTAINER" >/dev/null || {
    echo "container is not running: $TRACELOOM_CONTAINER" >&2
    exit 2
  }
  local remote_root
  remote_root=$(tl_remote_root)
  docker exec "$TRACELOOM_CONTAINER" sh -lc "mkdir -p '$remote_root' '$remote_root/patches' '$remote_root/workloads'"
  docker cp "$TRACELOOM_PROJECT_ROOT/examples/workloads/vllm_ascend_smoke.py" \
    "$TRACELOOM_CONTAINER:$remote_root/workloads/vllm_ascend_smoke.py"
  docker cp "$(tl_patch_file)" "$TRACELOOM_CONTAINER:$remote_root/patches/decode_a2a_buffer_reuse.diff"
}

tl_patch_file() {
  echo "${TRACELOOM_PATCH_FILE:-$TRACELOOM_CANN_SCRIPT_DIR/decode_a2a_buffer_reuse.diff}"
}

tl_runtime_patch_file() {
  if tl_in_container; then
    echo "$(tl_remote_root)/patches/decode_a2a_buffer_reuse.diff"
  else
    tl_patch_file
  fi
}

tl_runtime_vllm_ascend_dir() {
  if tl_in_container; then
    echo "${TRACELOOM_CONTAINER_VLLM_ASCEND_DIR:-/vllm-workspace/vllm-ascend}"
  else
    tl_require TRACELOOM_VLLM_ASCEND_DIR
    echo "$TRACELOOM_VLLM_ASCEND_DIR"
  fi
}

tl_apply_patch_state() {
  local state=$1
  local patch_file
  patch_file=$(tl_runtime_patch_file)
  local vllm_dir
  vllm_dir=$(tl_runtime_vllm_ascend_dir)
  if tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
    echo "+ cd $vllm_dir && apply Decode All-to-All Buffer Reuse state: $state"
    return
  fi

  local script
  case "$state" in
    baseline)
      script="
        cd '$vllm_dir'
        if git apply --reverse --check '$patch_file' >/dev/null 2>&1; then
          git apply --reverse '$patch_file'
        fi
        if grep -q '_a2a_recv_buf\\|_otp_recv_buf' vllm_ascend/ops/linear_op.py; then
          echo 'Decode All-to-All Buffer Reuse symbols still present after baseline switch' >&2
          exit 3
        fi
      "
      ;;
    optimized)
      script="
        cd '$vllm_dir'
        if git apply --check '$patch_file' >/dev/null 2>&1; then
          git apply '$patch_file'
        elif git apply --reverse --check '$patch_file' >/dev/null 2>&1; then
          :
        else
          echo 'Decode All-to-All Buffer Reuse cannot be applied or detected in $vllm_dir' >&2
          exit 3
        fi
        grep -q '_a2a_recv_buf' vllm_ascend/ops/linear_op.py
        grep -q '_otp_recv_buf' vllm_ascend/ops/linear_op.py
      "
      ;;
    *)
      echo "unknown patch state: $state" >&2
      exit 2
      ;;
  esac
  if tl_in_container; then
    tl_container_exec "$script"
  else
    bash -lc "$script"
  fi
}

tl_build_workload_cmd() {
  tl_require TRACELOOM_MODEL_PATH
  local workload_path="$TRACELOOM_PROJECT_ROOT/examples/workloads/vllm_ascend_smoke.py"
  if tl_in_container; then
    workload_path="$(tl_remote_root)/workloads/vllm_ascend_smoke.py"
  fi
  TL_WORKLOAD_CMD=(
    python3 "$workload_path"
    --model "$TRACELOOM_MODEL_PATH"
    --tp "${TRACELOOM_TP:-4}"
    --pp "${TRACELOOM_PP:-1}"
    --max-model-len "${TRACELOOM_MAX_MODEL_LEN:-1024}"
    --max-tokens "${TRACELOOM_MAX_TOKENS:-32}"
    --min-tokens "${TRACELOOM_MIN_TOKENS:-0}"
    --batch-size "${TRACELOOM_BATCH_SIZE:-1}"
    --rounds "${TRACELOOM_ROUNDS:-1}"
    --dispatch-mode "${TRACELOOM_DISPATCH_MODE:-round}"
    --dtype "${TRACELOOM_DTYPE:-bfloat16}"
    --seed "${TRACELOOM_SEED:-0}"
  )
  if tl_bool_true "${TRACELOOM_IGNORE_EOS:-0}"; then
    TL_WORKLOAD_CMD+=(--ignore-eos)
  fi
  if tl_bool_true "${TRACELOOM_TRUST_REMOTE_CODE:-0}"; then
    TL_WORKLOAD_CMD+=(--trust-remote-code)
  fi
}

tl_out_root() {
  echo "${TRACELOOM_OUT_ROOT:-$TRACELOOM_PROJECT_ROOT/out/reproduce/decode_a2a_buffer_reuse}"
}

tl_runtime_env_prefix() {
  echo "ASCEND_RT_VISIBLE_DEVICES=$TRACELOOM_DEVICES ASCEND_VISIBLE_DEVICES=$TRACELOOM_DEVICES NPU_VISIBLE_DEVICES=$TRACELOOM_DEVICES HCCL_OP_EXPANSION_MODE=${HCCL_OP_EXPANSION_MODE:-AIV} VLLM_PLUGINS=ascend VLLM_ASCEND_WORKER_NARROW_VISIBLE_DEVICES=${TRACELOOM_NARROW_WORKER_VISIBLE_DEVICES:-${VLLM_ASCEND_WORKER_NARROW_VISIBLE_DEVICES:-1}} VLLM_WORKER_MULTIPROC_METHOD=${TRACELOOM_WORKER_MULTIPROC_METHOD:-${VLLM_WORKER_MULTIPROC_METHOD:-spawn}}"
}
