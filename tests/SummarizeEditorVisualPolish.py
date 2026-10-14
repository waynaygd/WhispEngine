"""Summarize reproducible Release editor-helper OFF/ON measurements (milliseconds)."""
from pathlib import Path
import csv,json,statistics,sys
sys.stdout.reconfigure(encoding='utf-8')
root=Path(__file__).resolve().parents[1]/'out/diagnostics/editor-visual-polish'
def quantile(values,p):
    a=sorted(values);x=(len(a)-1)*p;i=int(x);return a[i]+(a[min(i+1,len(a)-1)]-a[i])*(x-i)
result={}
for count in [100,500,1000]:
    result[str(count)]={}
    for mode in ['off','grid','gradient','cubemap']:
        with (root/f'{mode}-{count}.csv').open(encoding='utf-8-sig',newline='') as f: rows=list(csv.DictReader(f))
        assert len(rows)==1200,(count,mode,len(rows))
        assert {(r['viewportWidth'],r['viewportHeight']) for r in rows}=={('744','411')},'viewport must match'
        assert all(r['editorGridDraws']==('0' if mode=='off' else '1') for r in rows),'grid pass count'
        assert all(r['editorBackgroundDraws']==('0' if mode=='off' else '1') for r in rows),'background pass count'
        ready=[r for r in rows if mode!='cubemap' or r['editorSkyReady']=='1']
        assert len(ready)>=900,(count,mode,'insufficient ready samples')
        result[str(count)][mode]={}
        for instance in ['0','1']:
            group=[r for r in ready if r['instancing']==instance];assert len(group)>=300
            assert all(r['objects']==str(count+1) and r['drawCalls']==('1' if instance=='1' else str(count+1)) for r in group),'batching regression'
            fields={}
            for field in ['frameMs','frameGpuMs','mainGpuMs','gatherMs','prepareMs','submitMs','editorUiMs','editorVisualCpuMs','drawCalls','instancedDrawCalls']:
                values=[float(r[field]) for r in group if 'Gpu' not in field or r['gpuValid']=='1'];assert values
                fields[field]={'mean':statistics.mean(values),'p50':quantile(values,.5),'p95':quantile(values,.95),'p99':quantile(values,.99)}
            result[str(count)][mode][instance]={'samples':len(group),**fields}
(root/'performance-summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
lines=['| Cubes | Mode | Instancing | CPU p50/p95/p99 | GPU frame mean | Main GPU mean | Visual submit CPU mean | Scene draws |','|---|---|---|---|---|---|---|---|']
for count,modes in result.items():
    for mode,variants in modes.items():
        for instance,d in variants.items():
            cpu='/'.join(f"{d['frameMs'][p]:.3f}" for p in ['p50','p95','p99'])
            lines.append(f"| {count} | {mode} | {'ON' if instance=='1' else 'OFF'} | {cpu} | {d['frameGpuMs']['mean']:.3f} | {d['mainGpuMs']['mean']:.3f} | {d['editorVisualCpuMs']['mean']:.3f} | {d['drawCalls']['mean']:.0f} |")
(root/'performance-tables.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('\n'.join(lines))
