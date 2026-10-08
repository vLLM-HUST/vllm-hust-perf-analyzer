"""Consume a hash-receipted producer workflow; no synthetic substitute.

Usage: python joint_acceptance.py TRACELOOM_PATH MANIFEST OUTPUT_DIRECTORY
The producer owns fixture generation; generated artifacts remain untracked.
"""

import hashlib
import json
from pathlib import Path
import random
import sqlite3
import subprocess
import sys
import time


cli, manifest_path, output = sys.argv[1:]
manifest = json.loads(Path(manifest_path).read_text())
source = Path(manifest["fixture"])
raw = source.read_bytes()
assert hashlib.sha256(raw).hexdigest() == manifest["sha256"]
records = [json.loads(line) for line in raw.splitlines()]
assert len(records) == manifest["events"]
root = Path(output)
root.mkdir(parents=True, exist_ok=False)
(root / "producer.ndjson").write_bytes(raw)
(root / "producer-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
receipt = {"producer_sha256": manifest["sha256"], "events": len(records), "cases": {}}


def run(*args, ok=True):
    process = subprocess.run(
        [cli, *map(str, args)], capture_output=True, text=True, timeout=30
    )
    assert (process.returncode == 0) == ok, process.stderr
    return process


def rows(path):
    with sqlite3.connect(path) as database:
        return database.execute(
            "select trace_id,event_id,payload from traceloom_inference_event "
            "order by trace_id,event_id"
        ).fetchall()


artifact, html, perfetto = root / "analysis.db", root / "trace.html", root / "trace.json"
run(
    "import-inference",
    root / "producer.ndjson",
    "--output",
    artifact,
    "--html-out",
    html,
    "--perfetto-out",
    perfetto,
)
expected = rows(artifact)
assert len(expected) == len(records)
run("import-inference", root / "producer.ndjson", "--output", artifact)
assert rows(artifact) == expected
receipt["cases"]["idempotent"] = True
shuffled = list(records)
random.Random(63).shuffle(shuffled)
mutated = root / "mutated.ndjson"
mutated.write_text("".join(json.dumps(record) + "\n" for record in shuffled))
run("import-inference", mutated, "--output", root / "reordered.db")
assert rows(root / "reordered.db") == expected
receipt["cases"]["out_of_order"] = True
for name, extra in [
    ("minor_zero", {"schema_minor": 0}),
    ("future_minor", {"schema_minor": 1}),
    ("future_major", {"schema_version": 2}),
    ("unknown_field", {"unknown_field": "rejected"}),
]:
    mutated.write_text(
        "".join(json.dumps(dict(record, **extra)) + "\n" for record in records)
    )
    run("import-inference", mutated, "--output", artifact, ok=name == "minor_zero")
    assert rows(artifact) == expected
    receipt["cases"][name] = True
for name, bad in [("oversize", b"x" * 16384 + b"\n"), ("malformed", b"{\n")]:
    mutated.write_bytes(raw + bad)
    run("import-inference", mutated, "--output", artifact, ok=False)
    assert rows(artifact) == expected
    receipt["cases"][name] = True
with sqlite3.connect(artifact) as database:
    receipt["span_statuses"] = dict(
        database.execute(
            "select status,count(*) from traceloom_v_inference_span group by status"
        )
    )
    receipt["trace_states"] = database.execute(
        "select state,terminal_status,count(*) from traceloom_v_inference_trace "
        "group by state,terminal_status"
    ).fetchall()
    receipt["dropped_event_receipts"] = database.execute(
        "select max(json_extract(payload,'$.attributes.dropped_events')) "
        "from traceloom_inference_event"
    ).fetchone()[0]
assert receipt["span_statuses"].get("error", 0) > 0
assert receipt["span_statuses"].get("cancelled", 0) > 0

# Follow actual producer lines through a partial write and rotated segment.
spool = root / "spool"
spool.mkdir()
active = spool / "active.ndjson"
lines = raw.splitlines(keepends=True)
active.write_bytes(lines[0])
watch_db, watch_html = root / "watch.db", root / "watch.html"
process = subprocess.Popen(
    [
        cli,
        "import-inference",
        str(spool),
        "--output",
        str(watch_db),
        "--html-out",
        str(watch_html),
        "--follow",
    ],
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)


def wait_count(count):
    started = time.monotonic()
    while time.monotonic() - started < 8:
        if watch_db.exists():
            try:
                if len(rows(watch_db)) == count and watch_html.exists():
                    return time.monotonic() - started
            except sqlite3.OperationalError:
                pass
        assert process.poll() is None
        time.sleep(0.05)
    raise AssertionError("joint live import timeout")


try:
    wait_count(1)
    active.rename(spool / "rotated.ndjson")
    remaining = b"".join(lines[1:])
    cut = len(remaining) // 2
    active.write_bytes(remaining[:cut])
    time.sleep(1.2)
    with active.open("ab") as stream:
        stream.write(remaining[cut:])
    receipt["live_publication_seconds"] = wait_count(len(records))
finally:
    process.terminate()
    process.wait(timeout=5)
assert process.returncode == 0 and rows(watch_db) == expected
assert 'http-equiv="refresh"' not in watch_html.read_text()
receipt["cases"]["partial_tail_and_directory_rotation"] = True
receipt["artifacts"] = {
    path.name: {
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "bytes": path.stat().st_size,
    }
    for path in [artifact, html, perfetto]
}
(root / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
print(json.dumps(receipt, indent=2))
