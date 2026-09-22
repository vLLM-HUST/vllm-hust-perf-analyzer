"""MC2 lifecycle/SQE evidence is not independent computation or communication."""
from pathlib import Path
import json
import sqlite3
import subprocess
import sys
import tempfile

binary = sys.argv[1]
repo = Path(__file__).resolve().parents[3]
fixture = repo / 'native/tests/fixtures/ascend_sqlite/exact_hlt'
with tempfile.TemporaryDirectory(prefix='traceloom-mc2-detail-') as directory:
    root = Path(directory)
    for relative in ('msprof.sql', 'host/sqlite/stream_info.sql'):
        target = root / Path(relative).with_suffix('.db')
        target.parent.mkdir(parents=True, exist_ok=True)
        with sqlite3.connect(target) as db:
            db.executescript((fixture / relative).read_text())
    raw = root / 'msprof.db'
    mc2 = 'MatmulAllReduceMc2AicpuKernel_467_127_1'
    with sqlite3.connect(raw) as db:
        db.executescript('CREATE TABLE IF NOT EXISTS COMMUNICATION_TASK_INFO(globalTaskId INTEGER,name INTEGER,taskType INTEGER);'
                        'CREATE TABLE COMMUNICATION_OP(opName INTEGER,opType INTEGER,startNs INTEGER,endNs INTEGER,deviceId INTEGER,connectionId INTEGER,opId INTEGER);')
        names = [mc2, 'MatmulAllReduce', 'hcom_allReduce_', 'C_CORE_SQE', 'NOTIFY_RECORD_SQE',
                 'NOTIFY_WAIT_SQE', 'WRITE_VALUE_SQE', 'SDMA_SQE', 'FUTURE_SQE']
        ids = {name: 2000+i for i, name in enumerate(names)}
        db.executemany('INSERT INTO STRING_IDS VALUES(?,?)', [(i,n) for n,i in ids.items()])
        for i, kind in enumerate(names[3:]):
            ident = 3000+i
            db.execute('INSERT INTO TASK VALUES(?,?,0,-1,?,1,?,-1,90,?,-1)',
                       (20+i*100, 25+i*100, ident, ids[kind], ident))
            db.execute('INSERT INTO COMMUNICATION_TASK_INFO VALUES(?,?,?)', (ident,ids[mc2],ids[kind]))
        # Two distinct fused invocations, never collapsed to this long envelope.
        for i, start in enumerate((21, 1201)):
            ident = 4000+i
            db.execute('INSERT INTO TASK VALUES(?,?,0,-1,?,1,30,0,91,?,-1)', (start,start+3,ident,ident))
            db.execute('INSERT INTO COMPUTE_TASK_INFO VALUES(?,?,?,30)', (ident,ids['MatmulAllReduce'],ids['MatmulAllReduce']))
        db.execute('INSERT INTO COMMUNICATION_OP VALUES(?,?,20,1400,0,-1,1)', (ids[mc2],ids[mc2]))
        db.execute('INSERT INTO COMMUNICATION_OP VALUES(?,?,30,33,0,-1,2)', (ids['hcom_allReduce_'],ids['hcom_allReduce_']))
    # Same binary, old classification policy: isolate the denoising decision.
    policy = (repo / 'native/data/default_signal_classification_rules.yaml').read_text()
    first = policy.index('  - rule_id: "ascend.mc2.')
    last = policy.index('  - rule_id: "ascend.aux.task.type.model.', first)
    old_policy = root / 'old.yaml'
    old_policy.write_text(policy[:first] + policy[last:])
    def run(stem, extra):
        out = root / (stem+'.db')
        trace = root / (stem+'.json')
        r = subprocess.run([binary,str(raw),'--threads','1','--output',str(out),
                            '--perfetto-out',str(trace),*extra],capture_output=True,text=True,timeout=60)
        assert r.returncode == 0, r.stderr
        return sqlite3.connect(out), json.loads(trace.read_text())['traceEvents']
    old, before = run('old',['--classification-rules',str(old_policy)])
    new, after = run('new',[])
    for table in ('traceloom_event','traceloom_graph_launch','traceloom_graph_body_member','traceloom_replay_cost_member'):
        columns = ','.join(r[1] for r in new.execute('PRAGMA table_info('+table+')') if r[1] != 'anchor_id')
        assert old.execute('SELECT '+columns+' FROM '+table).fetchall() == new.execute('SELECT '+columns+' FROM '+table).fetchall(), table
    rows = new.execute("SELECT event_id,final_role,cost_treatment,support_state FROM traceloom_evidence_role_decision WHERE rule_id LIKE 'ascend.mc2.%'").fetchall()
    assert len(rows) == 6, rows
    omitted = {r[0] for r in rows}
    assert all(r[1:3] == ('auxiliary','retained_as_evidence') for r in rows),rows
    assert all(r[3] not in ('orphan','retained_unplaced') for r in rows),rows
    primary = lambda t: {e['args']['event_id']:(e['ts'],e['dur']) for e in t if e.get('cat')=='traceloom.timeline_event'}
    assert set(primary(before))-set(primary(after)) == omitted.intersection(primary(before))
    assert not omitted.intersection(primary(after))
    # Display-only SQE suppression may already hide some old primary slices.
    # Canonical anchor removal and attribution exclusion must still be proven.
    old_anchors = {r[0] for r in old.execute("SELECT event_id FROM traceloom_anchor")}
    new_anchors = {r[0] for r in new.execute("SELECT event_id FROM traceloom_anchor")}
    assert old_anchors-new_anchors == omitted
    raw_plane = lambda t: [e for e in t if e.get("args",{}).get("projection_plane")=="raw_provider"]
    assert raw_plane(before) == raw_plane(after)
    assert all(primary(before)[k] == v for k,v in primary(after).items())
    assert not omitted.intersection(r[0] for r in new.execute('SELECT event_id FROM traceloom_anchor'))
    assert not omitted.intersection(r[0] for r in new.execute('SELECT aux_event_id FROM traceloom_aux_link'))
    assert new.execute("SELECT COUNT(*) FROM traceloom_anchor WHERE symbol='MatmulAllReduce'").fetchone()[0] == 2
    assert new.execute("SELECT COUNT(*) FROM traceloom_anchor WHERE symbol='AllReduce'").fetchone()[0] == 1
    # Unknown SQE carrying the same parent name stays visible.
    assert new.execute('SELECT COUNT(*) FROM traceloom_anchor WHERE symbol=?',(mc2,)).fetchone()[0] == 1
print('MC2 details excluded from primary structure and attribution; raw/exact evidence retained.')
