"""Independent-run summaries. Percentile: nearest rank ceil(p*N)-1; ms.
Aggregate rule fixed before measurement: median of three run-level statistics.
Never pools frames across runs. Captured timings are excluded by directory.
"""
import csv, math, pathlib, statistics, sys
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'out/lab3-evidence')
metrics=['frameMs','cpuRenderMs','materialResolveMs','gatherMs','prepareMs','batchMs','submitMs','gpuFrameMs']
summaries=[]
for path in sorted((root/'raw').glob('*.csv')):
    rows=list(csv.DictReader(path.open()))
    samples=[r for r in rows if r['phase']=='measured']
    assert len(samples)==270 and len(rows)==340, path
    assert all(r['width']=='1920' and r['height']=='1080' and r['objects']=='500' and r['instances']=='500' for r in samples),path
    assert len({r['psoCreations'] for r in samples})==1 and len({r['psoMisses'] for r in samples})==1,path
    assert rows[309]['psoCreations']==samples[-1]['psoCreations'],path
    assert int(rows[-1]['psoCreations'])>int(rows[309]['psoCreations']),path
    s={'run':path.stem,'materials':samples[0]['materials'],'instancing':samples[0]['instancing'],'frames':len(samples),'gpuValidFrames':sum(r['gpuValid']=='1' for r in samples)}
    for metric in metrics:
        v=sorted(float(r[metric]) for r in samples)
        for key,value in [('median',statistics.median(v)),('p95',v[math.ceil(.95*len(v))-1]),('p99',v[math.ceil(.99*len(v))-1]),('min',v[0]),('max',v[-1])]:s[f'{metric}_{key}']=value
    s.update(drawCalls=samples[0]['drawCalls'],initialCreations=max(int(r['psoCreations']) for r in rows[:30]),warmCreations=samples[0]['psoCreations'],finalCreations=samples[-1]['psoCreations'],editCreations=rows[309]['psoCreations'],reloadCreations=rows[-1]['psoCreations'],warmHits=samples[0]['psoHits'],finalHits=samples[-1]['psoHits'],warmMisses=samples[0]['psoMisses'],finalMisses=samples[-1]['psoMisses'])
    summaries.append(s)
def save(name,rows):
    if not rows:return
    with (root/name).open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
save('summary.csv',summaries)
aggregate=[]
for count in ['1','10','100','500']:
 for inst in ['0','1']:
    group=[r for r in summaries if r['materials']==count and r['instancing']==inst]
    if len(group)!=3:continue
    s={'materials':count,'instancing':inst,'runs':3}
    for metric in metrics:
        for stat in ['median','p95','p99']:s[f'{metric}_{stat}']=statistics.median(r[f'{metric}_{stat}'] for r in group)
    aggregate.append(s)
save('comparison.csv',aggregate)
if summaries:
 import matplotlib
 matplotlib.use('Agg')
 import matplotlib.pyplot as plt
 for metric in ['frameMs','cpuRenderMs','materialResolveMs']:
    fig,axes=plt.subplots(2,1,figsize=(12,7),sharex=True)
    for inst,ax in enumerate(axes):
     for count in ['1','10','100','500']:
      p=root/'raw'/f'm{count}-i{inst}-run1.csv'
      if p.exists():
       rows=[r for r in csv.DictReader(p.open()) if r['phase']=='measured']
       ax.plot([int(r['frame']) for r in rows],[float(r[metric]) for r in rows],label=f'{count} materials',linewidth=.7)
     ax.set_ylabel(f'{metric} (ms)');ax.set_title(f'Current system, run 1, instancing {inst}');ax.legend();ax.grid(alpha=.3)
    axes[-1].set_xlabel('Frame index (30 warmup excluded)');fig.tight_layout();fig.savefig(root/f'{metric}.png',dpi=150);plt.close(fig)
print(f'{len(summaries)} independent runs; {len(aggregate)} groups')
