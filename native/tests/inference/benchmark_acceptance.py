"""Three fresh offline 10k-event imports/projections; no inference claims."""

import json
from pathlib import Path
import statistics
import subprocess
import sys


cli, output = sys.argv[1:]
root = Path(output)
root.mkdir(parents=True, exist_ok=False)
source = root / "ten-thousand.ndjson"
with source.open("w") as stream:
    for sequence in range(10000):
        record = dict(
            schema_version=1,
            event_id=f"e{sequence}",
            trace_id="3" * 32,
            span_id=f"{sequence // 2 + 1:016x}",
            producer_id="bench",
            clock_id="clock",
            sequence=sequence,
            event_type="span_end" if sequence % 2 else "span_start",
            monotonic_ns=2**53 + sequence * 1000,
            wall_time_ns=1788919000000000000 + sequence * 1000,
        )
        record.update(
            {"status": "ok"}
            if sequence % 2
            else {"name": "step.execute", "kind": "step"}
        )
        stream.write(json.dumps(record, separators=(",", ":")) + "\n")


def measure(args, name):
    receipt = root / f"{name}.time"
    subprocess.run(
        ["/usr/bin/time", "-o", str(receipt), "-f", "%e %M", cli, *map(str, args)],
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    elapsed, rss = receipt.read_text().split()
    return {"seconds": float(elapsed), "peak_rss_kib": int(rss)}


samples = []
for index in range(3):
    database, html, perfetto = [
        root / f"run-{index}{extension}" for extension in [".db", ".html", ".json"]
    ]
    imported = measure(
        ["import-inference", source, "--output", database], f"import-{index}"
    )
    projected = measure(
        [
            "export-inference",
            database,
            "--html-out",
            html,
            "--perfetto-out",
            perfetto,
        ],
        f"project-{index}",
    )
    samples.append(
        {
            "import": imported,
            "projection": projected,
            "db_bytes": database.stat().st_size,
            "html_bytes": html.stat().st_size,
            "perfetto_bytes": perfetto.stat().st_size,
        }
    )

receipt = {
    "events": 10000,
    "spans": 5000,
    "source_bytes": source.stat().st_size,
    "samples": samples,
    "import_median_seconds": statistics.median(
        sample["import"]["seconds"] for sample in samples
    ),
    "import_peak_rss_kib": max(
        sample["import"]["peak_rss_kib"] for sample in samples
    ),
    "projection_median_seconds": statistics.median(
        sample["projection"]["seconds"] for sample in samples
    ),
    "projection_peak_rss_kib": max(
        sample["projection"]["peak_rss_kib"] for sample in samples
    ),
}
(root / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
print(json.dumps(receipt, indent=2))
assert receipt["import_median_seconds"] <= 3
assert receipt["import_peak_rss_kib"] <= 128 * 1024
assert receipt["projection_median_seconds"] <= 5
assert receipt["projection_peak_rss_kib"] <= 128 * 1024
