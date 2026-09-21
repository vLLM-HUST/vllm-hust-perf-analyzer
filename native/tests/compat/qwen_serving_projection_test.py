"""Model rules preserve exact evidence and export real per-launch layer windows."""
import collections
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

binary = sys.argv[1]
repo = Path(__file__).resolve().parents[3]
fixture = repo / 'native/tests/fixtures/ascend_sqlite/exact_hlt'
rules = repo / 'configs/qwen35-serving.yaml'
tail = ['_compute_slot_mapping_kernel', 'Index', 'Cast', 'Index', 'Cast',
        'Sub', 'Slice', 'Add', 'ViewCopy', 'Slice', 'ViewCopy', 'Index', 'Add',
        'Index', 'Fill', 'Fill', 'Fill', 'GatherV3', 'GatherV3', 'GatherV3',
        'Equal', 'MaskedFill', 'ClipByValueV2']
body = ['GemmaRmsNorm', 'CausalConv1d', 'aclnnAdds_AddAiCore_Add',
        'AddRmsNormBias', 'SwiGlu', 'aclnnAdds_AddAiCore_Add', 'AddRmsNormBias', 'Cast']

with tempfile.TemporaryDirectory(prefix='traceloom-qwen-model-') as directory:
    root = Path(directory)
    raw = root / 'msprof.db'
    for relative in ('msprof.sql', 'host/sqlite/stream_info.sql'):
        target = root / Path(relative).with_suffix('.db')
        target.parent.mkdir(parents=True, exist_ok=True)
        with sqlite3.connect(target) as db:
            db.executescript((fixture / relative).read_text())
    with sqlite3.connect(raw) as db:
        db.executescript('UPDATE TASK SET startNs=startNs*100,endNs=endNs*100;'
                         'UPDATE CANN_API SET startNs=startNs*100,endNs=endNs*100;')
        ids = {name: 1000 + i for i, name in enumerate(dict.fromkeys(body + tail))}
        for name, ident in ids.items():
            db.execute('INSERT INTO STRING_IDS VALUES(?,?)', (ident, name))
            db.execute('INSERT INTO COMPUTE_TASK_INFO VALUES(?,?,?,30)', (ident, ident, ident))
        originals = db.execute('SELECT * FROM TASK WHERE taskType=30 AND globalTaskId!=106').fetchall()
        db.execute('DELETE FROM TASK WHERE taskType=30 AND globalTaskId!=106')
        for row in originals:
            for i, name in enumerate(body):
                values = list(row)
                values[0], values[1] = row[0] + i * 10, row[0] + i * 10 + 5
                values[4], values[9] = ids[name], row[9] + i * 10000
                db.execute('INSERT INTO TASK VALUES(?,?,?,?,?,?,?,?,?,?,?)', values)
        for cycle, start in enumerate((35000, 65000, 95000, 125000)):
            for i, name in enumerate(tail):
                ident = 90000 + cycle * 100 + i
                db.execute('INSERT INTO TASK VALUES(?,?,0,?,?,1,30,0,3,?,-1)',
                           (start + i * 10, start + i * 10 + 5, ident, ids[name], ident))

    def analyze(stem, marked):
        output, trace = root / (stem + '.db'), root / (stem + '.json')
        args = ['--rules-config', str(rules)] if marked else []
        result = subprocess.run([binary, str(raw), '--threads', '1', '--output', str(output),
                                 '--perfetto-out', str(trace), *args],
                                capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, result.stderr
        return output, json.loads(trace.read_text())['traceEvents']

    baseline, before = analyze('baseline', False)
    output, after = analyze('marked', True)
    def events(trace):
        return collections.Counter((e['args']['event_id'], e['ts'], e['dur'])
                                   for e in trace if e.get('cat') == 'traceloom.timeline_event')
    assert events(before) == events(after)
    with sqlite3.connect(output) as db, sqlite3.connect(baseline) as old:
        for table in ('traceloom_event', 'traceloom_anchor', 'traceloom_graph_body_member',
                      'traceloom_graph_launch'):
            assert db.execute('SELECT * FROM ' + table).fetchall() == old.execute('SELECT * FROM ' + table).fetchall(), table
        assert db.execute('SELECT macro_discovery FROM traceloom_semantic_tree').fetchone()[0] == 'model_rules_explicit'
        assert db.execute("SELECT SUM(occurrence_count) FROM traceloom_v_tree_node WHERE label='serving_cycle_candidate'").fetchone()[0] == 3
        launches = dict(db.execute('SELECT launch_id,start_ns FROM traceloom_graph_launch'))
        layers = [e for e in after if e.get('cat') == 'traceloom.structural_interval'
                  and e.get('name', '').endswith(' · layer')]
        assert len(layers) == len(launches) > 0
        assert {e['args']['launch_id'] for e in layers} == set(launches)
        assert all(abs(e['dur'] - .065) < 1e-9 for e in layers), layers
        assert not db.execute("SELECT 1 FROM traceloom_replay_body_pattern_domain WHERE support_status!='supported'").fetchall()
    # An observed slot marker with an unrecognized tail is a barrier, not a
    # license to combine two cycles into one apparently complete occurrence.
    with sqlite3.connect(raw) as db:
        db.execute('UPDATE TASK SET globalTaskId=? WHERE connectionId=90101', (ids['GemmaRmsNorm'],))
    partial, _ = analyze('partial', True)
    with sqlite3.connect(partial) as db:
        assert db.execute('SELECT macro_discovery FROM traceloom_semantic_tree').fetchone()[0] == 'model_rules_partial'
        assert db.execute("SELECT SUM(occurrence_count) FROM traceloom_v_tree_node WHERE label='serving_cycle_candidate'").fetchone()[0] == 1
print('Qwen candidate cycles and replay layers preserve exact evidence and geometry.')
