#!/usr/bin/env python3
"""Run the repository-bundled TraceLoom paper-artifact ledger."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--traceloom", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    tools = Path(__file__).resolve().parent / "tools"
    checks = (
        ("exact-interleaved", tools / "verify_ascend_interleaved.py"),
        ("exact-tp2-composition", tools / "verify_ascend_tp2_exact.py"),
        (
            "fresh-tp2-preregistered-negative",
            tools / "verify_ascend_tp2_fresh_negative.py",
        ),
        ("mapped-gather-perturbation", tools / "verify_ascend_mapped_gather.py"),
        ("medium-folding", tools / "verify_kickstart_folding.py"),
        ("workflow-comparison", tools / "verify_workflow_comparison.py"),
    )
    for claim, verifier in checks:
        print(f"[{claim}]", flush=True)
        subprocess.run(
            [
                sys.executable,
                str(verifier),
                "--traceloom",
                str(args.traceloom.resolve()),
            ],
            check=True,
        )
    print(f"artifact ledger: {len(checks)}/{len(checks)} PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
