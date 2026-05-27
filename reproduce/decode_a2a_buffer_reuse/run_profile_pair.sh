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
mkdir -p "$OUT_ROOT/profiles"

profile_one() {
  local state=$1
  local tag=$2
  local run_dir="$OUT_ROOT/profiles/$tag"
  local raw_dir="$run_dir/msprof_raw"
  local report_json="$raw_dir/workload_result.json"
  local log_file="$run_dir/workload.log"
  local runtime_run_dir="$run_dir"
  local runtime_raw_dir="$raw_dir"
  local runtime_report_json="$report_json"
  local runtime_log_file="$log_file"
  if tl_in_container; then
    runtime_run_dir="$(tl_remote_root)/profiles/$tag"
    runtime_raw_dir="$runtime_run_dir/msprof_raw"
    runtime_report_json="$runtime_raw_dir/workload_result.json"
    runtime_log_file="$runtime_run_dir/workload.log"
  fi
  mkdir -p "$raw_dir"
  if tl_in_container; then
    if tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
      echo "+ docker exec $TRACELOOM_CONTAINER sh -lc \"rm -rf '$runtime_run_dir' && mkdir -p '$runtime_raw_dir'\""
    else
      tl_container_exec "rm -rf '$runtime_run_dir' && mkdir -p '$runtime_raw_dir'"
    fi
  fi

  tl_apply_patch_state "$state"
  local workload=("${TL_WORKLOAD_CMD[@]}" --output-json "$runtime_report_json")
  local workload_text
  workload_text="$(tl_runtime_env_prefix) $(tl_shell_join "${workload[@]}")"

  read -r -a extra_msprof_args <<< "${TRACELOOM_MSPROF_ARGS:-}"
  local application="$workload_text"
  if tl_in_container; then
    local wrapper="$runtime_run_dir/profile_workload.sh"
    if tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
      echo "+ docker exec $TRACELOOM_CONTAINER sh -lc \"cat > '$wrapper' <<'EOF'
#!/usr/bin/env sh
set -e
$workload_text
EOF
chmod +x '$wrapper'\""
    else
      tl_container_exec "cat > '$wrapper' <<'EOF'
#!/usr/bin/env sh
set -e
$workload_text
EOF
chmod +x '$wrapper'"
    fi
    application="$wrapper"
  fi
  local msprof_command="msprof --output='$runtime_raw_dir' --application='$application' ${TRACELOOM_MSPROF_ARGS:-} > '$runtime_log_file' 2>&1"
  if tl_in_container; then
    printf '+ docker exec %s sh -lc %q\n' "$TRACELOOM_CONTAINER" "$msprof_command"
  else
    echo "+ msprof --output=$runtime_raw_dir --application=$application ${extra_msprof_args[*]:-} > $runtime_log_file 2>&1"
  fi
  if ! tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
    if tl_in_container; then
      tl_container_exec "$msprof_command"
      rm -rf "$run_dir"
      mkdir -p "$run_dir"
      docker cp "$TRACELOOM_CONTAINER:$runtime_run_dir/." "$run_dir/"
    else
      msprof "--output=$raw_dir" "--application=$workload_text" "${extra_msprof_args[@]}" >"$log_file" 2>&1
    fi
  fi
}

profile_one baseline baseline
profile_one optimized optimized
tl_apply_patch_state baseline

paper_args=(
  "$TRACELOOM_PROJECT_ROOT/reproduce/run_reference.py"
  --out-root "$OUT_ROOT"
  decode-a2a-buffer-reuse
  --source-root "${TRACELOOM_DECODE_A2A_SOURCE_ROOT:-$TRACELOOM_PROJECT_ROOT/../template-of-thesis/experiments-data/run_20260507_npu3456}"
  --mode raw-analysis
  --baseline-run-dir "$OUT_ROOT/profiles/baseline"
  --optimized-run-dir "$OUT_ROOT/profiles/optimized"
)
if tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
  paper_args+=(--dry-run)
fi
PYTHONPATH="$TRACELOOM_PROJECT_ROOT${PYTHONPATH:+:$PYTHONPATH}" tl_run python3 "${paper_args[@]}"

echo "Profile pair: $OUT_ROOT/profiles"
echo "TraceLoom comparison: $OUT_ROOT/decode_a2a_buffer_reuse"
