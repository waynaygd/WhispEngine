# WhispEngine — ECS Light System, Forward+ и Editor Gizmos (DX12)

Дата: 2026-10-13–14. Ветка `job_system`, Release, DX12 ON, Vulkan OFF, Tracy ON.
Commit/push/merge/PR не выполнялись. После этого этапа Editor 2.0, scripting и Point/Spot shadows не начинались.

## 1. Аудит и сохранение исходного состояния

Изучены Lighting, Material, Shadow Mapping, Shadow Quality, PBR Core, PBR Textures, IBL,
Instancing и Interpolation reports, а также соответствующие реализации. Перед изменениями
проверены status и branch. Все 95 исходных изменённых/новых файлов сохранены в
`out/diagnostics/light-system/before-tree.zip`, вместе с patch, status и SHA-256 manifest.
Сравнение с этой резервной копией, а не с HEAD, отделяет текущий этап от предыдущих доработок.
Итоговая проверка сохранности: 77 прежних файлов byte-identical, 18 расширены, удалённых нет (`preservation-audit.json`); прежние отчёты сохранены.
Job System, physics implementation, fixed timestep, RenderInterpolation и Vulkan не изменялись.

Ранее Application владел единственным runtime Directional Light в LightingSettings.
Ambient, IBL и ShadowSettings также принадлежали Application. RenderSystem собирал mesh/material
snapshots, interpolated model/MVP и inverse-transpose normal matrix. MeshVertex имеет 48 bytes,
RenderInstanceData — 240 bytes. OBJ/glTF normals, UV/tangents, submeshes и material slots уже
поддерживались. Основной путь — forward HDR/SDR; shadow map используется только directional light.
ImGui имел Hierarchy, Inspector и ImGuizmo Transform manipulation; viewport light picking отсутствовал.
SceneSerializer v2 сохранял преимущественно render entities; Undo хранил selected-entity snapshots.

## 2. ECS и единый контракт Transform

Добавлены DirectionalLightComponent, PointLightComponent и SpotLightComponent. Общая базовая
структура LightParameters: enabled, linear color, intensity, range, attenuation, inner/outerAngle,
castShadows. Directional добавляет `primary`. Раздельные component types позволяют расширять
систему без DX12 типов в World. Создание lights не добавляет MeshRenderer, Rigidbody или Collider.

Position берётся только из Transform.position. Направление исходящих лучей — local **+Z**, вращаемый
Transform.rotation (`Rz * Ry * Rx`, через тот же quaternion, что и render pose). Для Lambert/BRDF
используется обратный вектор, от поверхности к источнику. Position directional light имеет только
editor/gizmo значение. Scale light entity не меняет направление или range: размер задаётся компонентом.
Scale объекта-получателя по-прежнему учитывается inverse-transpose normal matrix.

SanitizeLight ограничивает конечные значения и NaN/INF: intensity [0,100000], color [0,10000],
range [.01,10000], near-source radius [.01,1000], outer half angle [.1,89], inner [0,outer].
Invalid position исключается из snapshot; invalid quaternion заменяется identity. Ни NaN, ни
отрицательная энергия не должны доходить до lighting shader через ECS gather.

## 3. Миграция legacy Directional Light

LightGather выбирает один активный ECS directional: сначала `primary=true`, затем минимальные
Entity index/generation. При нескольких primary выбор детерминирован. Нулевая intensity не
отменяет primary: это позволяет выключить его вклад без неожиданного появления legacy sun.
Disabled component не участвует. Если активных ECS directional нет, остаётся прежний LightingSettings.

Выбранный ECS primary преобразуется во временный LightingSettings snapshot, используемый **и**
основным shader, **и** directional shadow map. Он исключается из local light buffer, поэтому
double counting отсутствует. Дополнительные directionals освещают без shadows через общий buffer.
CastShadows primary действует совместно с глобальным ShadowSettings.enabled. CastShadows для
Point/Spot хранится и сериализуется, но пока не создаёт shadow resources.

Legacy directional controls в Lighting panel заблокированы при ECS primary; пользователь направляется
в Inspector. Ambient/IBL/Shadow settings остаются общими настройками Application. Global Lighting
OFF возвращает существующее unlit поведение. Это не отдельный второй ECS sun.

## 4. Data flow и Light Gather

