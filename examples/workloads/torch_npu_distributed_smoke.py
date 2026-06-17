#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import time


def _rank_env(name: str, default: int) -> int:
    value = os.environ.get(name)
    return int(value) if value not in (None, "") else default


def main() -> int:
    parser = argparse.ArgumentParser(description="Tiny two-rank torch-npu smoke workload for TraceLoom profiling.")
    parser.add_argument("--iters", type=int, default=int(os.environ.get("TRACELOOM_SMOKE_ITERS", "8")))
    parser.add_argument("--size", type=int, default=int(os.environ.get("TRACELOOM_SMOKE_SIZE", "512")))
    parser.add_argument("--warmup", type=int, default=int(os.environ.get("TRACELOOM_SMOKE_WARMUP", "2")))
    args = parser.parse_args()

    import torch
    import torch.distributed as dist
    import torch_npu  # noqa: F401

    rank = _rank_env("RANK", 0)
    local_rank = _rank_env("LOCAL_RANK", rank)
    world_size = _rank_env("WORLD_SIZE", 1)
    torch.npu.set_device(local_rank)
    device = torch.device(f"npu:{local_rank}")

    dist.init_process_group(backend="hccl", rank=rank, world_size=world_size)

    x = torch.randn((args.size, args.size), dtype=torch.float16, device=device)
    w = torch.randn((args.size, args.size), dtype=torch.float16, device=device)

    torch.npu.synchronize()
    start = time.time()
    for step in range(args.warmup + args.iters):
        y = torch.matmul(x, w)
        y = torch.nn.functional.gelu(y)
        dist.all_reduce(y)
        x = y / float(world_size)
        torch.npu.synchronize()
        if rank == 0 and step >= args.warmup:
            print(f"[smoke] step={step - args.warmup + 1}/{args.iters}", flush=True)
    torch.npu.synchronize()
    elapsed = time.time() - start

    if rank == 0:
        print(
            f"[smoke] done world_size={world_size} size={args.size} "
            f"warmup={args.warmup} iters={args.iters} elapsed_s={elapsed:.3f}",
            flush=True,
        )

    dist.destroy_process_group()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
