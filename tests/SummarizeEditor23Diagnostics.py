from pathlib import Path
import csv,json,statistics,argparse
parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);args=parser.parse_args()
metrics=['frameMs','editorUiMs','hierarchyMs','inspectorMs','assetBrowserMs','shadowGpuMs','mainGpuMs','frameGpuMs','gatherMs','prepareMs','submitMs']
def percentile(values,q):
    v=sorted(values);index=(len(v)-1)*q;lo=int(index);hi=min(lo+1,len(v)-1);return v[lo]+(v[hi]-v[lo])*(index-lo)
results=[]
for phase in ['before','after']:
    for file in sorted((args.root/phase).glob('*.csv')):
        rows=list(csv.DictReader(file.open(encoding='utf-8-sig')));assert len(rows)==1200,(file,len(rows))
        for r in rows:assert r['viewportWidth']=='744' and r['viewportHeight']=='411' and r['gpuValid']=='1',(file,r['frame'])
        for instancing in [False,True]:
            selected=[r for r in rows if bool(int(r['instancing']))==instancing and (int(r['frame'])-100)%300>=30]
            assert len(selected)>=500,(file,len(selected))
            if file.stem=='sky-on':
                selected=[r for r in selected if r['editorSkyReady']=='1'];assert len(selected)>=400,(file,'sky not ready')
            record={'phase':phase,'case':file.stem,'instancing':instancing,'samples':len(selected)}
            record['objects']=sorted(set(int(r['objects']) for r in selected))
            record['drawCalls']=sorted(set(int(r['drawCalls']) for r in selected))
            record['shadowResolution']=sorted(set(int(r['resolution']) for r in selected))
            record['shadowFilterTaps']=sorted(set(int(r.get('shadowFilterTaps',3)) for r in selected))
            record['shadowMapBytesPerFrame']=sorted(set(int(r['resolution'])**2*4 for r in selected))
            if file.stem in ['100','500','1000']:
                count=int(file.stem)+1;assert record['objects']==[count],record
                assert record['drawCalls']==([1] if instancing else [count]),record
            record['metrics']={m:{'p50':percentile([float(r[m]) for r in selected],.5),'p95':percentile([float(r[m]) for r in selected],.95),'p99':percentile([float(r[m]) for r in selected],.99)} for m in metrics}
            results.append(record)
(args.root/'performance-summary.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
lines=['| Case | Phase | Instancing | CPU frame | Editor UI | Hierarchy | Inspector | Assets | Main GPU | GPU frame | Draws |','|---|---|---|---:|---:|---:|---:|---:|---:|---:|---|']
for r in results:
 if not r['instancing']:continue
 m=r['metrics'];lines.append('| '+r['case']+' | '+r['phase']+' | ON | '+' | '.join(f"{m[k]['p50']:.3f}" for k in ['frameMs','editorUiMs','hierarchyMs','inspectorMs','assetBrowserMs','mainGpuMs','frameGpuMs'])+' | '+str(r['drawCalls'])+' |')
(args.root/'performance-tables.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('Validated',len(results),'groups')
