#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ENV_FILE="${TRACELOOM_CANN_ENV:-$SCRIPT_DIR/reproduce/decode_a2a_buffer_reuse/local.env}"

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
source "$SCRIPT_DIR/reproduce/decode_a2a_buffer_reuse/common.sh"
tl_load_env "$ENV_FILE"
tl_configure_ascend_env
tl_check_host
tl_prepare_runtime
tl_build_workload_cmd

OUT_ROOT=$(tl_out_root)
PYTHON_BIN=${PYTHON:-python3}
TRACELOOM_CLI=("$PYTHON_BIN" -m traceloom)
export PYTHONPATH="$TRACELOOM_PROJECT_ROOT${PYTHONPATH:+:$PYTHONPATH}"

ANALYSIS_TOP_DEVICES=${TRACELOOM_ANALYSIS_TOP_DEVICES:-0}
ANALYSIS_DEVICES=${TRACELOOM_ANALYSIS_DEVICES:-}
ANALYSIS_MAX_MAIN_EVENTS=${TRACELOOM_ANALYSIS_MAX_MAIN_EVENTS:-5000}
ANALYSIS_MAX_MACRO_DEFS=${TRACELOOM_ANALYSIS_MAX_MACRO_DEFS:-32}
ANALYSIS_SUMMARY_TOP_LOOPS=${TRACELOOM_ANALYSIS_SUMMARY_TOP_LOOPS:-12}

write_profile_config() {
  local config_file=$1
  local profile_name=$2
  local profile_dir=$3
  local analysis_dir=$4
  local log_file=$5
  local workload_command=$6
  local docker_workdir="${TRACELOOM_CONTAINER_WORKDIR:-}"
  if tl_in_container && [[ -z "$docker_workdir" ]]; then
    docker_workdir=$(tl_runtime_vllm_ascend_dir)
  fi

  echo "+ ${TRACELOOM_CLI[*]} create config -o $config_file --force"
  "${TRACELOOM_CLI[@]}" create config -o "$config_file" --force >/dev/null

  TRACELOOM_CFG_PROFILE_NAME="$profile_name" \
  TRACELOOM_CFG_PROFILE_DIR="$profile_dir" \
  TRACELOOM_CFG_ANALYSIS_DIR="$analysis_dir" \
  TRACELOOM_CFG_LOG_FILE="$log_file" \
  TRACELOOM_CFG_WORKLOAD_COMMAND="$workload_command" \
  TRACELOOM_CFG_MSPROF_ARGS="${TRACELOOM_MSPROF_ARGS:-}" \
  TRACELOOM_CFG_ANALYSIS_TOP_DEVICES="$ANALYSIS_TOP_DEVICES" \
  TRACELOOM_CFG_ANALYSIS_DEVICES="$ANALYSIS_DEVICES" \
  TRACELOOM_CFG_ANALYSIS_MAX_MAIN_EVENTS="$ANALYSIS_MAX_MAIN_EVENTS" \
  TRACELOOM_CFG_ANALYSIS_MAX_MACRO_DEFS="$ANALYSIS_MAX_MACRO_DEFS" \
  TRACELOOM_CFG_ANALYSIS_SUMMARY_TOP_LOOPS="$ANALYSIS_SUMMARY_TOP_LOOPS" \
  TRACELOOM_CFG_DOCKER_ENABLED=$([[ -n "${TRACELOOM_CONTAINER:-}" ]] && echo true || echo false) \
  TRACELOOM_CFG_DOCKER_CONTAINER="${TRACELOOM_CONTAINER:-}" \
  TRACELOOM_CFG_DOCKER_WORKDIR="$docker_workdir" \
  "$PYTHON_BIN" - "$config_file" <<'PY'
import configparser
import os
import sys
from pathlib import Path

path = Path(sys.argv[1])
cfg = configparser.ConfigParser(interpolation=None)
cfg.read(path, encoding="utf-8")
cfg["profile"]["name"] = os.environ["TRACELOOM_CFG_PROFILE_NAME"]
cfg["paths"]["run_dir"] = str(Path(os.environ["TRACELOOM_CFG_PROFILE_DIR"]).parent)
cfg["paths"]["profile_dir"] = os.environ["TRACELOOM_CFG_PROFILE_DIR"]
cfg["paths"]["analysis_dir"] = os.environ["TRACELOOM_CFG_ANALYSIS_DIR"]
cfg["paths"]["log_file"] = os.environ["TRACELOOM_CFG_LOG_FILE"]
cfg["workload"]["cwd"] = "."
cfg["workload"]["command"] = os.environ["TRACELOOM_CFG_WORKLOAD_COMMAND"]
cfg["profiler"]["extra_args"] = os.environ["TRACELOOM_CFG_MSPROF_ARGS"]
cfg["analysis"]["top_devices_global"] = os.environ["TRACELOOM_CFG_ANALYSIS_TOP_DEVICES"]
cfg["analysis"]["devices"] = os.environ["TRACELOOM_CFG_ANALYSIS_DEVICES"]
cfg["analysis"]["max_main_events_per_device"] = os.environ["TRACELOOM_CFG_ANALYSIS_MAX_MAIN_EVENTS"]
cfg["analysis"]["max_macro_defs"] = os.environ["TRACELOOM_CFG_ANALYSIS_MAX_MACRO_DEFS"]
cfg["analysis"]["summary_top_loops"] = os.environ["TRACELOOM_CFG_ANALYSIS_SUMMARY_TOP_LOOPS"]
cfg["docker"]["enabled"] = os.environ["TRACELOOM_CFG_DOCKER_ENABLED"]
cfg["docker"]["container"] = os.environ["TRACELOOM_CFG_DOCKER_CONTAINER"]
cfg["docker"]["workdir"] = os.environ["TRACELOOM_CFG_DOCKER_WORKDIR"]
with path.open("w", encoding="utf-8") as f:
    cfg.write(f)
PY
}

