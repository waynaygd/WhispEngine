"""Measured medians after 30 transition frames; disconnected Tracy comparison."""
import csv
import pathlib
import statistics
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'out/diagnostics/materials')
fields = ['frameMs','gatherMs','prepareMs','batchMs','submitMs','shadowGpuMs','mainGpuMs','frameGpuMs','uploadBytes']
lines = ['| Cubes | Phase | Instancing | Main/shadow draws | CPU frame | Gather | Prepare | Batch | Submit | Shadow GPU | Main GPU | GPU frame | Upload bytes | Resolve | Hits/misses |',
         '|---:|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|']
for count in [100,250,500,1000]:
    for phase in ['before','after']:
        path = root / phase / f'shadow-{count}-1024.csv'
        if not path.exists():
            continue
        rows = list(csv.DictReader(path.open()))
        for block in [4,5]:
            samples = [r for r in rows if 100+block*300+30 <= int(r['frame']) < 100+(block+1)*300]
            assert len(samples) == 270 and all(r['gpuValid'] == '1' for r in samples)
            values = [statistics.median(float(r[f]) for r in samples) for f in fields]
            resolve = f"{statistics.median(float(r['materialResolveMs']) for r in samples):.3f}" if 'materialResolveMs' in samples[0] else 'N/A'
            hit = f"{samples[0]['materialCacheHits']}/{samples[0]['materialCacheMisses']}" if 'materialCacheHits' in samples[0] else 'N/A'
            lines.append(f"| {count} | {phase} | {'ON' if block%2 else 'OFF'} | {samples[0]['drawCalls']}/{samples[0]['shadowDraws']} | " + ' | '.join(f'{v:.3f}' for v in values) + f' | {resolve} | {hit} |')
multi = root / 'multi-material.csv'
if multi.exists():
    rows = list(csv.DictReader(multi.open()))
    for block in [4,5]:
        samples = [r for r in rows if 100+block*300+30 <= int(r['frame']) < 100+(block+1)*300]
        values = [statistics.median(float(r[f]) for r in samples) for f in fields]
        lines.append(f"| 200 two-slot models | after | {'ON' if block%2 else 'OFF'} | {samples[0]['drawCalls']}/{samples[0]['shadowDraws']} | " + ' | '.join(f'{v:.3f}' for v in values) + f" | {statistics.median(float(r['materialResolveMs']) for r in samples):.3f} | {samples[0]['materialCacheHits']}/{samples[0]['materialCacheMisses']} |")
output='\n'.join(lines)+'\n'
(root / 'performance.md').write_text(output,encoding='utf-8')
print(output)