`World + Transform/Light components → RenderInterpolation.Resolve → immutable GatheredLights →
IRenderAdapter.SetSceneLights → DX12 per-frame/per-pass upload → compute tile lists → pixel shader`.
Gather выполняется на основном потоке, без структурных ECS изменений; component pointers не
сохраняются. Adapter копирует span до возврата. Entity generation используется для primary/editor,
но не передаётся shader. Измеряются LightGather и LightCullPrepare, отдельные Tracy zones.

GpuSceneLight — align16, **80 bytes**, пять float4:
positionRange, directionType, colorIntensity, coneAttenuation и screenBounds. Последний содержит
консервативный projected UV AABB; coneAttenuation — cos(inner), cos(outer), near radius, reserved enabled.
CPU static_assert и независимые GPU color oracles проверяют контракт. Instance vertex buffer не расширялся.

## 5. Выбор Forward+, а не Clustered

Full Forward оставлен как reference/diagnostic path: каждый shaded pixel проверяет все lights.
Его GPU стоимость примерно O(shadedPixels × lights). CPU gather O(lights) общий обоим путям.

Выбран **tiled Forward+ 16×16** с compute culling. Он сохраняет текущий forward/material/HDR pipeline,
не требует deferred rendering, depth prepass или новых G-buffer attachments. CPU вычисляет только
projected bounds; **GPU** строит per-tile counts и indices. Это не CPU tile-list implementation.
При разнесённых lights pixel cost зависит от локальных кандидатов; compute cost O(tiles × lights).

