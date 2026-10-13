"""Summarize measured CPU/GPU data; omit first 30 frames after each switch."""
import csv
import pathlib
import statistics
import sys

root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'out/diagnostics/shadows')
fields=['frameMs','gatherMs','prepareMs','submitMs','shadowCpuMs','shadowGpuMs','mainGpuMs','frameGpuMs']
lines=['| Cubes | Resolution | Mode | Instancing | Main/shadow draws | CPU frame | Gather | Prepare | Submit | Shadow CPU | Shadow GPU | Main GPU | GPU frame |',
       '|---:|---:|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|']
for path in sorted(root.glob('shadow-*-*.csv')):
    count,resolution=path.stem.split('-')[1:]
    rows=list(csv.DictReader(path.open()))
    for block in range(6):
        samples=[r for r in rows if 100+block*300+30<=int(r['frame'])<100+(block+1)*300]
        if not samples:continue
        assert all(r['gpuValid']=='1' for r in samples)
        mode=['OFF','OFF','Hard','Hard','PCF','PCF'][block]
        draws=f"{samples[0]['drawCalls']}/{samples[0]['shadowDraws']}"
        values=[statistics.median(float(r[f]) for r in samples) for f in fields]
        lines.append(f"| {count} | {resolution} | {mode} | {'ON' if block%2 else 'OFF'} | {draws} | "+' | '.join(f'{v:.3f}' for v in values)+' |')
output='\n'.join(lines)+'\n'
(root/'performance.md').write_text(output,encoding='utf-8')
print(output)
