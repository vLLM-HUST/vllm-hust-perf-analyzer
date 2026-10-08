"""Deterministic adversarial importer/observer acceptance (no model/network)."""

import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import time


cli, output = sys.argv[1:]
root = Path(output) / "inference-acceptance-tests"
shutil.rmtree(root, ignore_errors=True)
root.mkdir(parents=True)
source = root / "source.ndjson"
base = dict(
    schema_version=1,
    trace_id="1" * 32,
    span_id="1" * 16,
    event_id="e0",
    producer_id="p",
    clock_id="c",
    sequence=0,
    event_type="span_start",
    wall_time_ns=1000000,
    monotonic_ns=1000000,
    name="run",
    kind="pipeline",
)


def write(records, path=source):
    path.write_text("".join(json.dumps(record) + "\n" for record in records))


def run(*args, ok=True):
    process = subprocess.run(
        [cli, *map(str, args)], capture_output=True, text=True, timeout=30
    )
    assert (process.returncode == 0) == ok, process.stderr
    assert "PRIVATE_CANARY" not in process.stdout + process.stderr
    return process


def ingest(record, name, *, ok=True, summaries=False):
    write([record])
    paths = [root / (name + extension) for extension in [".db", ".html", ".json"]]
    run(
        "import-inference",
        source,
        "--output",
        paths[0],
        "--html-out",
        paths[1],
        "--perfetto-out",
        paths[2],
        *(["--include-summaries"] if summaries else []),
        ok=ok,
    )
    return paths


# Major/minor negotiation: absent and explicit zero are identical.
minor_db, _, _ = ingest(base, "minor")
write([dict(base, schema_minor=0)])
result = run("import-inference", source, "--output", minor_db)
assert "duplicates=1" in result.stderr
for version in [
    dict(base, schema_version=2),
    dict(base, schema_minor=1),
    dict(base, schema_version="1.1"),
    dict(base, schema_version=1.1),
    dict(base, extension="PRIVATE_CANARY"),
]:
    write([version])
    run("import-inference", source, "--output", minor_db, ok=False)
with sqlite3.connect(minor_db) as database:
    assert database.execute(
        "select count(*) from traceloom_inference_event"
    ).fetchone()[0] == 1

# Unknown envelope fields reject; unknown attributes are removed.
private = [
    "Authorization",
    "token",
    "url",
    "raw_prompt",
    "raw_output",
    "exception_message",
    "hidden_reasoning",
    "chain_of_thought",
]
for index, key in enumerate(private):
    ingest(dict(base, **{key: "PRIVATE_CANARY"}), f"envelope-{index}", ok=False)
paths = ingest(
    dict(
        base,
        attributes={key: "PRIVATE_CANARY" for key in private},
        decision_summary="PRIVATE_CANARY raw prompt and hidden reasoning",
    ),
    "removed",
)
assert all(b"PRIVATE_CANARY" not in path.read_bytes() for path in paths)

# Allowed fields cannot launder recognizable credentials, URIs, or paths.
unsafe = [
    "Authorization:Bearer_PRIVATE_CANARY",
    "sk-PRIVATE_CANARY",
    "file:PRIVATE_CANARY",
    "https://example.invalid/PRIVATE_CANARY",
    "../../PRIVATE_CANARY",
    "C:/PRIVATE_CANARY",
    "<script>PRIVATE_CANARY</script>",
    "hidden_reasoning_PRIVATE_CANARY",
]
for index, value in enumerate(unsafe):
    ingest(dict(base, evidence_refs=[value]), f"ref-{index}", ok=False)
    ingest(dict(base, attributes={"model": value}), f"model-{index}", ok=False)
    ingest(
        dict(base, decision_summary=value),
        f"summary-{index}",
        ok=False,
        summaries=True,
    )
long_summary = "Public evidence confirms the selected operation. " * 5
assert len(long_summary.encode()) <= 256
paths = ingest(
    dict(base, decision_summary=long_summary, evidence_refs=["document-hmac-1234"]),
    "long-summary",
    summaries=True,
)
assert "summary-preview" in paths[1].read_text()
assert long_summary in paths[1].read_text()
ingest(
    dict(base, decision_summary="x" * 257),
    "oversize-summary",
    ok=False,
    summaries=True,
)

# An externally modified or old DB cannot bypass current export policy.
with sqlite3.connect(paths[0]) as database:
    payload = json.loads(
        database.execute("select payload from traceloom_inference_event").fetchone()[0]
    )
    payload["attributes"] = {"model": "sk-PRIVATE_CANARY"}
    database.execute(
        "update traceloom_inference_event set payload=?", (json.dumps(payload),)
    )