run_analysis() {
  local profile_dir=$1
  local analysis_dir=$2
  local config_file=$3
  local command=(
    "${TRACELOOM_CLI[@]}"
    analysis "$profile_dir"
    --config "$config_file"
    --out-dir "$analysis_dir"
  )
  tl_run "${command[@]}"
  if ! tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
    for candidate in "$profile_dir/workload_result.json" "$(dirname "$profile_dir")/workload_result.json"; do
      if [[ -f "$candidate" ]]; then
        cp "$candidate" "$analysis_dir/workload_result.json"
        break
      fi
    done
  fi
}

write_workload_wrapper() {
  local wrapper_path=$1
  local workload_text=$2
  if tl_in_container; then
    if tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
      echo "+ docker exec $TRACELOOM_CONTAINER sh -lc \"cat > '$wrapper_path' <<'EOF'
#!/usr/bin/env sh
set -e
$workload_text
EOF
chmod +x '$wrapper_path'\""
    else
      tl_container_exec "cat > '$wrapper_path' <<'EOF'
#!/usr/bin/env sh
set -e
$workload_text
EOF
chmod +x '$wrapper_path'"
    fi
  else
    cat >"$wrapper_path" <<EOF
#!/usr/bin/env sh
set -e
$workload_text
EOF
    chmod +x "$wrapper_path"
  fi
}

profile_one() {
  local state=$1
  local tag=$2
  local run_dir="$OUT_ROOT/profiles/$tag"
  local raw_dir="$run_dir/msprof_raw"
  local analysis_dir="$run_dir/analysis"
  local config_file="$run_dir/traceloom.profile.ini"
  local log_file="$run_dir/workload.log"
  local report_json="$raw_dir/workload_result.json"

  local runtime_run_dir="$run_dir"
  local runtime_raw_dir="$raw_dir"
  local runtime_report_json="$report_json"
  local runtime_wrapper="$run_dir/profile_workload.sh"
  if tl_in_container; then
    runtime_run_dir="$(tl_remote_root)/profiles/$tag"
    runtime_raw_dir="$runtime_run_dir/msprof_raw"
    runtime_report_json="$runtime_raw_dir/workload_result.json"
    runtime_wrapper="$runtime_run_dir/profile_workload.sh"
  fi

  if tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
    echo "+ mkdir -p '$run_dir'"
    echo "+ rm -rf '$raw_dir' '$analysis_dir'"
    echo "+ mkdir -p '$raw_dir' '$analysis_dir'"
    mkdir -p "$raw_dir" "$analysis_dir"
  else
    mkdir -p "$run_dir"
    rm -rf "$raw_dir" "$analysis_dir"
    mkdir -p "$raw_dir" "$analysis_dir"
  fi
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
  write_workload_wrapper "$runtime_wrapper" "$workload_text"

  write_profile_config \
    "$config_file" \
    "decode-a2a-$tag" \
    "$runtime_raw_dir" \
    "$analysis_dir" \
    "$log_file" \
    "$runtime_wrapper"

  if tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
    "${TRACELOOM_CLI[@]}" run "$config_file" --dry-run
  else
    tl_run "${TRACELOOM_CLI[@]}" run "$config_file"
  fi

  if tl_in_container && ! tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
    mkdir -p "$run_dir"
    docker cp "$TRACELOOM_CONTAINER:$runtime_run_dir/." "$run_dir/"
  fi

  run_analysis "$raw_dir" "$analysis_dir" "$config_file"
}

if tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
  echo "+ mkdir -p '$OUT_ROOT/profiles'"
else
  mkdir -p "$OUT_ROOT/profiles"
fi
profile_one baseline baseline
profile_one optimized optimized
tl_apply_patch_state baseline

comparison_args=(
  "$TRACELOOM_PROJECT_ROOT/reproduce/decode_a2a_buffer_reuse.py"
  --out-root "$OUT_ROOT"
  --source-root "${TRACELOOM_DECODE_A2A_SOURCE_ROOT:-$TRACELOOM_PROJECT_ROOT/../template-of-thesis/experiments-data/run_20260507_npu3456}"
  --mode existing-analysis
  --baseline-analysis-dir "$OUT_ROOT/profiles/baseline/analysis"
  --optimized-analysis-dir "$OUT_ROOT/profiles/optimized/analysis"
)
if tl_bool_true "${TRACELOOM_DRY_RUN:-0}"; then
  echo "+ $PYTHON_BIN ${comparison_args[*]} --dry-run"
else
  "$PYTHON_BIN" "${comparison_args[@]}"
fi

echo "Profiles: $OUT_ROOT/profiles"
echo "Comparison: $OUT_ROOT/decode_a2a_buffer_reuse"
