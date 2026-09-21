"""Exact multi-launch replay must not own ordinary work in its envelope."""
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

binary = sys.argv[1]
fixture = Path(__file__).resolve().parents[1] / 'fixtures/ascend_sqlite/exact_hlt'


def run(*args):
    p = subprocess.run([binary, *map(str, args)], capture_output=True, text=True, timeout=60)
    assert p.returncode == 0, p.stderr


with tempfile.TemporaryDirectory(prefix='traceloom-replay-gap-') as directory:
    root = Path(directory)
    raw = root / 'msprof.db'
    for relative in ('msprof.sql', 'host/sqlite/stream_info.sql'):
        target = root / Path(relative).with_suffix('.db')
        target.parent.mkdir(parents=True, exist_ok=True)
        with sqlite3.connect(target) as db:
            db.executescript((fixture / relative).read_text())
    baseline = root / 'baseline.db'
    run(raw, '--threads', '1', '--output', baseline)
    with sqlite3.connect(baseline) as db:
        launches = db.execute('SELECT launch_id,start_ns,end_ns FROM traceloom_graph_launch ORDER BY launch_id').fetchall()
        bodies = db.execute('SELECT COUNT(*) FROM traceloom_graph_body_member').fetchone()[0]
    with sqlite3.connect(raw) as db:
        # First is after launch 0's completion (120) and before launch 1 (200),
        # on the execute stream. Second overlaps the graph on an unrelated stream.
        # Neither has capture-backed body membership. Reuse a known compute identity.
        db.executescript((fixture / 'mutations/inter_launch_eager.sql').read_text())
    output = root / 'with-eager.db'
    timeline = root / 'with-eager.json'
    run(raw, '--threads', '1', '--output', output, '--perfetto-out', timeline)
    with sqlite3.connect(output) as db:
        assert db.execute('SELECT launch_id,start_ns,end_ns FROM traceloom_graph_launch ORDER BY launch_id').fetchall() == launches
        assert db.execute('SELECT COUNT(*) FROM traceloom_graph_body_member').fetchone()[0] == bodies
        events = db.execute('SELECT event_id FROM traceloom_event WHERE (start_ns=125 AND end_ns=135) OR (start_ns=116 AND end_ns=117 AND stream_id=99)').fetchall()
        assert len(events) == 2, events
        for (event,) in events:
            assert db.execute('SELECT COUNT(*) FROM traceloom_anchor WHERE event_id=?', (event,)).fetchone()[0] == 1, event
            role = db.execute('SELECT final_role FROM traceloom_evidence_role_decision WHERE event_id=?', (event,)).fetchall()
            assert len(role) == 1 and role[0][0] in ('anchor', 'unknown_anchor'), role
            assert db.execute('SELECT COUNT(*) FROM traceloom_graph_body_member WHERE event_id=?', (event,)).fetchone()[0] == 0
        assert db.execute("SELECT COUNT(*) FROM traceloom_v_replay_region_annotation_status WHERE support_state<>'supported'").fetchone()[0] == 0
    slices = json.loads(timeline.read_text())['traceEvents']
    projected = [e for e in slices if e.get('cat') == 'traceloom.timeline_event']
    for (event,) in events:
        matching = [e for e in projected if e.get('args', {}).get('event_id') == event]
        assert len(matching) == 1, (event, matching)
        assert not matching[0]['args']['replay_derived'], matching
    exact = [e for e in projected if e.get('args', {}).get('replay_derived')]
    assert len(exact) == bodies, (len(exact), bodies)
    assert len({e['args']['event_id'] for e in projected}) == len(projected)
print('Inter-launch and concurrent non-members retained once; exact bodies unchanged.')
