from pathlib import Path
import csv,json,sys,statistics
root=Path(sys.argv[1] if len(sys.argv)>1 else 'out/diagnostics/editor25')
def quantile(values,q):
 values=sorted(values);i=(len(values)-1)*q;a=int(i);return values[a]+(values[min(a+1,len(values)-1)]-values[a])*(i-a)
metrics=['frameMs','editorUiMs','hierarchyMs','inspectorMs','assetBrowserMs','lightGizmoMs','gatherMs','prepareMs','submitMs','shadowCpuMs','shadowGpuMs','mainGpuMs','outputGpuMs','frameGpuMs','materialResolveMs']
results=[]
for phase in ['before','after']:
 for path in sorted((root/phase).glob('*.csv')):
  rows=list(csv.DictReader(path.open(encoding='utf-8-sig')));expected=1800 if path.stem=='materials-heavy' else 1200
  assert len(rows)==expected,(path,len(rows));assert all(r['viewportWidth']=='744' and r['viewportHeight']=='411' and r['gpuValid']=='1' for r in rows),path
  groups={}
  for r in rows:
   if (int(r['frame'])-100)%300<30:continue
   key=(int(r['instancing']),int(r['shadows']) if path.stem=='materials-heavy' else -1,int(r['pcf']) if path.stem=='materials-heavy' else -1)
   groups.setdefault(key,[]).append(r)
  for (inst,shadow,pcf),samples in groups.items():
   assert len(samples)==(270 if expected==1800 else 540),(path,inst,len(samples))
   if path.stem in ['100','500','1000']:
    n=int(path.stem)+1;assert all(int(r['objects'])==n and int(r['drawCalls'])==(1 if inst else n) for r in samples),path
   if path.stem=='materials-heavy':
    assert all(int(r['objects'])==200 and int(r['submeshes'])==400 and int(r['drawCalls'])==(4 if inst else 400) and int(r['shadowDraws'])==((4 if inst else 400) if shadow else 0) for r in samples),path
   result={'phase':phase,'case':path.stem,'instancing':bool(inst),'shadows':shadow,'pcf':pcf,'samples':len(samples),'draws':sorted(set(int(r['drawCalls']) for r in samples)),'shadowDraws':sorted(set(int(r['shadowDraws']) for r in samples)),'metrics':{}}
   for field in metrics:
    if field not in samples[0]:continue
    values=[float(r[field]) for r in samples];result['metrics'][field]={f'p{round(q*100)}':quantile(values,q) for q in [.5,.95,.99]}
   results.append(result)
(root/'performance-summary.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
table='| Phase | Case | Inst | Shadow/PCF | CPU | UI | Gather | Submit | Shadow GPU | Main GPU | Output GPU | Frame GPU | Main / Shadow draws |\n|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|\n'
for r in results:
 m=r['metrics'];v=lambda f:f"{m[f]['p50']:.3f}" if f in m else 'n/a'
 table+=f"| {r['phase']} | {r['case']} | {'ON' if r['instancing'] else 'OFF'} | {r['shadows']}/{r['pcf']} | "+' | '.join(v(f) for f in ['frameMs','editorUiMs','gatherMs','submitMs','shadowGpuMs','mainGpuMs','outputGpuMs','frameGpuMs'])+f" | {r['draws']} / {r['shadowDraws']} |\n"
(root/'performance-tables.md').write_text(table,encoding='utf-8');print('Validated',len(results),'groups')