Clustered добавил бы Z slices и уменьшил false positives при большом depth overlap, ценой более
сложной discretization/storage/lookup. Для первого этапа он не внедрялся. Depth min/max culling также
не используется: нет stale previous-frame depth и дополнительных depth-resource hazards. Это осознанный
консервативный вариант Forward+, а не заявление о полноценном depth-aware clustered renderer.
Архитектурные ориентиры: [AMD TiledLighting11](https://gpuopen.com/learn/tiledlighting11-directx-11-sdk-sample/)
и [Forward+ paper](https://diglib.eg.org/server/api/core/bitstreams/753c34a3-5685-49e0-8485-33901d4f77ce/content).

## 6. Culling, overflow и камера

Point/Spot broad phase использует bounding sphere радиуса range, projected view-space AABB из восьми
углов. Spot sphere намеренно шире cone; точный cone/distance тест выполняется в PS. Near-crossing
spheres покрывают весь XY, behind/far spheres исключаются, bounds расширяются для raster precision.
Каждый Gather использует текущую camera projection/aspect/near/far. Камера и resize покрыты readback.

Один compute group из 64 threads обслуживает tile. Lights тестируются блоками по 64; два groupshared
uint masks и countbits prefix формируют **детерминированный возрастающий** список. Group barriers
защищают mask reset, prefix reads и running count. Dispatch(TilesX,TilesY,1); запись вне viewport отсутствует.
Final capacity — **256 indices/tile**. Global stats: stored indices, true candidate sum/max, overflow tiles.

Overflow не теряет lights: tile получает flag, PS делает полный light loop **вместо** списка; double
counting отсутствует. Dense 500-light case намеренно вызывает этот путь. Максимум 16384 uploaded lights:
превышение диагностируется и выключает весь local pass, без скрытого truncation первых N lights.
Lighting panel показывает ошибку, counts, average/max, overflow и timing.

## 7. Local shader lighting

Point: `L = normalize(lightPosition-worldPosition)`,
`window = saturate(1-(distance/range)^4)`,
`attenuation = window² / max(distance², nearRadius²)`, за range вклад равен нулю.
Параметр attenuation в компоненте — near-source softening radius, отображаемый как **Near radius**.
Intensity — artistic energy multiplier, не калиброванная физическая единица lumen/candela.

Spot дополнительно применяет smoothstep в cosine-space между outer и inner half angles; одинаковые
углы дают hard edge. Point/Spot rays не зависят от non-uniform/negative scale light entity.
Lambert использует base × max(dot(N,L),0). PBR использует прежний DirectBRDF: GGX D, Smith masking,
Schlick Fresnel, metallic diffuse/specular split и roughness. Новый local loop не заменяет его Lambert.

Все contributions складываются в linear HDR до exposure/tone mapping/output transfer. Texture/tint,
normal mapping/TBN, material slots, MR/AO sampling и color space остаются прежними. AO влияет на
indirect lighting; directional visibility умножает только primary direct BRDF. Local и IBL не затеняются
primary shadow map. Unlit shader и normal-data/singular-transform fallback сохранены.

## 8. DX12 bindings и PSO

Добавлены graphics root SRV t9/t10/t11 для lights/counts/indices и 8 DWORD b7 constants.
Root signature занимает 53 DWORD (менее DX12 limit 64). Root SRVs не требуют изменять material
descriptor tables и не вытесняют IBL/shadow/texture bindings. Reflection проверяет b7 name/size=32,
SRV spaces/types/counts/dimensions; ordinary/HDR и instanced PSO поддерживают один контракт.

Compute имеет отдельный root signature: light SRV, три UAV (counts/indices/stats), 8 constants.
Компиляция cs_5_1/PSO lazy; ошибка возвращает Full Forward с постоянной diagnostic error.
Pixel shader не читает tile resources при Full Forward или zero-light path. Compute PSO устанавливается
до graphics PSO, чтобы lazy preparation не подменяла pipeline непосредственно перед draw.

## 9. Resource lifetime

Два DX12 frames in flight имеют независимые light resource pages; каждый main pass внутри frame
получает свою страницу и immutable upload. BeginFrame ждёт fence перед повторным использованием.
Grow происходит только на доступной странице; второй recorded-but-not-submitted pass не перезаписывает
первый. Capacity растёт степенями двух. До 32 main passes/frame, viewport limit 1048576 tiles.

Counts/indices: PIXEL_SHADER_RESOURCE → UAV → PIXEL_SHADER_RESOURCE. Stats: COPY_DEST → UAV →
COPY_SOURCE, затем CopyBufferRegion в readback. Переходы упорядочивают UAV writes перед PS reads;
barriers согласованы с [DX12 resource states](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12).
Readback/timestamps читаются после fence, без ожидания GPU в обычном render loop; диагностический
ReadBackLightTiles явно ждёт GPU. Timestamp lag составляет frame-slot reuse delay. Shutdown освобождает
pages/compute/query resources после GPU wait. Invalid/null window init возвращает false.

`lightGpuBytes` показывает размер buffers активной страницы (включая upload/readback), а не весь DX12
heap budget или driver residency. В устойчивом single-pass run обе frame pages занимают примерно удвоенный
размер. Multi-pass resources, query heap и общий renderer memory следует считать отдельно.
Fault injection реального device removal/OOM не выполнялся; HRESULT allocation failures остаются исключениями.

## 10. Scene serialization и lifecycle

Serializer v3 сохраняет light-only entities: Tag, Transform и light object с type/enabled/color/intensity,
range/attenuation/cone angles/castShadows/primary. Loader принимает v1/v2/v3; неизвестный light type
даёт явную ошибку, numeric data sanitize применяется при чтении. ConfigLoader поддерживает тот же light object.
Spawn branch создаёт только Transform/Tag/Light; default debug mesh/physics не подставляются.

Play/Stop меняют simulation mode существующего World и сохраняют light entities/edits. Clear удаляет
scene lights вместе со stress scene; Restart создаёт новый preset и удаляет старые lights. Load Scene
заменяет World из configs и очищает историю. Ambient/IBL/Shadow runtime settings сохраняют прежнюю
семантику Application и не стали автоматически сериализуемыми scene components.

## 11. Hierarchy, Inspector, Undo/Redo

Hierarchy Create Light предлагает Directional/Point/Spot; selected light можно удалить. Inspector
редактирует Transform, enabled, color, intensity, range, near radius, half angles, Cast Shadows и primary.
Point/Spot Cast Shadows помечен как подготовленный флаг. Изменения применяются в следующем Gather,
без shader rebuild. Новые lights появляются перед текущей камерой; Spot ориентирован вдоль её forward. Color controls используют прежний linear/color conversion helper редактора.

Undo snapshots включают light components и alive state. Create/Delete и edits поддерживают Undo/Redo.
Undo удаления восстанавливает новую Entity generation и переназначает более ранние history records;
не воскресает stale Entity handle и не создаёт дубликат при следующем Undo. Clear/Load сбрасывают history.

## 12. Gizmos и picking

Editor ImDrawList overlay рисует sun/bulb/spot icons, selected halo, directional ray, point range circles
и inner/outer cone. Cone ring соответствует sphere distance cutoff: axial distance range×cos(angle),
radius range×sin(angle). Range/cone handles меняют component с Undo; ImGuizmo Move/Rotate меняет Transform.
Gizmos не создают runtime mesh, physics object, main/shadow draw или light buffer entry.

Projection использует текущую camera и viewport rectangle; outside/behind-camera icons пропускаются.
Picking выбирает ближайший projected icon. Обычный режим скрывает icons за opaque geometry через CPU
ray/AABB/triangle intersections с interpolated model pose; X-ray mode доступен отдельно. Это editor-only
occlusion, не DX12 depth readback. Alpha cutouts/transparency не воспроизводятся в CPU ray test.
Overlay geometry не увеличивает RenderSystem batching counts. В benchmarks gizmos отключены, чтобы
ray picking/overlay не смешивались с оценкой runtime Light System.

## 13. Демонстрации и команды

Рабочая директория executable должна быть build directory — там находятся DXIL, runtime HLSL и assets.
Запуск из repo root не является корректным DX12 запуском. Нормальный editor startup не изменён.

```powershell
Push-Location out/build/shadows-dx12
./WhispEngine.exe --light-demo
./WhispEngine.exe --light-stress 500
./WhispEngine.exe --light-stress 500 --light-overlap
./WhispEngine.exe --light-stress 100 --light-objects 500
./WhispEngine.exe --light-demo --forward-lights
./WhispEngine.exe --light-demo --no-lighting
Pop-Location
```

`--light-stress` без count выбирает 100. Поддержаны 0/1/10/50/100/250/500; scene содержит paused
PBR objects с maps и mixed Point/Spot, legacy sun выключен. Распределённый layout использует range 3,
dense layout — совместный центр/range 45. `--light-demo` включает imported glTF, material/PBR spheres,
IBL, ECS primary directional shadows и выбранный Spot Light с editor gizmo.

## 14–16. Regression, readback и измерения

**Результат: 18/18 CTest PASS, 73.62 s** (`ctest-final.log`). Дополнительно пройдены DX12
LightingRegression, GpuInstancingRegression, MaterialSystemRegression, PbrCoreRegression,
PbrTexturesRegression, IblRegression и ShadowMappingRegression из diagnostic script.
`light-readback.log` и `compute-fallback.log` фиксируют отдельную новую GPU проверку и fallback.
DX12 Debug Layer: 0 ERROR/CORRUPTION в readback assertions. Intentional compiler failure/limit
warnings не являются D3D12 validation errors.

CPU coverage: creation/deletion, generation reuse, Undo/Redo chain после восстановления entity,
primary tie/replacement/legacy fallback, outgoing +Z, negative scale, NaN/INF/angles sanitation,
attenuation/cone formulas, serialization roundtrip, invalid type, projection/picking/behind camera,
Play/Stop/Clear/history invalidation, interpolated moving-light pose без изменения physics Transform.

GPU coverage: point near/far/outside range, RGB ratios/additive lights/movement; spot inner/penumbra/
outer/rotation; independent double-precision Lambert и Cook–Torrance oracles; 0/1/10/50/100/250/500
lights в distributed/dense layouts; full-image float Forward/Forward+/ordinary/instanced comparison;
capacity overflow, camera rotation, resize, 16385-light diagnostic; два main passes с buffer growth,
записанные до submission; imported OBJ/glTF + material slots/maps/IBL; 500-cube ECS primary shadows;
primary visibility reconstruction сохраняет local+IBL contribution; entity removal и Clear.
Основные float tolerances: 2e-5 для Lambert/additive, 1e-4 для BRDF, 2e-4 для large-scene equality;
shadow-mask reconstruction tolerance .004 учитывает UNORM mask quantization.

Legacy GPU suites дополнительно покрывают unlit fallback, missing normal/tangent data, PBR maps,
texture/material reload, non-uniform model transforms и HDR/environment resource lifetime.
Editor coverage: C++ create/delete/Inspector snapshot/Undo/Redo tests, projection hit test, и реальные
editor captures с selected Spot Inspector/ImGuizmo/range/cone overlay. Автоматизация native mouse
click/drag по редактору не выполнялась; не выдаётся за отдельный interactive UI test.

Сохранены 12 editor PPM/PNG: семь stress counts, clean 500 без icons и четыре imported-model modes.
`capture-comparison.json`: viewport crop (190,138)-(930,545) Forward/Forward+ **0 differing pixels**;
unlit — 36370 changed pixels; normal mapping OFF — 16675 changed pixels. UI frame/timing text
исключён из сравнения. `editor-tiled.png` показывает выбранный Spot Light, editable Inspector,
Transform axes и два cone rings; `editor-stress-500-clean.png` — собственно lit geometry.

### Методика performance

RTX 4060 Laptop GPU, Release/Tracy ON, 1280×720 editor. Actual viewport — 1222 tiles; scene содержит
**100 cubes + floor, 101 instances**, PBR normal/MR/AO maps, IBL ON intensity .15, shadows OFF,
physics paused. Render geometry/physics parameters одинаковы между paths. Instancing ON;
1 main draw/1 instanced draw и 101 instances подтверждены **в каждой строке CSV**. Отдельный
readback на 500 cubes + floor подтвердил 1 main и 1 shadow instanced draw, upload reuse.

Для каждого из 10 counts/layout конфигураций: 100 warmup frames, затем 4 блока по 300 frames,
Forward/Forward+/Forward/Forward+. Первые 30 после каждого переключения исключены, поэтому
на path приходится **540 steady samples**, всего 10800 анализируемых строк из 12000 raw.
GPU runs последовательные, без параллельных builds/tests. IBL ready (1114112 GPU bytes) и material
bindings (12 descriptors) стабильны во всех raw строках. GPU timestamps — GPU work, CPU frame —
wall-clock frame с editor/presentation/waits; LightGather, cull CPU, render phases измерены отдельно.

Все timing columns ниже в ms; average/overflow — counts. Полные p50/p95/p99 для CPU frame,
RenderGather/Prepare/Batch/Submit, LightGather, Cull CPU/GPU, Main GPU, GPU Frame находятся в
`tests/LIGHT_SYSTEM_PERFORMANCE.csv` (также `performance.csv` в artifacts); raw — `perf-<count>-<layout>.csv`. Descriptor usage не зависит от lights:
root SRV/UAV не расходуют material heap descriptors. Light upload = count×80 bytes: 800/4000/
8000/20000/40000 B. Allocated per-pass buffers приблизительно 2.12–2.15 MB, два frame slots —
примерно 4.3 MB; это не оценка driver residency.

| lights | layout | path | frameMs_p50 | lightGatherMs_p50 | lightCullGpuMs_p50 | mainGpuMs_p50 | mainGpuMs_p95 | mainGpuMs_p99 | frameGpuMs_p50 | lightsPerTile_p50 | lightOverflow_p50 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 10 | distributed | Forward | 6.912900 | 0.009100 | 0.000000 | 0.112640 | 0.132096 | 0.143360 | 0.377856 | 0.000000 | 0.000000 |
| 10 | distributed | Forward+ | 6.924400 | 0.009200 | 0.015360 | 0.093184 | 0.144384 | 0.191488 | 0.416768 | 0.255319 | 0.000000 |
| 50 | distributed | Forward | 2.118000 | 0.018700 | 0.000000 | 0.113664 | 0.187392 | 0.225280 | 0.144384 | 0.000000 | 0.000000 |
| 50 | distributed | Forward+ | 3.779900 | 0.019400 | 0.006144 | 0.034624 | 0.107520 | 0.115712 | 0.098304 | 1.377250 | 0.000000 |
| 100 | distributed | Forward | 2.644700 | 0.030400 | 0.000000 | 0.195552 | 0.291840 | 0.301056 | 0.233472 | 0.000000 | 0.000000 |
| 100 | distributed | Forward+ | 5.314400 | 0.032300 | 0.009216 | 0.048128 | 0.102400 | 0.105472 | 0.107520 | 2.642390 | 0.000000 |
| 250 | distributed | Forward | 3.452000 | 0.094400 | 0.000000 | 0.517120 | 0.612352 | 0.665600 | 0.591872 | 0.000000 | 0.000000 |
| 250 | distributed | Forward+ | 4.281100 | 0.111600 | 0.016384 | 0.107520 | 0.193536 | 0.200704 | 0.182272 | 5.463993 | 0.000000 |
| 500 | distributed | Forward | 5.477600 | 0.227200 | 0.000000 | 1.049600 | 1.152000 | 1.272832 | 1.146880 | 0.000000 | 0.000000 |
| 500 | distributed | Forward+ | 4.718600 | 0.233000 | 0.027648 | 0.127744 | 0.212992 | 0.220160 | 0.206848 | 10.288871 | 0.000000 |
| 10 | overlap | Forward | 2.346900 | 0.006600 | 0.000000 | 0.056320 | 0.098304 | 0.103424 | 0.102400 | 0.000000 | 0.000000 |
| 10 | overlap | Forward+ | 4.215000 | 0.009400 | 0.008192 | 0.070656 | 0.136192 | 0.155648 | 0.148480 | 10.000000 | 0.000000 |
| 50 | overlap | Forward | 4.665800 | 0.016200 | 0.000000 | 0.171008 | 0.292864 | 0.302080 | 0.256000 | 0.000000 | 0.000000 |
| 50 | overlap | Forward+ | 5.548200 | 0.016700 | 0.008192 | 0.206848 | 0.317440 | 0.327680 | 0.282624 | 50.000000 | 0.000000 |
| 100 | overlap | Forward | 2.944600 | 0.027700 | 0.000000 | 0.345088 | 0.443392 | 0.461824 | 0.387072 | 0.000000 | 0.000000 |
| 100 | overlap | Forward+ | 4.282300 | 0.034800 | 0.010240 | 0.351232 | 0.472064 | 0.492544 | 0.471040 | 100.000000 | 0.000000 |
| 250 | overlap | Forward | 3.941700 | 0.047400 | 0.000000 | 0.709632 | 0.881664 | 0.912384 | 0.754688 | 0.000000 | 0.000000 |
| 250 | overlap | Forward+ | 6.104000 | 0.046600 | 0.016384 | 0.680960 | 0.899072 | 0.933888 | 0.734208 | 250.000000 | 0.000000 |
| 500 | overlap | Forward | 6.875800 | 0.094100 | 0.000000 | 1.487808 | 1.654784 | 1.810432 | 2.079744 | 0.000000 | 0.000000 |
| 500 | overlap | Forward+ | 6.915700 | 0.086200 | 0.026624 | 1.227776 | 1.363968 | 1.420288 | 1.996800 | 500.000000 | 1222.000000 |

### Ожидаемая и измеренная стоимость 100/250/500 lights

Ожидание: CPU gather/upload растут с количеством lights, Full Forward main GPU перебирает N
lights на pixel, Forward+ ограничивает shader локальным tile list и платит дополнительный compute.
Для разнесённых lights среднее/max candidates должны быть значительно меньше N; dense case
может не ускориться. Эти ожидания проверены, а не использованы вместо измерений.

| Lights | Gather CPU F+ p50 | Cull GPU p50 | Main Forward / F+ p50 | GPU frame Forward / F+ p50 | Tile avg / max | Overflow |
|---|---|---|---|---|---|---|
| 100 | 0.0323 | 0.0092 | 0.1956 / 0.0481 | 0.2335 / 0.1075 | 2.64 / 54 | 0 |
| 250 | 0.1116 | 0.0164 | 0.5171 / 0.1075 | 0.5919 / 0.1823 | 5.46 / 108 | 0 |
| 500 | 0.2330 | 0.0276 | 1.0496 / 0.1277 | 1.1469 / 0.2068 | 10.29 / 162 | 0 |

На distributed 500 lights final main GPU 1.0496 → .1277 ms, culling .0276 ms,
GPU frame 1.1469 → .2068 ms; max tile 162, average 10.29, overflow 0. Среднее считается по
всем viewport tiles, а не по weighted shaded pixels. CPU LightGather .2330 ms; это не 500 draws.
Initial serial culler/capacity 64 дал .3697 ms compute и 86 overflow tiles на том же layout
(`performance-initial.csv`); group-parallel bit-prefix culler и capacity 256 устранили это ограничение.
Initial/final captures сняты в разные серии, поэтому ratio между ними не считается строгим paired benchmark.

Dense 500: 1222 overflow tiles, 500 candidates/tile; каждый shaded pixel здесь обязан учитывать
все lights. Main GPU 1.4878 / 1.2278 ms, GPU frame 2.0797 / 1.9968 ms в данной серии.
Это **не алгоритмический выигрыш culling**: список ничего не исключает, branch переходит в
Full Forward. Clock/scheduling variability и p95/p99 не позволяют обещать такое ускорение повторно.
Для 10 lights и dense small-count случаев compute может быть чистой добавочной стоимостью.
CPU wall frame, например на distributed 100, вырос несмотря на снижение GPU time; изменения
wall frame не приписываются pixel shader. Фиксированных CPU/GPU clocks и полной изоляции ОС нет.
Гарантия 60 FPS для любого workload не заявляется.

Tracy: `lights-500.tracy` и `tracy-zones.csv`, 1300 событий каждого LightGather/LightCullPrepare /
RenderGather/RenderSubmit. Final capture 7.95 s, 174117 zones, около 841 KB. Tracy подтверждает
распределение CPU работы; GPU timing берётся из DX12 timestamp queries, а не CPU zones.


## 17–21. Файлы, воспроизведение, ограничения и следующий этап

Новые файлы: `engine/render/SceneLights.h`, `engine/ecs/LightGather.h`,
`engine/ecs/components/LightComponent.h`, `engine/render/backends/dx12/Dx12SceneLights.cpp`,
`engine/shaders/dx12/light_cull.hlsl`, `engine/scene/LightSerialization.h`,
`engine/editor/LightGizmoMath.h`, `engine/editor/LightGizmos.cpp`,
`tests/LightSystemRegression.cpp`, `tests/RunLightSystemDiagnostics.ps1`,
`tests/SummarizeLightSystemDiagnostics.py`, `tests/LIGHT_SYSTEM_PERFORMANCE.csv` и этот отчёт.

Расширены CMakeLists/engine CMake, WhispEngine.cpp, Application.h/cpp, ConfigLoader.h/cpp,
RenderDiagnosticSample.h, RenderSystem.h/cpp, IRenderAdapter.h, DX12 adapter h/cpp,
Dx12Shadows.cpp (prepare light resources before main timing), textured.hlsl, SceneSerializer.cpp,
EditorLayer.h/cpp. Подробный список относительно pre-stage backup — `preservation-audit.json`.

```powershell
# В developer environment VS, UTF-8 console для Ninja showIncludes:
cmake --build out/build/shadows-dx12 --parallel 8
ctest --test-dir out/build/shadows-dx12 --output-on-failure
./tests/RunLightSystemDiagnostics.ps1
python tests/SummarizeLightSystemDiagnostics.py
```

Точные tool paths использованной VS 2022 установки (PowerShell из repo root):

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && chcp 65001 > nul && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B out/build/shadows-dx12 -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_DX12=ON -DENABLE_VULKAN=OFF -DENABLE_TRACY=ON -DWHISP_BUILD_PHYSICS_TESTS=ON && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out/build/shadows-dx12 --parallel 8'
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir out/build/shadows-dx12 --output-on-failure
./tests/RunLightSystemDiagnostics.ps1
& 'C:\Users\Тимофей\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' tests/SummarizeLightSystemDiagnostics.py
./out/diagnostics/tracy-tools/tracy-csvexport.exe out/diagnostics/light-system/lights-500.tracy > out/diagnostics/light-system/tracy-zones.csv
```

Для отдельного измерения: из build directory
`./WhispEngine.exe --light-benchmark 500 <absolute-output.csv>`; dense вариант добавляет
`--light-overlap`. Debug Layer включается `$env:WHISP_DX12_DEBUG='1'` для readback/demos и
отключается на benchmark. Полная серия и validation checks находятся в diagnostic script.

Diagnostic script проверяет DX12 ON/Vulkan OFF, запускает readback с Debug Layer, проверяет shader
fallback посредством порчи **build copy** compute HLSL с обязательным восстановлением bytes,
проводит последовательные benchmarks, captures и Tracy capture. Демо временно задают editor layout;
существующий imgui.ini восстанавливается. Используются ранее созданные Tracy CLI tools в
`out/diagnostics/tracy-tools`. Можно отдельно применять SkipRegression/SkipBenchmarks/SkipDemos/SkipCapture.

Ограничения: один primary directional shadow map; Point/Spot/secondary directional без shadows;
консервативное 2D culling без depth slices; dense overflow возвращается к Full Forward;
не photometric units; global limits диагностируются; editor occlusion opaque/CPU; global runtime
settings не входят в v3 light serialization; allocation/device-removal fault injection не проводился.
Vulkan не изменялся, не собирался и не запускался по DX12-only требованиям этого этапа.

Для согласованного будущего Editor 2.0: component metadata registry для Inspector, явные editor/runtime
World semantics, transactions для Undo, scene-level settings serialization, cached picking acceleration,
единый viewport/tool routing. Для scripting позже нужен API Entity(index,generation), проверка alive,
main-thread queued structural changes и value setters light parameters. Язык/bindings/runtime не выбирались.
Для дальнейшего lighting можно заменить conservative XY broad phase на depth-aware tiles/clusters,
не меняя component/material contracts; Point/Spot Shadow Mapping требует отдельного согласования.
