"""Three fresh offline 10k-event imports and projections; no inference claims."""
import json
from pathlib import Path
import statistics
import subprocess
import sys

cli, output = sys.argv[1:]
root=Path(output);root.mkdir(parents=True,exist_ok=False)
source=root/'ten-thousand.ndjson'
with source.open('w') as stream:
    for seq in range(10000):
        r=dict(schema_version=1,event_id=f'e{seq}',trace_id='3'*32,
               span_id=f'{seq//2+1:016x}',producer_id='bench',clock_id='clock',sequence=seq,
               event_type='span_end' if seq%2 else 'span_start',
               monotonic_ns=2**53+seq*1000,wall_time_ns=1788919000000000000+seq*1000)
        r.update({'status':'ok'} if seq%2 else {'name':'step.execute','kind':'step'})
        stream.write(json.dumps(r,separators=(',',':'))+'\n')


def measure(args, name):
    receipt=root/f'{name}.time'
    subprocess.run(['/usr/bin/time','-o',str(receipt),'-f','%e %M',cli,*map(str,args)],
                   check=True,capture_output=True,text=True,timeout=30)
    elapsed,rss=receipt.read_text().split()
    return {'seconds':float(elapsed),'peak_rss_kib':int(rss)}


samples=[]
for i in range(3):
    db,html,perfetto=[root/f'run-{i}{extension}' for extension in ['.db','.html','.json']]
    imported=measure(['import-inference',source,'--output',db],f'import-{i}')
    projected=measure(['export-inference',db,'--html-out',html,'--perfetto-out',perfetto],f'project-{i}')
    samples.append({'import':imported,'projection':projected,'db_bytes':db.stat().st_size,
                    'html_bytes':html.stat().st_size,'perfetto_bytes':perfetto.stat().st_size})
receipt={'events':10000,'spans':5000,'source_bytes':source.stat().st_size,'samples':samples,
         'import_median_seconds':statistics.median(s['import']['seconds'] for s in samples),
         'import_peak_rss_kib':max(s['import']['peak_rss_kib'] for s in samples),
         'projection_median_seconds':statistics.median(s['projection']['seconds'] for s in samples),
         'projection_peak_rss_kib':max(s['projection']['peak_rss_kib'] for s in samples)}
(root/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
assert receipt['import_median_seconds'] <= 3 and receipt['import_peak_rss_kib'] <= 128*1024
assert receipt['projection_median_seconds'] <= 5 and receipt['projection_peak_rss_kib'] <= 128*1024
