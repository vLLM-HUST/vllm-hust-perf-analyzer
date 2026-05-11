#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import time
import traceback
from pathlib import Path
from typing import Any


def _env_str(*names: str, default: str = "") -> str:
    for name in names:
        value = os.environ.get(name)
        if value is not None and value != "":
            return value
    return default


def _env_int(*names: str, default: int) -> int:
    raw = _env_str(*names)
    if raw == "":
        return default
    return int(raw)


def _env_float(*names: str, default: float) -> float:
    raw = _env_str(*names)
    if raw == "":
        return default
    return float(raw)


def _env_bool(*names: str, default: bool) -> bool:
    raw = _env_str(*names)
    if raw == "":
        return default
    return raw.strip().lower() in {"1", "true", "yes", "on"}


def _write_json(path: str, payload: dict[str, Any]) -> None:
    if not path:
        return
    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Distributed vLLM smoke workload")
    parser.add_argument("--model", default=_env_str("VLLM_SMOKE_MODEL"))
    parser.add_argument("--tp", type=int, default=_env_int("VLLM_SMOKE_TP", "SMOKE_TP", default=2))
    parser.add_argument("--pp", type=int, default=_env_int("VLLM_SMOKE_PP", "SMOKE_PP", default=2))
    parser.add_argument(
        "--max-model-len",
        type=int,
        default=_env_int("VLLM_SMOKE_MAX_MODEL_LEN", "SMOKE_MAX_MODEL_LEN", default=1024),
    )
    parser.add_argument(
        "--max-tokens",
        type=int,
        default=_env_int("VLLM_SMOKE_MAX_TOKENS", "SMOKE_MAX_TOKENS", default=32),
    )
    parser.add_argument(
        "--batch-size",
        type=int,
        default=_env_int("VLLM_SMOKE_BATCH_SIZE", "SMOKE_BATCH_SIZE", default=1),
    )
    parser.add_argument("--rounds", type=int, default=_env_int("VLLM_SMOKE_ROUNDS", "SMOKE_ROUNDS", default=1))
    parser.add_argument(
        "--dispatch-mode",
        choices=("round", "dense"),
        default=_env_str("VLLM_SMOKE_DISPATCH_MODE", "SMOKE_DISPATCH_MODE", default="round"),
        help="round: call generate once per round; dense: submit all requests in one generate call",
    )
    parser.add_argument(
        "--temperature",
        type=float,
        default=_env_float("VLLM_SMOKE_TEMPERATURE", "SMOKE_TEMPERATURE", default=0.0),
    )
    parser.add_argument(
        "--prompt",
        default=_env_str(
            "VLLM_SMOKE_PROMPT",
            "SMOKE_PROMPT",
            default="Explain the purpose of msprof in one sentence.",
        ),
    )
    parser.add_argument("--dtype", default=_env_str("VLLM_SMOKE_DTYPE", default="bfloat16"))
    parser.add_argument("--hf-overrides-json", default=_env_str("VLLM_SMOKE_HF_OVERRIDES_JSON", "SMOKE_HF_OVERRIDES_JSON"))
    parser.add_argument("--seed", type=int, default=_env_int("VLLM_SMOKE_SEED", default=0))
    parser.add_argument("--output-json", default=_env_str("WORKLOAD_OUTPUT_JSON", "SMOKE_OUTPUT_JSON"))
    parser.add_argument(
        "--trust-remote-code",
        action="store_true",
        dest="trust_remote_code",
        help="Enable trust_remote_code for model loading",
    )
    parser.add_argument(
        "--no-trust-remote-code",
        action="store_false",
        dest="trust_remote_code",
        help="Disable trust_remote_code for model loading",
    )
    parser.set_defaults(trust_remote_code=_env_bool("VLLM_SMOKE_TRUST_REMOTE_CODE", "SMOKE_TRUST_REMOTE_CODE", default=False))
    return parser


