from pathlib import Path
import argparse,csv,json
parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);args=parser.parse_args()
metrics=['frameMs','editorUiMs','hierarchyMs','inspectorMs','assetBrowserMs','gatherMs','prepareMs','submitMs','shadowGpuMs','mainGpuMs','outputGpuMs','frameGpuMs']
def percentile(v,q):
    v=sorted(v);x=(len(v)-1)*q;lo=int(x);hi=min(lo+1,len(v)-1);return v[lo]+(v[hi]-v[lo])*(x-lo)
results=[]
for phase in ['before','aa-off','aa-low','aa-high','game']:
    for path in sorted((args.root/phase).glob('*.csv')):
        rows=list(csv.DictReader(path.open(encoding='utf-8-sig')))
        assert len(rows)==1200,(path,len(rows))
        for row in rows:
            assert row['gpuValid']=='1' and row['viewportWidth']=='744' and row['viewportHeight']=='411',path
            if phase!='before':
                assert row['effectiveSamples']=='1',path
                assert int(row['aaMode'])=={'aa-off':0,'aa-low':1,'aa-high':2,'game':2}[phase],path
            if phase=='game':assert row['gameView']=='1' and row['editorGridDraws']=='0',path
        for enabled in [False,True]:
            chosen=[r for r in rows if bool(int(r['instancing']))==enabled and (int(r['frame'])-100)%300>=30]
            assert len(chosen)==540,(path,len(chosen))
            entry={'phase':phase,'case':path.stem,'instancing':enabled,'samples':len(chosen),'draws':sorted({int(r['drawCalls']) for r in chosen}),'objects':sorted({int(r['objects']) for r in chosen}),'hdrAllocationBytes':sorted({int(r.get('aaIntermediateBytes',0)) for r in chosen})}
            if path.stem in ['100','500','1000']:
                count=int(path.stem)+1;assert entry['objects']==[count],entry
                assert entry['draws']==([1] if enabled else [count]),entry
            entry['metrics']={key:{'p50':percentile([float(r[key]) for r in chosen],.5),'p95':percentile([float(r[key]) for r in chosen],.95),'p99':percentile([float(r[key]) for r in chosen],.99)} for key in metrics if key in chosen[0]}
            results.append(entry)
(args.root/'performance-summary.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
lines=['| Phase | Case | Instancing | CPU ms | UI ms | Main GPU ms | Output GPU ms | GPU frame ms | Draws | HDR cache MiB |','|---|---|---|---:|---:|---:|---:|---:|---:|---:|']
for r in results:
    if not r['instancing']:continue
    m=r['metrics'];numbers=[f"{m[k]['p50']:.3f}" if k in m else 'n/a' for k in ['frameMs','editorUiMs','mainGpuMs','outputGpuMs','frameGpuMs']]
    lines.append('| '+r['phase']+' | '+r['case']+' | ON | '+' | '.join(numbers)+' | '+str(r['draws'])+' | '+f"{max(r['hdrAllocationBytes'])/1048576:.2f}"+' |')
(args.root/'performance-tables.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('Validated',len(results),'instancing/quality groups')
