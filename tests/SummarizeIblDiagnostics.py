"""Validate stable DX12 IBL runs and emit nearest-rank p50/p95/p99."""
import csv,math,pathlib,sys
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'out/diagnostics/ibl')
metrics=['frameMs','gatherMs','prepareMs','materialResolveMs','batchMs','submitMs','mainGpuMs','shadowGpuMs','frameGpuMs','shadowPrepareMs','shadowCpuMs']
results=[]
for source in sorted(root.glob('perf-*-i*.csv')):
    count=int(source.stem.split('-')[1]);instance=int(source.stem[-1]);rows=list(csv.DictReader(source.open(encoding='utf-8-sig')))
    assert len(rows)==2400,source
    for mask,name in enumerate(['OFF','Diffuse','Specular','Full']):
        selected=[r for r in rows if int(r['iblMask'])==mask and (int(r['frame'])-int(rows[0]['frame']))%300>=30]
        assert len(selected)==540,(source,mask,len(selected))
        for r in selected:
            assert int(r['instances'])==count+1 and int(r['instancing'])==instance and r['gpuValid']=='1'
            assert int(r['drawCalls'])==int(r['shadowDraws'])==(1 if instance else count+1)
            assert int(r['outputDrawCalls'])==bool(mask) and int(r['environmentGpuBytes'])>0
        out=dict(cubes=count,instancing=instance,mode=name,samples=len(selected))
        for k in ['drawCalls','shadowDraws','outputDrawCalls','uploadBytes','frameConstantUploadBytes','environmentGpuBytes','materialDescriptors','environmentPreprocessMs','environmentUploadMs']:
            out[k]='/'.join(sorted({r[k] for r in selected}))
        for k in metrics:
            values=sorted(float(r[k]) for r in selected)
            for p in (50,95,99):out[f'{k}_p{p}']=f'{values[math.ceil(len(values)*p/100)-1]:.6f}'
        results.append(out)
assert results,'No performance datasets'
with (root/'performance.csv').open('w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,fieldnames=list(results[0]));w.writeheader();w.writerows(results)
keys=['cubes','instancing','mode','frameMs_p50','submitMs_p50','mainGpuMs_p50','mainGpuMs_p95','mainGpuMs_p99','shadowGpuMs_p50','frameGpuMs_p50']
lines=['| '+' | '.join(keys)+' |','| '+' | '.join(['---']*len(keys))+' |']
for r in results:lines.append('| '+' | '.join(str(r[k]) for k in keys)+' |')
(root/'performance.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(f'{len(results)} groups, 540 steady samples each; complete p50/p95/p99: performance.csv')