def main() -> int:
    parser = _build_parser()
    args = parser.parse_args()

    if not args.model:
        raise RuntimeError("model is required (set --model or VLLM_SMOKE_MODEL)")

    args.batch_size = max(1, args.batch_size)
    args.rounds = max(1, args.rounds)

    hf_overrides = None
    if args.hf_overrides_json:
        hf_overrides = json.loads(args.hf_overrides_json)

    print(f"[workload] model={args.model}")
    print(f"[workload] tp={args.tp} pp={args.pp} dtype={args.dtype}")
    print(f"[workload] rounds={args.rounds} batch_size={args.batch_size} max_tokens={args.max_tokens}")
    print(f"[workload] dispatch_mode={args.dispatch_mode}")

    total_start = time.time()
    init_seconds = 0.0
    generate_seconds = 0.0

    try:
        from vllm import LLM, SamplingParams

        init_start = time.time()
        llm = LLM(
            model=args.model,
            tensor_parallel_size=args.tp,
            pipeline_parallel_size=args.pp,
            dtype=args.dtype,
            max_model_len=args.max_model_len,
            trust_remote_code=args.trust_remote_code,
            hf_overrides=hf_overrides,
            seed=args.seed,
        )
        init_seconds = time.time() - init_start

        sampling = SamplingParams(max_tokens=args.max_tokens, temperature=args.temperature)

        round_latencies: list[float] = []
        generated_tokens_estimate = 0
        first_output_text = ""
        total_requests = args.rounds * args.batch_size

        gen_start = time.time()
        if args.dispatch_mode == "dense":
            # Put request id at the prompt prefix to avoid shared-prefix reuse.
            prompts = [f"[request={i}]\\n{args.prompt}" for i in range(total_requests)]
            t0 = time.time()
            outputs = llm.generate(prompts, sampling)
            round_latencies.append(time.time() - t0)

            if outputs and outputs[0].outputs:
                first_output_text = outputs[0].outputs[0].text or ""

            for item in outputs:
                if item.outputs:
                    text = item.outputs[0].text or ""
                    generated_tokens_estimate += len(text.split())
        else:
            for r in range(args.rounds):
                prompts = [f"{args.prompt}\\n[round={r} request={i}]" for i in range(args.batch_size)]
                t0 = time.time()
                outputs = llm.generate(prompts, sampling)
                round_latencies.append(time.time() - t0)

                if outputs and outputs[0].outputs:
                    first_output_text = outputs[0].outputs[0].text or ""

                for item in outputs:
                    if item.outputs:
                        text = item.outputs[0].text or ""
                        generated_tokens_estimate += len(text.split())

        generate_seconds = time.time() - gen_start
        total_seconds = time.time() - total_start

        payload = {
            "status": "ok",
            "model": args.model,
            "tp": args.tp,
            "pp": args.pp,
            "dtype": args.dtype,
            "max_model_len": args.max_model_len,
            "max_tokens": args.max_tokens,
            "batch_size": args.batch_size,
            "rounds": args.rounds,
            "dispatch_mode": args.dispatch_mode,
            "temperature": args.temperature,
            "prompt": args.prompt,
            "trust_remote_code": args.trust_remote_code,
            "hf_overrides": hf_overrides,
            "seed": args.seed,
            "total_requests": total_requests,
            "generated_tokens_estimate": generated_tokens_estimate,
            "first_output_text": first_output_text,
            "avg_round_seconds": round(sum(round_latencies) / max(len(round_latencies), 1), 4),
            "request_throughput_rps": round(total_requests / max(generate_seconds, 1e-9), 4),
            "init_seconds": round(init_seconds, 4),
            "generate_seconds": round(generate_seconds, 4),
            "total_seconds": round(total_seconds, 4),
            "timestamp": int(time.time()),
        }
        _write_json(args.output_json, payload)

        print(f"[workload] init_seconds={payload['init_seconds']}")
        print(f"[workload] generate_seconds={payload['generate_seconds']}")
        print(f"[workload] throughput_rps={payload['request_throughput_rps']}")
        print("[workload] done")
        return 0

    except Exception as exc:
        total_seconds = time.time() - total_start
        failed = {
            "status": "error",
            "model": args.model,
            "tp": args.tp,
            "pp": args.pp,
            "dtype": args.dtype,
            "max_model_len": args.max_model_len,
            "max_tokens": args.max_tokens,
            "batch_size": args.batch_size,
            "rounds": args.rounds,
            "dispatch_mode": args.dispatch_mode,
            "temperature": args.temperature,
            "prompt": args.prompt,
            "trust_remote_code": args.trust_remote_code,
            "hf_overrides": hf_overrides,
            "seed": args.seed,
            "error": str(exc),
            "traceback": traceback.format_exc(),
            "init_seconds": round(init_seconds, 4),
            "generate_seconds": round(generate_seconds, 4),
            "total_seconds": round(total_seconds, 4),
            "timestamp": int(time.time()),
        }
        _write_json(args.output_json, failed)
        print("[workload][error]", exc)
        raise


if __name__ == "__main__":
    raise SystemExit(main())
