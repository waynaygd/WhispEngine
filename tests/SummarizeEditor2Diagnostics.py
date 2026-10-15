"""Summarize actual paired DX12 runs, preserving per-mode samples and percentile definition."""
from pathlib import Path
import csv,json,statistics,sys
sys.stdout.reconfigure(encoding='utf-8')
root=Path(__file__).resolve().parents[1]/'out/diagnostics/editor2'
def read(name):
    with (root/name).open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f))
def percentile(values,p):
    a=sorted(values);x=(len(a)-1)*p;i=int(x);return a[i]+(a[min(i+1,len(a)-1)]-a[i])*(x-i)
def stats(rows,key):
    values=[float(r[key]) for r in rows];return {'mean':statistics.mean(values),'p50':percentile(values,.5),'p95':percentile(values,.95),'p99':percentile(values,.99)}
result={}
for case in ['entities-100','entities-500','entities-1000','lights','materials']:
    result[case]={}
    for prefix in ['before-ui','after']:
        rows=read(prefix+'-'+case+'.csv');assert len(rows)==(1800 if case=='materials' else 1200),(case,len(rows))
        assert {(r['viewportWidth'],r['viewportHeight']) for r in rows}=={('744','411')},(case,prefix,'different viewport')
        assert all(r['gpuValid']=='1' for r in rows),(case,prefix,'invalid GPU timing')
        groups={}
        for r in rows:
            key='instancing='+r['instancing']+(',tiled='+r['tiled'] if case=='lights' else '')+(',shadows='+r['shadows']+',pcf='+r['pcf'] if case=='materials' else '')
            groups.setdefault(key,[]).append(r)
        result[case][prefix]={key:{'samples':len(group),**{metric:stats(group,metric) for metric in ['frameMs','gatherMs','prepareMs','submitMs','editorUiMs','hierarchyMs','inspectorMs','frameGpuMs','mainGpuMs','drawCalls','instancedDrawCalls']}} for key,group in groups.items()}
result['transitions']={}
for prefix in ['before-ui','after']:
    rows=read(prefix+'-transitions.csv');assert len(rows)==75
    result['transitions'][prefix]={n:{key:stats([r for r in rows if r['entities']==n],key) for key in ['serializeMs','cloneMs','stopMs','transactionMs']} for n in ['100','500','1000']}
(root/'performance-summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
lines=['| Сцена / режим | CPU p50/p95/p99 до → после, ms | UI до → после | Hierarchy до → после | Inspector до → после | Gather до → после | GPU frame до → после | Scene draws |','|---|---|---|---|---|---|---|---|']
for case in ['entities-100','entities-500','entities-1000','lights','materials']:
    for key,before in result[case]['before-ui'].items():
        after=result[case]['after'][key]
        pair=lambda m:f"{before[m]['mean']:.3f} → {after[m]['mean']:.3f}"
        cpu=lambda r:'/'.join(f"{r['frameMs'][p]:.3f}" for p in ['p50','p95','p99'])
        lines.append(f"| {case}, {key} | {cpu(before)} → {cpu(after)} | {pair('editorUiMs')} | {pair('hierarchyMs')} | {pair('inspectorMs')} | {pair('gatherMs')} | {pair('frameGpuMs')} | {pair('drawCalls')} |")
lines+=['','| Entities | v3 → v4 serialization CPU | Старый переключатель Play → настоящий clone | Stop до → после | Gesture: 100 старых Undo records → 1 новая transaction |','|---|---|---|---|---|']
for n in ['100','500','1000']:
    b=result['transitions']['before-ui'][n];a=result['transitions']['after'][n];pair=lambda m:f"{b[m]['mean']:.3f} → {a[m]['mean']:.3f}"
    lines.append(f"| {n} | {pair('serializeMs')} | {pair('cloneMs')} | {pair('stopMs')} | {pair('transactionMs')} |")
(root/'performance-tables.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('\n'.join(lines))
