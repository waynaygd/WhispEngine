"""Summarize paired stable-scene DX12 stage-2 measurements (nearest rank)."""
import csv, math, pathlib, sys
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'out/diagnostics/pbr-textures')
names={0:'Core',1:'Normal',6:'MR+AO',7:'All'}
metrics=['frameMs','gatherMs','materialResolveMs','prepareMs','batchMs','submitMs','shadowPrepareMs','shadowCpuMs','mainGpuMs','shadowGpuMs','frameGpuMs']
results=[]
for source in sorted(root.glob('perf-*-i*.csv')):
    count=int(source.stem.split('-')[1]);instancing=int(source.stem[-1])
    rows=list(csv.DictReader(source.open(encoding='utf-8-sig')))
    assert len(rows)==2400,source
    for mask,name in names.items():
        # Exclude 30 frames of each block, including delayed timestamp attribution.
        selected=[r for r in rows if int(r['textureMapMask'])==mask and (int(r['frame'])-100)%300>=30]
        assert len(selected)==540,(source,name,len(selected))
        expected=1 if instancing else count+1
        assert all(int(r['instances'])==count+1 and int(r['drawCalls'])==expected and int(r['shadowDraws'])==expected and int(r['gpuValid'])==1 for r in selected),source
        out={'cubes':count,'instancing':instancing,'mode':name,'samples':len(selected)}
        for key in ['drawCalls','instancedDrawCalls','shadowDraws','shadowInstancedDraws','uploadBytes','frameConstantUploadBytes','textureMemoryBytes','materialDescriptors']:
            values={int(r[key]) for r in selected};out[key]='/'.join(map(str,sorted(values)))
        for key in metrics:
            values=sorted(float(r[key]) for r in selected)
            for percentile in (50,95,99):out[f'{key}_p{percentile}']=f'{values[math.ceil(len(values)*percentile/100)-1]:.6f}'
        results.append(out)
with (root/'performance.csv').open('w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,fieldnames=list(results[0]));w.writeheader();w.writerows(results)
lines=['| Cubes | Instancing | Mode | CPU frame p50 | Gather p50 | Resolve p50 | Submit p50 | Main GPU p50 | Main GPU p95 | Main GPU p99 | Shadow GPU p50 |','|---:|---:|:---|---:|---:|---:|---:|---:|---:|---:|---:|']
for r in results:lines.append('| '+ ' | '.join(str(r[k]) for k in ['cubes','instancing','mode','frameMs_p50','gatherMs_p50','materialResolveMs_p50','submitMs_p50','mainGpuMs_p50','mainGpuMs_p95','mainGpuMs_p99','shadowGpuMs_p50'])+' |')
(root/'performance.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(f'{len(results)} verified groups; 540 stable samples per group; all CPU/GPU p50/p95/p99 in performance.csv')