previous = paths[1].read_bytes()
run("export-inference", paths[0], "--html-out", paths[1], ok=False)
assert paths[1].read_bytes() == previous

# Empty state is valid; malformed complete records are not.
source.write_bytes(b"")
run(
    "import-inference",
    source,
    "--output",
    root / "empty.db",
    "--html-out",
    root / "empty.html",
)
assert "No observations yet" in (root / "empty.html").read_text()
for index, data in enumerate(
    [b"{\n", b"[]\n", b'{"a":1,"a":2}\n', b"\xff\n", b"x" * 16384 + b"\n"]
):
    source.write_bytes(data)
    run("import-inference", source, "--output", root / f"malformed-{index}.db", ok=False)

# Deep/wide scopes remain bounded without manufacturing dependency evidence.
records = []
for index in range(1, 257):
    record = dict(
        base, event_id=f"s{index}", span_id=f"{index:016x}", sequence=index
    )
    if index > 1:
        record["parent_span_id"] = f"{index - 1:016x}"
    records.append(record)
for index in range(257, 513):
    records.append(
        dict(
            base,
            event_id=f"s{index}",
            span_id=f"{index:016x}",
            sequence=index,
            parent_span_id=f"{1:016x}",
        )
    )
write(records)
run(
    "import-inference",
    source,
    "--output",
    root / "layout.db",
    "--html-out",
    root / "layout.html",
)
html = (root / "layout.html").read_text()
assert "longest observed dependency path: unavailable" in html
assert "padding-left:168" in html and "padding-left:182" not in html

# Bound browser projection independently; prior output survives failure.
records = [
    dict(
        base, event_id=f"s{index}", span_id=f"{index:016x}", sequence=index
    )
    for index in range(1, 10002)
]
write(records)
run("import-inference", source, "--output", root / "projection-cap.db")
sentinel = root / "bounded.html"
sentinel.write_text("previous valid projection")
run(
    "export-inference",
    root / "projection-cap.db",
    "--html-out",
    sentinel,
    ok=False,
)
assert sentinel.read_text() == "previous valid projection"

# Polling: partial tails, inode replacement, rotation, deduplication, failure.
spool = root / "spool"
spool.mkdir()
active = spool / "active.ndjson"
first = dict(base, event_id="e1", span_id="1" * 16, sequence=1)
second = dict(first, event_id="e2", span_id="2" * 16, sequence=2)
write([first], active)
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
    stderr=subprocess.PIPE,
    text=True,
)
latencies = []


def wait_count(count):
    started = time.monotonic()
    deadline = started + 8
    while time.monotonic() < deadline:
        if watch_html.exists() and f"{count} observed spans" in watch_html.read_text():
            latencies.append(time.monotonic() - started)
            return
        assert process.poll() is None, "observer exited on valid rotation"
        time.sleep(0.05)
    raise AssertionError("watch refresh deadline exceeded")


try:
    wait_count(1)
    old = active.stat()
    replacement = spool / "next.tmp"
    write([second], replacement)
    assert replacement.stat().st_size == old.st_size
    os.utime(replacement, ns=(old.st_atime_ns, old.st_mtime_ns))
    os.replace(replacement, active)
    wait_count(2)
    active.rename(spool / "rotated.ndjson")
    third = dict(first, event_id="e3", span_id="3" * 16, sequence=3)
    encoded = json.dumps(third)
    active.write_text(encoded[: len(encoded) // 2])
    time.sleep(1.2)
    assert "2 observed spans" in watch_html.read_text()
    with active.open("a") as stream:
        stream.write(encoded[len(encoded) // 2 :] + "\n")
    wait_count(3)
    shutil.copyfile(active, spool / "duplicate.ndjson")
    time.sleep(1.2)
    with sqlite3.connect(watch_db) as database:
        assert database.execute(
            "select count(*) from traceloom_inference_event"
        ).fetchone()[0] == 3
    (spool / "invalid.ndjson").write_text(
        '{"Authorization":"PRIVATE_CANARY"}\n'
    )
    process.wait(timeout=8)
    assert process.returncode != 0
    assert 'http-equiv="refresh"' not in watch_html.read_text()
    assert "PRIVATE_CANARY" not in process.stderr.read()
finally:
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=5)

(root / "receipt.json").write_text(
    json.dumps(
        {
            "watch_publication_seconds": latencies,
            "max_publication_seconds": max(latencies),
            "privacy_cases": len(unsafe) * 3 + len(private),
            "deep_scopes": 256,
            "wide_scopes": 256,
        },
        indent=2,
    )
    + "\n"
)
print("Adversarial privacy/version/layout/projection-budget/live-rotation gates passed")
