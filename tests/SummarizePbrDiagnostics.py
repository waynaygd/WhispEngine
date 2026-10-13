"""python tests/SummarizePbrDiagnostics.py out/diagnostics/pbr"""
import csv
import sys
from pathlib import Path

root=Path(sys.argv[1] if len(sys.argv)>1 else "out/diagnostics/pbr")
metrics=["gatherMs","prepareMs","batchMs","submitMs","frameMs","mainGpuMs","shadowGpuMs","frameGpuMs","uploadBytes","drawCalls","shadowDraws"]
def percentile(values,fraction):
    values=sorted(values);at=(len(values)-1)*fraction;lo=int(at)
    return values[lo]+(values[min(lo+1,len(values)-1)]-values[lo])*(at-lo)
output=[]
for count in [100,250,500,1000]:
    with (root/f"perf-{count}.csv").open() as stream:rows=list(csv.DictReader(stream))
    for mode in [0,1]:
        # Discard mode transitions (and timestamp readback lag), thirty frames
        # from each of two 300-frame blocks. Geometry/physics remain fixed.
        selected=[r for r in rows if int(r["shadingModel"])==mode and (int(r["frame"])-100)%300>=30]
        assert len(selected)==540
        result={"cubes":count,"mode":"PBR" if mode else "Lambert","samples":len(selected)}
        for metric in metrics:
            for label,p in [("p50",.5),("p95",.95),("p99",.99)]:
                result[f"{metric}_{label}"]=percentile([float(r[metric]) for r in selected],p)
        output.append(result)
with (root/"performance.csv").open("w",newline="") as stream:
    writer=csv.DictWriter(stream,fieldnames=output[0].keys());writer.writeheader();writer.writerows(output)
lines=["| Cubes | Model | CPU frame p50/p95/p99, ms | Main GPU p50/p95/p99, ms | Shadow GPU p50, ms | Upload, bytes |",
       "|---:|---|---:|---:|---:|---:|"]
for r in output:
    triple=lambda metric:" / ".join(f'{r[f"{metric}_{p}"]:.4f}' for p in ["p50","p95","p99"])
    lines.append(f'| {r["cubes"]} | {r["mode"]} | {triple("frameMs")} | {triple("mainGpuMs")} | {r["shadowGpuMs_p50"]:.4f} | {r["uploadBytes_p50"]:.0f} |')
(root/"performance.md").write_text("\n".join(lines)+"\n",encoding="utf8")
print("\n".join(lines))
