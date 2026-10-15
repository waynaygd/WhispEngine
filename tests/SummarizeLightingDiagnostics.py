"""Summarize RunLightingDiagnostics CSVs; standard library only."""
import csv
import json
import math
import sys
from pathlib import Path

directory = Path(sys.argv[1] if len(sys.argv) > 1 else "out/diagnostics/lighting")
summary = []
table = [
    "| Cubes | Lighting | Instancing | Draws | Gather p50 | Prepare p50 | Batch p50 | Submit p50 | CPU frame p50 / p95 / p99 |",
    "|---:|:---:|:---:|---:|---:|---:|---:|---:|:---|",
]
for count in (100, 250, 500, 1000):
    with (directory / f"lighting-{count}.csv").open() as file:
        samples = list(csv.DictReader(file))
    for light in (0, 1):
        for instancing in (0, 1):
            rows = [r for r in samples if int(r["lighting"]) == light and int(r["instancing"]) == instancing]
            if len(rows) != 600:
                raise ValueError(f"{count}/{light}/{instancing}: expected 600 samples, got {len(rows)}")
            row = dict(cubes=count, lighting=light, instancing=instancing, samples=len(rows),
                       draws=int(rows[0]["drawCalls"]), instanced=int(rows[0]["instancedDrawCalls"]))
            for metric in ("gatherMs", "prepareMs", "batchMs", "submitMs", "frameMs"):
                values = sorted(float(r[metric]) for r in rows)
                for percentile in (50, 95, 99):
                    row[f"{metric}_p{percentile}"] = values[math.ceil(len(values) * percentile / 100) - 1]
            summary.append(row)
            table.append(
                f"| {count} | {light} | {instancing} | {row['draws']} | "
                f"{row['gatherMs_p50']:.4f} | {row['prepareMs_p50']:.4f} | "
                f"{row['batchMs_p50']:.4f} | {row['submitMs_p50']:.4f} | "
                f"{row['frameMs_p50']:.4f} / {row['frameMs_p95']:.4f} / {row['frameMs_p99']:.4f} |"
            )
(directory / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
(directory / "performance.md").write_text("\n".join(table) + "\n")
print("\n".join(table))
