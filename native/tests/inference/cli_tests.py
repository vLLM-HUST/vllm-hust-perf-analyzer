"""Native CLI contract tests; Python is used only to inspect JSON and drive signals."""
import json
import pathlib
import shutil
import sqlite3
import subprocess
import sys
import time

cli, fixture, output = sys.argv[1:]
root = pathlib.Path(output) / 'inference-cli-tests'
shutil.rmtree(root, ignore_errors=True)
root.mkdir(parents=True)
source, db, html, perfetto = [root / x for x in ['events.ndjson', 'analysis.db', 'view.html', 'trace.json']]
source.write_bytes(pathlib.Path(fixture).read_bytes())


def run(*args, ok=True):
    result = subprocess.run([cli, *map(str, args)], capture_output=True, text=True, timeout=20)
    assert (result.returncode == 0) == ok, result.stderr
    return result


run('import-inference', source, '--output', db, '--html-out', html, '--perfetto-out', perfetto)
projection = json.loads(perfetto.read_text())
spans = [e for e in projection['traceEvents'] if e.get('cat', '').startswith('inference.') and e['ph'] in {'X', 'i'}]
assert any(e.get('cat') == 'inference.dependency' and e['ph'] == 's' for e in projection['traceEvents'])
assert len(spans) == 11  # 10 spans plus one observation
assert any(e['name'] == 'model.progress' and e['ph'] == 'i' for e in spans)
assert all(e['ts'] >= 0 for e in spans)
assert any(e.get('dur') == 45000 for e in spans)
assert any(e['args'].get('status') == 'open' and 'dur' not in e for e in spans)
assert any(e['ph'] == 'C' and e['args']['count'] == 2 for e in projection['traceEvents'])
assert 'Selected three' not in html.read_text()
assert 'data-trace-id="11111111111111111111111111111111"' in html.read_text()
run('export-inference', db, '--perfetto-out', root / 'second.json')
assert perfetto.read_bytes() == (root / 'second.json').read_bytes()
run('import-inference', source, '--output', source, ok=False)
run('import-inference', source, '--output', db, '--html-out', source, ok=False)
run('export-inference', db, '--html-out', db, ok=False)
run('import-inference', source, '--max-events', '-1', ok=False)
# Live tail: watcher sees an open root, then its end. A partial line is deferred.
records = [json.loads(line) for line in source.read_text().splitlines()]
start = records[0]
end = next(r for r in records if r['event_type'] == 'span_end' and r['span_id'] == start['span_id'])
source = root / 'live.ndjson'
source.write_text(json.dumps(start) + '\n')
live_db, live_html = root / 'live.db', root / 'live.html'
process = subprocess.Popen([cli, 'import-inference', str(source), '--output', str(live_db), '--html-out', str(live_html), '--follow'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def eventually(check):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if check():
            return
        assert process.poll() is None, 'watcher exited early'
        time.sleep(.1)
    raise AssertionError('live snapshot did not advance')


try:
    eventually(lambda: live_html.exists() and 'duration unknown' in live_html.read_text())
    encoded = json.dumps(end)
    with source.open('a') as stream:
        stream.write(encoded[:len(encoded)//2])
    time.sleep(1.2)
    assert 'duration unknown' in live_html.read_text()
    with source.open('a') as stream:
        stream.write(encoded[len(encoded)//2:] + '\n')
    eventually(lambda: '100.000 ms' in live_html.read_text())
finally:
    process.terminate()
    process.wait(timeout=5)
assert process.returncode == 0
assert 'http-equiv="refresh"' not in live_html.read_text()
# Reordering, large nanosecond integers, wall jumps and wrong clock evidence.
base = dict(start)
base['event_id'], base['sequence'] = 'precision-start', 0
base['monotonic_ns'] = 2**53 + 1
finish = dict(end)
finish['event_id'], finish['sequence'] = 'precision-end', 1
finish['monotonic_ns'], finish['wall_time_ns'] = 2**53 + 8, 1
source.write_text(json.dumps(finish) + '\n' + json.dumps(base) + '\n')
precision = root / 'precision.db'
run('import-inference', source, '--output', precision)
with sqlite3.connect(precision) as conn:
    assert conn.execute('select duration_ns from traceloom_v_inference_span').fetchone()[0] == 7
for bad in [True, 1.5, -1, 2**63]:
    broken = dict(base, monotonic_ns=bad)
    source.write_text(json.dumps(broken) + '\n')
    run('import-inference', source, '--output', root / 'bad.db', ok=False)
# A span_event without lifecycle evidence is retained and exportable.
observation = dict(base, event_type='span_event')
source.write_text(json.dumps(observation) + '\n')
run('import-inference', source, '--output', root / 'orphan.db', '--perfetto-out', root / 'orphan.json')
assert all(e['ts'] >= 0 for e in json.loads((root / 'orphan.json').read_text())['traceEvents'] if 'ts' in e)
print('CLI JSON, privacy, live append, signal, precision and orphan checks passed')
# Equivalent normalized records deduplicate despite key/whitespace order.
source.write_text(json.dumps(base, sort_keys=True, indent=None) + '\n')
run('import-inference', source, '--output', precision)
with sqlite3.connect(precision) as conn:
    assert conn.execute('select count(*) from traceloom_inference_event').fetchone()[0] == 2
# Cross-clock dependencies remain queryable but cannot produce timing paths/flows.
a = dict(base, event_id='a-start', sequence=0, monotonic_ns=100)
az = dict(finish, event_id='a-end', sequence=1, monotonic_ns=200)
b = dict(base, event_id='b-start', sequence=0, span_id='0000000000000009', producer_id='other', clock_id='other-clock', monotonic_ns=1, links=[a['span_id']])
bz = dict(finish, event_id='b-end', sequence=1, span_id=b['span_id'], producer_id='other', clock_id='other-clock', monotonic_ns=10)
source.write_text(''.join(json.dumps(r) + '\n' for r in [a, az, b, bz]))
cross = root / 'cross.db'
run('import-inference', source, '--output', cross, '--perfetto-out', root / 'cross.json')
with sqlite3.connect(cross) as conn:
    assert conn.execute('select timing_state from traceloom_v_inference_dependency').fetchone()[0] == 'cross_clock'
    assert conn.execute('select count(*) from traceloom_inference_span_metric where observed_path_ns is not null').fetchone()[0] == 0
cross_events = json.loads((root / 'cross.json').read_text())['traceEvents']
assert not any(e.get('cat') == 'inference.dependency' for e in cross_events)
assert len({e['pid'] for e in cross_events if e['ph'] == 'X'}) == 2
# Same-clock impossible dependency must be rejected atomically.
b.update(producer_id=a['producer_id'], clock_id=a['clock_id'], sequence=2)
bz.update(producer_id=a['producer_id'], clock_id=a['clock_id'], sequence=3)
source.write_text(''.join(json.dumps(r) + '\n' for r in [a, az, b, bz]))
run('import-inference', source, '--output', root / 'overlap.db', ok=False)
# Directory watch follows newly rotated neutral NDJSON segments.
spool = root / 'spool'
spool.mkdir()
(spool / 'segment-1.ndjson').write_text(json.dumps(base) + '\n')
dir_db, dir_html = root / 'directory.db', root / 'directory.html'
process = subprocess.Popen([cli, 'import-inference', str(spool), '--output', str(dir_db), '--html-out', str(dir_html), '--follow'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    eventually(lambda: dir_html.exists() and 'duration unknown' in dir_html.read_text())
    (spool / 'segment-2.ndjson').write_text(json.dumps(finish) + '\n')
    eventually(lambda: 'duration unknown' not in dir_html.read_text())
finally:
    process.terminate()
    process.wait(timeout=5)
assert process.returncode == 0
with sqlite3.connect(dir_db) as conn:
    assert conn.execute('select duration_ns from traceloom_v_inference_span').fetchone()[0] == 7
# File budget is enforced before reading or allocating the payload.
with source.open('wb') as stream:
    stream.truncate(64 * 1024 * 1024 + 1)
run('import-inference', source, '--output', root / 'oversize.db', ok=False)
