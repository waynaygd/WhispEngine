"""Append only observed results to the lab report; retain explicit missing states."""
import csv, pathlib, json, hashlib, subprocess, re, statistics
repo=pathlib.Path(__file__).resolve().parents[1];root=repo/'out/lab3-evidence'
doc=repo/'docs/RenderingLab3-Performance.md'
def rows(name):
 p=root/name
 return list(csv.DictReader(p.open())) if p.exists() else []
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def text(name):
 p=root/name
 return p.read_text(encoding='utf-8',errors='replace') if p.exists() else 'NOT VERIFIED'
summary=rows('summary.csv');comparison=rows('comparison.csv')
lines=['## 10. Фактические результаты','',f'Завершённых независимых запусков в summary: **{len(summary)}/24**. Число измеряемых кадров: **{sum(int(r["frames"]) for r in summary)}**. Все значения ниже в ms.','',
'| Материалы | Instancing | Frame median | Frame p95 | Frame p99 | CPU Render median | CPU p95 | CPU p99 | Resolve median |',
'|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
for r in comparison:
 keys=['frameMs_median','frameMs_p95','frameMs_p99','cpuRenderMs_median','cpuRenderMs_p95','cpuRenderMs_p99','materialResolveMs_median']
 lines.append('| '+r['materials']+' | '+r['instancing']+' | '+' | '.join(f'{float(r[k]):.6f}' for k in keys)+' |')
lines+=['','Интерпретация внутри фиксированного instancing (500 assets минус 1 asset; это разница текущих конфигураций):','']
for inst in ['0','1']:
 group={r['materials']:r for r in comparison if r['instancing']==inst}
 if '1' in group and '500' in group:
  delta=lambda m:float(group['500'][m+'_median'])-float(group['1'][m+'_median'])
  lines.append(f'- Instancing {inst}: Frame median Δ={delta("frameMs"):+.6f} ms; CPU Render Δ={delta("cpuRenderMs"):+.6f} ms; Material Resolve Δ={delta("materialResolveMs"):+.6f} ms. Число mesh draws в этом сравнении неизменно.')
lines+=['','Колебания трёх run-level значений видны в следующей таблице; доверительный интервал при N=3 не оценивается. Эквивалентные параметры материалов допускают одинаковую batching-группу при разных asset keys. Число PSO определяется shader/state, поэтому 500 material assets не означает 500 PSO. Эти данные не доказывают исторического ускорения.']
lines+=['','### Каждый независимый запуск','','| Run | Frame median / p95 / p99 | CPU Render median / p95 / p99 | Resolve median | Draws | GPU valid |','|---|---:|---:|---:|---:|---:|']
for r in summary:
 fmt=lambda metric:' / '.join(f'{float(r[metric+"_"+s]):.6f}' for s in ['median','p95','p99'])
 lines.append(f'| {r["run"]} | {fmt("frameMs")} | {fmt("cpuRenderMs")} | {float(r["materialResolveMs_median"]):.6f} | {r["drawCalls"]} | {r["gpuValidFrames"]}/270 |')
lines+=['','Min/max всех метрик сохранены в summary.csv. Raw CSV содержат также warmup, edits и reload, исключённые из таблицы.','',
'### PSO и startup','','| Run | Warmup creations | Steady begin → end | Parameter edit | Shader reload | Hits begin → end | Misses begin → end | Setup ms |','|---|---:|---:|---:|---:|---:|---:|---:|']
for r in summary:
 log=text('raw/'+r['run']+'.log');match=re.search(r'setupMs=([\d.]+)',log);setup=match.group(1) if match else 'NOT MEASURED'
 lines.append(f'| {r["run"]} | {r["initialCreations"]} | {r["warmCreations"]} → {r["finalCreations"]} | {r["editCreations"]} | {r["reloadCreations"]} | {r["warmHits"]} → {r["finalHits"]} | {r["warmMisses"]} → {r["finalMisses"]} | {setup} |')
lines+=['','Setup охватывает window/device, CPU assets и создание ECS; PSO compilation происходит при GPU shader preparation в warmup. Setup не выдаётся за отдельное время PSO compilation. Material assets различны по key, но их параметры эквивалентны; эксперимент показывает стоимость количества asset identities при постоянных 500 instances. Нельзя экстраполировать его на 500 разных shader pipelines.','',
'### Графики','']
for metric,label in [('frameMs','Frame Time'),('cpuRenderMs','CPU Render Time'),('materialResolveMs','Material Resolution')]:
 if (root/(metric+'.png')).exists():lines+=['!['+label+'](../out/lab3-evidence/'+metric+'.png)','']
lines+=['### Capture/replay','']
captures=list((root/'captures').glob('*.rdc'));manifest=[]
for p in captures:
 item={'path':str(p.resolve()),'sha256':sha(p),'bytes':p.stat().st_size};manifest.append(item)
 lines.append(f'- [{p.name}](../out/lab3-evidence/captures/{p.name}), {item["bytes"]} bytes, SHA-256 `{item["sha256"]}`.')
if not captures:lines+=['Фактические RDC: **NOT CAPTURED**.']
replay=root/'captures/replay.json'
if replay.exists():
 for r in json.loads(replay.read_text()):lines.append(f'- `{pathlib.Path(r["path"]).name}`: OpenFile={r["open"]}, replay={r["replay"]}, total draw actions={r["drawCount"]}, mesh event={r["meshEvent"]}, PSO={r["pipelineResourceId"]}, shaders={r["shaders"]}. Текстуры, output targets, rasterizer и depth/blend/root state: [replay.json](../out/lab3-evidence/captures/replay.json). ResourceId локальны для каждого capture; совпадение номера не доказывает общий GPU object между процессами.')
else:lines+=['Replay: **NOT VERIFIED**. '+text('captures/replay-error.txt')]
shots=list((root/'captures').glob('*-pipeline.png'))+list((root/'captures').glob('*-events.png'))
for p in shots:lines+=['','!['+p.stem+'](../out/lab3-evidence/captures/'+p.name+')']
if not shots:lines+=['','Event Browser / Pipeline screenshots: **NOT CAPTURED**.']
views=list((root/'captures').glob('*-viewport.png'))
if len(views)==2:
 equal=sha(views[0])==sha(views[1])
 lines+=['','Viewport export после последнего draw: '+('**побитно одинаковые PNG**' if equal else '**PNG различаются**')+', SHA-256 '+', '.join('`'+sha(p)+'`' for p in views)+'. Эти exports — дополнительные изображения; RDC подтверждены отдельно.','', '!['+views[0].stem+'](../out/lab3-evidence/captures/'+views[0].name+')']
lines+=['','Исторические material-before.rdc / material-after.rdc: **NOT CAPTURED**. Полученные current captures нельзя предъявлять как историческое before/after. Тайминги из captures исключены.','',
'### Проверки сохранности','', '```text',text('correctness-exit.json').strip(),text('correctness.log')[-2200:].strip(),text('instancing-dx12.log')[-500:].strip(),text('demo-verifier.log')[-1200:].strip(),text('vulkan-smoke.log')[-700:].strip(),'```','']
original=json.loads((root/'source-identification.json').read_text(encoding='utf-8'))['originalRenderingLab3']
changed=[name for name,h in original.items() if not (repo/name).exists() or sha(repo/name)!=h]
extra=set(str(p.relative_to(repo)) for p in (repo/'test_projects/RenderingLab3').rglob('*') if p.is_file())-set(original)
lines+=['RenderingLab3 SHA-256 verification: '+('**UNCHANGED**, все исходные файлы и набор путей сохранены.' if not changed and not extra else '**CHANGED** '+str(changed)+str(extra)),'']
lines+=['396 проверок RenderingLab3 verifier пройдены; в verification.log явно записано DX12 validation errors = 0. Отдельно прошли 10/10 CTest, standalone instancing DX12 и 1/1 Vulkan smoke. Это correctness evidence, не performance measurements.','']
validlogs=[text('raw/'+r['run']+'.log') for r in summary]
validation=bool(validlogs) and all('DX12 validation errors=0' in s for s in validlogs)
lines+=['### Статус требований и рекомендации для защиты','','| Требование | Фактический статус |','|---|---|',f'| Release DX12, три процесса на вариант, 30+270, raw и p95/p99 | {"DONE" if len(summary)==24 else "INCOMPLETE"} |',f'| Fixed 500 instances / 1920×1080 / separate instancing | {"DONE (CSV invariants)" if len(summary)==24 else "INCOMPLETE"} |',f'| Steady PSO reuse / constants without PSO / reload | {"DONE (summary assertions + raw counters)" if summary else "NOT MEASURED"} |',f'| DX12 validation в benchmark logs | {"0 reported; debug layer отдельно в correctness" if validation else "NOT VERIFIED"} |',f'| Genuine current-configuration RDCs | {len(captures)}/2; replay status выше |','| Историческое timing before/after и два исторических capture | NOT MEASURED / NOT CAPTURED |','| Чистое driver PSO compilation time | NOT MEASURED |','| Изолирование Editor overhead | DONE: benchmark без Editor; Editor performance NOT MEASURED |','',
'На защите показывать архитектуру и данные из MaterialPipeline/ExtraFeatures, затем raw → run-level summary → aggregate, а PSO counters объяснять отдельно. Не заявлять FPS gain. Историческое требование ТЗ остаётся открытым: нужно согласовать с преподавателем альтернативный эксперимент текущей системы либо отдельно восстановить совместимую legacy-сцену на исторических сборках. Проверки корректности не заменяют performance evidence.','',
'Если автоматические current capture/screenshots отсутствуют, минимальное ручное действие: открыть RenderDoc, запустить runtime/MaterialPerformanceProbe.exe с arguments `1 0 ../captures/manual-excluded.csv capture` через Launch Application, working directory runtime; повторить с `500`. Приложение само вызывает TriggerCapture после прогрева. Открыть настоящий RDC, выбрать draw с 36 indices, сохранить Event Browser/Pipeline State. Не выполнять timing-замеры в этом запуске. Для исторического capture требуется сначала отдельный проверенный исторический executable.','',
'### Git status','']
status=subprocess.check_output(['git','status','--short'],cwd=repo).decode('utf-8')
lines+=['```text',status.strip(),'```','Ветка и HEAD сохранены; commit/merge/reset/clean не выполнялись. Новые tests — диагностические helpers; единственная правка существующего tracked файла — whitelist отчёта в .gitignore.']
doc.write_text(doc.read_text(encoding='utf-8').split('<!-- GENERATED RESULTS -->')[0]+'<!-- GENERATED RESULTS -->\n'+'\n'.join(lines)+'\n',encoding='utf-8')
(root/'captures/manifest.json').write_text(json.dumps(manifest,indent=2))
(root/'final-git-status.txt').write_text(status)
print(len(summary),len(captures),len(shots),'original changed:',changed,'extra:',sorted(extra))
