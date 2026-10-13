"""Actual timestamp/wall measurements: nearest-rank p50/p95/p99, 30-frame settling exclusion."""
import csv
import math
import pathlib
import sys

root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'out/diagnostics/shadow-quality')
fields=['shadowCpuMs','shadowGpuMs','mainGpuMs','frameGpuMs','frameMs']
lines=['| Profile | Cubes | Resolution | Coverage | Mode | Instancing | Main/Shadow | Shadow CPU | Shadow GPU | Main GPU | GPU frame | CPU frame |',
       '|---|---:|---:|---:|---|---|---|---|---|---|---|---|']
records=[]
for profile in ('before','after-fixed','after-default'):
    for path in sorted((root/profile).glob('shadow-*.csv')):
        count=int(path.stem.split('-')[1])
        rows=list(csv.DictReader(path.open(encoding='utf-8-sig')))
        assert len(rows)==1800
        for block in range(6):
            samples=[r for r in rows if 100+block*300+30<=int(r['frame'])<100+(block+1)*300]
            assert len(samples)==270 and all(r['gpuValid']=='1' for r in samples)
            mode=('OFF','OFF','Hard','Hard','Soft','Soft')[block]
            r=samples[0];coverage=float(r.get('shadowCoverage',48)) if block>=2 else 0
            values=[]
            record=dict(profile=profile,count=count,mode=mode,instancing=block%2)
            for field in fields:
                sorted_values=sorted(float(s[field]) for s in samples)
                quantiles=[sorted_values[math.ceil(len(sorted_values)*q)-1] for q in (.5,.95,.99)]
                values.append('/'.join(f'{v:.3f}' for v in quantiles))
                for suffix,value in zip(('p50','p95','p99'),quantiles):record[field+'_'+suffix]=value
            records.append(record)
            lines.append(f"| {profile} | {count} | {r['resolution']} | {coverage:.2f} | {mode} | {block%2} | {r['drawCalls']}/{r['shadowDraws']} | "+' | '.join(values)+' |')
(root/'performance.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
with (root/'percentiles.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,records[0].keys());writer.writeheader();writer.writerows(records)
print(f'{len(records)} measured mode rows; each cell p50/p95/p99 ms -> {root}/performance.md')
