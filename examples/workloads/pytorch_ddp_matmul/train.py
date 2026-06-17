"""Small PyTorch distributed matmul workload for profiler smoke runs."""

from __future__ import annotations

import argparse
import json
import os
import sys
import time


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--steps", type=int, default=20)
    parser.add_argument("--size", type=int, default=2048)
    parser.add_argument("--warmup", type=int, default=5)
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        import torch
        import torch.distributed as dist
    except ImportError:
        print("This workload requires PyTorch in the user's environment.", file=sys.stderr)
        return 2

    world_size = int(os.environ.get("WORLD_SIZE", "1"))
    local_rank = int(os.environ.get("LOCAL_RANK", "0"))
    rank = int(os.environ.get("RANK", "0"))
    use_cuda = torch.cuda.is_available()
    device = torch.device(f"cuda:{local_rank}" if use_cuda else "cpu")

    if use_cuda:
        torch.cuda.set_device(device)

    if world_size > 1:
        backend = "nccl" if use_cuda else "gloo"
        dist.init_process_group(backend=backend)

    a = torch.randn(args.size, args.size, device=device)
    b = torch.randn(args.size, args.size, device=device)

    start = time.perf_counter()
    for step in range(args.warmup + args.steps):
        c = a @ b
        if world_size > 1:
            dist.all_reduce(c)
        if use_cuda:
            torch.cuda.synchronize()
        if step == args.warmup - 1:
            start = time.perf_counter()

    elapsed_s = time.perf_counter() - start

    if world_size > 1:
        dist.destroy_process_group()

    if rank == 0:
        print(
            json.dumps(
                {
                    "steps": args.steps,
                    "size": args.size,
                    "world_size": world_size,
                    "device": str(device),
                    "elapsed_s": elapsed_s,
                },
                sort_keys=True,
            )
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
