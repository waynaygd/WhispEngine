"""Nearest-rank p50/p95/p99; discard 30 frames after each path switch."""
import csv, math, pathlib, sys
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'out/diagnostics/light-system')
metrics=['frameMs','gatherMs','prepareMs','batchMs','submitMs','lightGatherMs','lightCullCpuMs','lightCullGpuMs','mainGpuMs','frameGpuMs','lightsPerTile','lightMaxPerTile','lightOverflow']
results=[]
for source in sorted(root.glob('perf-*.csv')):
    _,count,layout=source.stem.split('-');rows=list(csv.DictReader(source.open(encoding='utf-8-sig')))
    assert len(rows)==1200,source
    for mode in (0,1):
        selected=[r for r in rows if int(r['tiled'])==mode and (int(r['frame'])-100)%300>=30]
        assert len(selected)==540,(source,mode,len(selected))
        for r in selected:
            assert int(r['lightTotal'])==int(count) and r['drawCalls']=='1' and r['instances']=='101' and r['gpuValid']=='1'
            assert all(math.isfinite(float(r[k])) and float(r[k])>=0 for k in metrics)
        out=dict(lights=int(count),layout=layout,path='Forward+' if mode else 'Forward',samples=len(selected))
        for k in ['drawCalls','instancedDrawCalls','lightUploadBytes','lightGpuBytes','tiles','lightIndices','materialDescriptors','textureMemoryBytes','uploadBytes']:
            out[k]='/'.join(sorted({r[k] for r in selected}))
        for k in metrics:
            values=sorted(float(r[k]) for r in selected)
            for p in (50,95,99):out[f'{k}_p{p}']=f'{values[math.ceil(len(values)*p/100)-1]:.6f}'
        results.append(out)
assert len(results)==20,len(results)
with (root/'performance.csv').open('w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,fieldnames=list(results[0]));w.writeheader();w.writerows(results)
PathBaseline=pathlib.Path(__file__).resolve().with_name('LIGHT_SYSTEM_PERFORMANCE.csv')
PathBaseline.write_bytes((root/'performance.csv').read_bytes())
keys=['lights','layout','path','frameMs_p50','lightGatherMs_p50','lightCullGpuMs_p50','mainGpuMs_p50','mainGpuMs_p95','mainGpuMs_p99','frameGpuMs_p50','lightsPerTile_p50','lightOverflow_p50']
lines=['| '+' | '.join(keys)+' |','| '+' | '.join(['---']*len(keys))+' |']
for r in sorted(results,key=lambda r:(r['layout'],r['lights'],r['path'])):lines.append('| '+' | '.join(str(r[k]) for k in keys)+' |')
(root/'performance.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('20 groups; 540 stable samples per group; p50/p95/p99 in performance.csv')
