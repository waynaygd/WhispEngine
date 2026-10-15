# Editor 2.4 — Level Editing / Game View / Scene Import / Visual Quality

DX12-only, `job_system`, 14–15 октября 2026. Исходный HEAD
`dc6ef17d5f9dbf4ef48aa23f127166db2fe9e2c5`. Перед работой status был чистым.
Commit/push/merge/PR, Vulkan, Lua, sol2, ScriptComponent и Prefab Runtime не выполнялись.

## 1. Аудит и сохранность

Изучены семь требуемых reports, editor/input/history, World/Entity/Registry, v4
serializer, asset/import/loaders, DX12 targets/PSO/output, shadows/Forward+/IBL и
SunSynchronization. `out/diagnostics/editor24/report-audit.json` содержит их hashes.
Сохранены 656 tracked files (`before-source.zip`, `before-sha256.json`), первоначальные
HEAD/status, INI/preferences и non-cache runtime assets. Финальный `preservation-final.json` подтверждает: branch/HEAD прежние, старые tests/reports,
ECS core, Job/Physics/interpolation, loaders/ResourceManager и Vulkan source не изменены.
INI/preferences восстановлены byte-for-byte, runtime `pz3_runtime_snapshot.json` возвращён
к первоначальным bytes; временных editor24 fixture directories не осталось.

Во время работы исходный HospitalModular GLB/cache исчез из source, а появились отдельные
пользовательские HospitalModular_Wall/ceiling/door/doorframe/floor packages. Эти внешние
изменения сохранены, не включены в список реализации и не откатывались. Проверки полного
Hospital выполнялись по исходной runtime copy; source backup сохраняет оригинал. Для
повторения Hospital-specific cases требуется исходный 7-node GLB в runtime assets по
`models/imported/HospitalModular/HospitalModular.glb`. Новые пользовательские parts
не выдаются за автоматически созданный результат import.

Важные факты исходного renderer:

- Vertex 48 B, instance 240 B, обычный/instanced PBR и unlit paths.
- Scene color/depth — single sample; HDR intermediate RGBA32_FLOAT, D32_FLOAT depth,
  output RGBA8_UNORM. HDR intermediates не означают HDR10 display output.
- Forward+ использует консервативные XY tiles, без depth-SRV culling.
- Reinhard/exposure/sRGB output уже существовали. Geometry AA отсутствовало.
- MeshLoader объединял node references, запекая их matrices в вершины. Его Assimp
  flags включают FlipUVs, но не ConvertToLeftHanded. Новый импорт сохраняет именно
  эту native coordinate convention, вместо дополнительного отражения Z.
- В существующем HospitalModular.glb: 7 nodes, 7 meshes, 23 primitives, 9 images,
  без skins/animations/mandatory extensions. Полный source audit сохранён отдельно.

## 2. Десять UX пунктов

| Пункт | Реализация |
|---|---|
| Plane Scale | Три projected quad handles XY/XZ/YZ, local TRS axes, snapping, signed scale, capture до release, одна Undo transaction. |
| Centered Open | WorkCenter + Appearing + pivot .5; размеры ограничены WorkSize. ImGui measurement frame учитывается тестом. |
| Direct Recent | Clean открывается сразу; dirty открывает guard без Scene Browser. Save/Discard/Cancel, error modal при missing/invalid file. |
| Separate/Scene import | Single Mesh / Separate Mesh Assets / Instantiate Imported Scene; независимые `.gltf` keys + `.wscene` manifest. |
| Game View | Один общий dockable viewport, G/checkbox/menu. Сохраняет selection/history, убирает editor helpers; второй camera/target не добавлен. |
| AA | OFF / FXAA Low / FXAA High, до ImGui, HDR/SDR, обычный/instanced, resize и frame lifetime. Настоящий MSAA не реализован. |
| Sun UX | Sun Source, primary actor, Sync Mode, explicit direction, visible disc, effective rays/elevation/azimuth, HDR/IBL warnings. |
| Day/Night | Artist-friendly shader по elevation/azimuth primary, плавные day/twilight/night, horizon warmth/glow и регулируемые цвета. |
| Asset Browser | Project Assets filter, Grid/List, card size, breadcrumbs/search/locate/context, metadata/material/HDR preview и load state. |
| Model drop | Принятый mesh/scene payload создаёт actor/group, world ray placement, selection, dirty, одна Undo transaction. Hover не создаёт entities. |

## 3. Scale и input

Используемая ImGuizmo поддерживает axis/uniform scale, но не scale-plane handles.
Библиотека не форкалась: небольшой editor helper рисует/hit-tests convex quads.
Третий scale component сохраняется; factor snap соответствует ratio snapping.
Negative signs сохраняются, величина ограничена .001..10000. ImGuizmo results также
проходят signed TRS decomposition; shear/singular results не заменяют Transform.
Вращённый world non-uniform scale математически требует shear; scale planes используют
local axes, что явно отражено в tooltip. Move/Rotate/Alt+Move продолжают прежний путь. Применение ImGuizmo decomposition
дополнительно требует фактического mouse drag, чтобы press не менял Transform/Alt-copy;
существующий Editor 2.3 Alt+Move regression проходит.
Active plane имеет приоритет перед mesh picking/camera, release за viewport завершает
операцию. Escape/focus loss/World replacement сбрасывают capture и восстанавливают snapshot.

## 4. Scene import architecture

AssetImport продолжает bounded dependency scan, собственный staging, decoder validation,
no-overwrite и atomic directory publication. Separate modes читают настоящий glTF/GLB
graph выбранной scene, накапливают `parent * local` matrices и проверяют cycles/parents,
глубину/размеры и representable TRS. Shear/singular transforms диагностируются до публикации;
можно выбрать прежний Single Mesh path с baking. Physics/Job/ResourceManager не переписаны.

Один независимый glTF asset создаётся для каждого referenced glTF mesh. Его material
primitives остаются submeshes; это целиком назначаемый MeshRenderer resource. Одинаковый
mesh на разных nodes разделяет один key. Keys зависят от mesh index и package; reimport
в существующий package не перезаписывает предыдущие assets.

GLB BIN сохраняется как общий buffer. Embedded image bufferViews/data URIs извлекаются
в shared PNG/JPEG/DDS files без перекодирования и валидируются existing TextureLoader.
Все parts используют одинаковые texture keys. Material scalar/maps, UV0, authored/generated
tangents, normals, indices и bounds остаются в existing MeshLoader/Material pipeline.
Generated staging caches удаляются до publication, чтобы не сохранить временные paths.

Manifest v1 хранит source, native coordinate convention, node/name/mesh/TRS, world bounds
и placement origin. Source node transforms сохраняются в вычисленном world TRS; parenting
не имитируется. Группа — набор независимых ECS entities и один связанный Undo record.
Restore inverse snapshots remap related fresh generations, включая повторный Undo/Redo.
Все mesh/texture references и transforms preflight до создания actors; ошибка удаляет
только созданные данной операцией actors и не создаёт частичную history entry.

## 5. Drag/drop и scenes

Payload принимается только при delivery в viewport, Edit и без активного tool/modal/camera.
Placement ray использует camera и actual target aspect: closest triangle hit, затем Y=0,
затем positive camera-distance plane. Invalid values отклоняются. Scene-group drop смещает
world bounds placement origin к выбранной точке, сохраняя относительные transforms.
Source pivots отдельных mesh остаются авторскими; pivot-editing не добавлен.

SceneSerializer остаётся v4. AA и новые background fields optional; старые scenes получают
AA OFF/Independent и прежние defaults. G/Card size/Project Assets — editor state/preferences,
не scene Undo. Runtime World clone и Stop восстанавливают authoring settings/actors.

## 6. AA pipeline

MSAA 2x/4x исследован через hardware quality probes для RGBA32_FLOAT/D32_FLOAT.
На тестовом DX12 device доступны quality levels 1/1 для обоих counts. Это hardware support,
а не реализованный MSAA path. Настоящий MSAA потребовал бы color/depth variants, scene/grid
PSO sample variants, resolve и readback/lifetime изменений. TAA требует jitter/history/motion
data и оставлен отдельным будущим этапом. Shadow filtering/texture/specular aliasing не
выдаются за geometry AA.

Выбран FXAA: один existing HDR/output pass, color targets/depth остаются single-sampled.
При SDR AA scene тоже использует existing linear HDR target, exposure=0/tone=OFF, чтобы
не изменять opaque interior colors. Native HDR применяет прежние exposure/Reinhard/sRGB.
FXAA интерполирует display-referred colors **после** tone/encoding. Четыре point loads
с последующим bilinear blend сохраняют этот нелинейный порядок без второго full-size target.
Первая версия, фильтровавшая radiance до encoding, была отклонена coverage regression.

Low/High имеют разные contrast thresholds/span; UI composition происходит после output.
Effective samples=1. Диагностика считает actual HDR target allocation sizes (не driver residency),
включая retained per-frame cache; выключение AA не обещает немедленное освобождение cache.
SDR transparent blending при AA использует существующую HDR linear blending policy и может
отличаться от старого LDR blend. FXAA может смягчать fine texture/specular detail; temporal AA
и mip generation не добавлены. Resize/barriers/descriptor retirement используют existing fence slots.

GPU timestamps расширены с 4 до 6 queries/pass: shadow, main, HDR/output; capacity 16 passes
сохранена. CSV дополнен outputGpuMs/AA/effective samples/allocation bytes/Game View.

## 7. Sky / Sun

Процедурный Day/Night — художественная аппроксимация, не Rayleigh/Mie/volumetrics.
Elevation primary задаёт плавный daylight/twilight blend; azimuth задаёт horizon glow.
Zenith/horizon/ground/night colors, glow/warmth/night intensity и solar disc редактируются
без shader recompilation. Root constants 60 DWORD + SRV table укладываются в DX12 budget.

Drive Light Properties по умолчанию OFF. При явном включении меняет primary color/intensity
по elevation; direction задаётся ECS/explicit sync. Scene Undo содержит соответствующие
directional snapshots. Ambient/IBL не пересчитываются. Static HDRI mismatch показан в UI.
Light Drives Sky перемещает procedural disc, не солнце внутри HDR image. Metadata задаётся
пользователем, brightest-pixel inference отсутствует. Asset содержит authored known sun
20° elevation, 0° azimuth; generator и `.sun.json` документируют это отдельно.

## 8. Asset previews

Scene manifests — дополнительный catalog type; service shaders остаются indexed, но
Project Assets скрывает их из основного content. Mesh bounds/dependencies пересчитываются
только при selected key/version change. Material preview показывает swatch/scalars/maps.
HDR preview — cached 256x128 Reinhard image с отдельным texture handle, обновляется по version,
без per-frame decode/load. Handle retirement и shutdown выполняются renderer API.
Отдельного live 3D mesh-preview camera/target нет: metadata и scene drop являются preview workflow.

## 9. Regression/readback

`EditorLevelWorkflowRegression.cpp` добавляет CPU и DX12 cases; прежние suites сохраняются.
Проверки: plane axis/sign/snapping/TRS/shear, реальные plane drags, centered/movable modal,
Recent dirty/missing/save guard, separate/shared GLB meshes/maps, nested/mirrored transforms,
transaction rollback, repeated group Undo/Redo, world-ray placement, v4/prefs/Play isolation,
G/selection/history, actual edge filtering, 8x8 coverage oracle, thin geometry, HDR/SDR,
ordinary/instanced, known HDR sun/disc alignment, day/dusk/night, Game/grid, resize/lifetime.
Real Hospital проверяет triangle counts/world bounds, 7 entities/23 material ranges/9 shared
images и actual GPU rendering. Intentional invalid-scene log errors не являются D3D12 errors.
Release build PASS. Финальный CTest: **29/29 PASS, 117.53 s** (`ctest-final.log`).
CPU и DX12 EditorLevelWorkflow cases проходят вместе с неизменёнными старыми suites.
DX12 ERROR/CORRUPTION count = **0**, включая shutdown checks.

Coverage oracle SSE: OFF **51.1167**, High **47.0079** (−8.04%); High даёт **1196**
частично покрытых pixels против 0 OFF. Это один независимый диагональный тест,
не универсальная оценка всех видов aliasing. HDR/SDR opaque interiors и ordinary/instanced
readback отличаются не более 2/255; thin geometry остаётся видимой; resize 419×277 проходит.
MSAA probe 2x/4x = 1/1 quality levels color/depth, effective pipeline samples=1.
Known HDR sun и procedural disc совпадают после yaw 37°; проверены day/dusk/night и Game grid=0.
Hospital: 7 actors, 23 material ranges, 5490 indices, 9 shared images, bounds совпадают
со старым baked Single path. Final run import 360.808–463.192 ms, CPU instantiate/preflight
133.543–136.542 ms; это явный синхронный hitch, без попытки скрыть его через CPU timing.
GPU source screenshot — набор авторских modular pieces, не собранный больничный уровень.

## 10. Performance и воспроизведение

Сопоставлены Release baseline исходного HEAD и final build: **744×411**,
100 warmup +1200 frames/case, четыре alternating 300-frame ON/OFF instancing blocks.
Из каждого блока исключены первые 30 frames: по **540 samples** на состояние,
**48 quality/instancing groups** валидированы. AA OFF/Low/High, Game View, browser
1000 entries, primitives и реальный Hospital измерены отдельно.
CSV содержит CPU/render/editor/browser/shadow/main/output/frame timing и memory/batching.
Clocks/power не фиксируются; CPU fluctuations не приписываются pixel shader.
Tracy использует существующие zones, DX12 time — timestamp queries.

```powershell
$cmake='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$python='C:\Users\Тимофей\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
& $python tests/GenerateEditor24Assets.py
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && chcp 65001 > nul && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out/build/shadows-dx12 --parallel 8'
$env:WHISP_DX12_DEBUG='1'
& $ctest --test-dir out/build/shadows-dx12 --output-on-failure
& $ctest --test-dir out/build/shadows-dx12 -R EditorLevelWorkflow -V
& $python tests/PrepareEditor24Baseline.py
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && chcp 65001 > nul && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S out/diagnostics/editor24/baseline-source -B out/diagnostics/editor24/baseline-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out/diagnostics/editor24/baseline-build --parallel 8'
Copy-Item out/diagnostics/editor24/baseline-build/EditorBaseline.exe out/diagnostics/editor24/baseline-runtime/EditorBaseline.exe
& ./tests/RunEditor24Diagnostics.ps1 -BuildDirectory out/diagnostics/editor24/baseline-runtime -Executable EditorBaseline.exe -Phase before -Cases @('100','500','1000','browser-list','browser-grid','primitives')
& ./tests/RunEditor24Diagnostics.ps1 -Phase aa-off -Cases @('100','500','1000','browser-list','browser-grid','primitives') -ExtraArguments @('--aa-quality','0')
& ./tests/RunEditor24Diagnostics.ps1 -Phase aa-high -Cases @('100','500','1000','primitives') -ExtraArguments @('--aa-quality','2')
& ./tests/RunEditor24Diagnostics.ps1 -Phase aa-low -Cases @('100','500','1000') -ExtraArguments @('--aa-quality','1')
& ./tests/RunEditor24Diagnostics.ps1 -Phase game -Cases @('100','500','1000') -ExtraArguments @('--aa-quality','2','--game-view')
& $python tests/SummarizeEditor24Diagnostics.py out/diagnostics/editor24
& ./tests/RunEditor24VisualCaptures.ps1 -BuildDirectory out/diagnostics/editor24/baseline-runtime -Executable EditorBaseline.exe -Phase before -Cases @('editor')
& ./tests/CaptureEditor24Tracy.ps1
& ./tests/RunEditor24VisualCaptures.ps1 -Phase after -Cases @('editor','day','sunset','night','known-sun','game')
Push-Location out/build/shadows-dx12
.\WhispEngine.exe --primitive-demo --daynight-demo 20 --aa-quality 2 --scene-background-panel
.\WhispEngine.exe --primitive-demo --known-sun-demo --aa-quality 2 --scene-background-panel
Pop-Location
```

Первичная configure (в VS x64 environment):

```powershell
& $cmake -S . -B out/build/shadows-dx12 -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_DX12=ON -DENABLE_VULKAN=OFF -DENABLE_TRACY=ON
```

Использованы cached FetchContent dependencies из `out/build/x64-release/_deps` через
`FETCHCONTENT_SOURCE_DIR_*` в существующем build cache. Baseline preparation требует
`before-head.txt`, старого commit и этих уже собранных libraries. `CMAKE_TRY_COMPILE_CONFIGURATION=Release`
обходит локальную ошибку Debug `/Zi` PDB manager C1902, не меняя конфигурацию продукта.

### Измеренные результаты

Все значения ниже — p50 ms, timestamps GPU, CPU frame отдельно. Полные p50/p95/p99,
Gather/Prepare/Submit, UI/Hierarchy/Inspector/Browser и shadow/main/output/frame находятся
в `performance-summary.json`; исходные 1200-row CSV сохранены по phase/case.

| Phase | Case | Instancing | CPU ms | UI ms | Main GPU ms | Output GPU ms | GPU frame ms | Draws | HDR cache MiB |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| before | 100 | ON | 2.320 | 0.114 | 0.112 | n/a | 0.248 | [1] | 0.00 |
| before | 1000 | ON | 6.917 | 0.183 | 0.200 | n/a | 0.502 | [1] | 0.00 |
| before | 500 | ON | 4.712 | 0.152 | 0.150 | n/a | 0.396 | [1] | 0.00 |
| before | browser-grid | ON | 4.882 | 0.192 | 0.207 | n/a | 0.493 | [1] | 0.00 |
| before | browser-list | ON | 4.885 | 0.192 | 0.212 | n/a | 0.502 | [1] | 0.00 |
| before | primitives | ON | 1.669 | 0.098 | 0.104 | n/a | 0.179 | [5] | 0.00 |
| aa-off | 100 | ON | 2.130 | 0.114 | 0.070 | 0.000 | 0.155 | [1] | 0.00 |
| aa-off | 1000 | ON | 6.528 | 0.183 | 0.142 | 0.000 | 0.273 | [1] | 0.00 |
| aa-off | 500 | ON | 4.289 | 0.151 | 0.138 | 0.000 | 0.264 | [1] | 0.00 |
| aa-off | browser-grid | ON | 7.512 | 0.291 | 0.142 | 0.000 | 0.293 | [1] | 0.00 |
| aa-off | browser-list | ON | 4.685 | 0.206 | 0.203 | 0.000 | 0.430 | [1] | 0.00 |
| aa-off | hospital | ON | 2.826 | 0.149 | 0.070 | 0.024 | 0.231 | [23] | 10.25 |
| aa-off | primitives | ON | 1.642 | 0.093 | 0.104 | 0.000 | 0.182 | [5] | 0.00 |
| aa-low | 100 | ON | 4.494 | 0.146 | 0.184 | 0.289 | 0.722 | [1] | 10.25 |
| aa-low | 1000 | ON | 7.449 | 0.184 | 0.356 | 0.346 | 1.007 | [1] | 10.25 |
| aa-low | 500 | ON | 7.435 | 0.193 | 0.206 | 0.271 | 0.764 | [1] | 10.25 |
| aa-high | 100 | ON | 2.901 | 0.121 | 0.158 | 0.302 | 0.607 | [1] | 10.25 |
| aa-high | 1000 | ON | 7.367 | 0.179 | 0.397 | 0.266 | 0.928 | [1] | 10.25 |
| aa-high | 500 | ON | 5.043 | 0.158 | 0.166 | 0.226 | 0.612 | [1] | 10.25 |
| aa-high | hospital | ON | 2.622 | 0.141 | 0.054 | 0.185 | 0.355 | [23] | 10.25 |
| aa-high | primitives | ON | 1.870 | 0.095 | 0.103 | 0.154 | 0.331 | [5] | 10.25 |
| game | 100 | ON | 4.641 | 0.155 | 0.100 | 0.338 | 0.597 | [1] | 10.25 |
| game | 1000 | ON | 12.983 | 0.261 | 0.298 | 0.604 | 1.418 | [1] | 10.25 |
| game | 500 | ON | 7.759 | 0.166 | 0.190 | 0.427 | 0.991 | [1] | 10.25 |


Instancing и CPU render stages (p50 ms; CPU frame дополнительно p95/p99):

| Phase | Case | Inst | Draws | CPU p50/p95/p99 | Gather | Prepare | Submit | GPU frame |
|---|---|---|---:|---:|---:|---:|---:|---:|
| before | 500 | OFF | 501 | 10.631/13.571/16.199 | 1.815 | 0.053 | 3.620 | 0.550 |
| before | 500 | ON | 1 | 4.712/6.868/8.261 | 1.806 | 0.053 | 0.029 | 0.396 |
| aa-off | 500 | OFF | 501 | 10.030/11.633/13.504 | 1.859 | 0.053 | 3.700 | 0.241 |
| aa-off | 500 | ON | 1 | 4.289/6.421/7.573 | 1.836 | 0.052 | 0.029 | 0.264 |
| aa-low | 500 | OFF | 501 | 20.923/26.900/29.664 | 3.685 | 0.080 | 8.776 | 1.031 |
| aa-low | 500 | ON | 1 | 7.435/12.026/15.138 | 2.010 | 0.056 | 0.036 | 0.764 |
| aa-high | 100 | OFF | 101 | 4.070/8.616/11.647 | 0.376 | 0.010 | 0.741 | 0.572 |
| aa-high | 100 | ON | 1 | 2.901/3.723/4.341 | 0.368 | 0.009 | 0.026 | 0.607 |
| aa-high | 1000 | OFF | 1001 | 19.030/21.255/23.116 | 3.676 | 0.118 | 7.169 | 1.363 |
| aa-high | 1000 | ON | 1 | 7.367/9.456/11.224 | 3.639 | 0.118 | 0.031 | 0.928 |
| aa-high | 500 | OFF | 501 | 11.147/13.291/14.455 | 1.864 | 0.052 | 3.633 | 0.842 |
| aa-high | 500 | ON | 1 | 5.043/6.989/8.474 | 1.845 | 0.052 | 0.029 | 0.612 |
| aa-high | hospital | OFF | 23 | 2.778/4.502/5.751 | 0.086 | 0.003 | 0.216 | 0.351 |
| aa-high | hospital | ON | 23 | 2.622/4.059/4.921 | 0.084 | 0.003 | 0.207 | 0.355 |
| game | 500 | OFF | 501 | 22.286/27.868/32.327 | 3.670 | 0.080 | 9.204 | 1.193 |
| game | 500 | ON | 1 | 7.759/12.312/14.670 | 3.373 | 0.080 | 0.046 | 0.991 |

500 cubes: baseline CPU 4.712 → final AA OFF 4.289 ms; High 5.043 ms.
Main GPU OFF 0.138 → High 0.166 ms; High output 0.226 ms, frame GPU 0.264 → 0.612 ms.
Это стоимость дополнительного HDR/output пути, включая conversion, а не чистого FXAA ALU.
На 1000 cubes High output 0.266 ms, frame GPU 0.928 ms. Дополнительный retained HDR cache
**10.25 MiB** при 744×411 (не полная VRAM и не peak retired allocations).
Batching: ON один основной instanced draw для cubes+floor; OFF N+1 ordinary draws.
Hospital остаётся 23 material draws: разные meshes/material ranges не объединяются искусственно.

CPU результаты не изолируют clocks/power/OS. Low/Game runs заметно колеблются, а browser-grid
CPU 4.882 → 7.512 ms и UI 0.192 → 0.291 ms — наблюдаемый рост, который не объявляется
улучшением и не приписывается pixel shader. Отдельного стабильного повторного исследования
этого варианта не проведено. Game View — UX/helper toggle, не обещание ускорения.

Tracy: `aa500.tracy` **5,070,535 B**, 1303 frames, 14.86 s, **1,392,913 zones**.
`RenderGather`, `RenderPrepare`, `RenderSubmit`, `HdrSceneBegin`, `HdrOutput` — по 1300 calls.
CSV export — `tracy-zones.csv`; его CPU zone durations не заменяют GPU timestamps.
Tracy запуск подтверждает instrumentation, не используется как paired benchmark.

### Before/After и визуальные подтверждения

Реальные GPU screenshots 1280×720, identical layout/camera для before/after editor.
Обычная HDR/PBR сцена сохраняет объекты/материалы/свет/тени; после добавлены Game View,
AA controls и Project Assets filter. Day/Night снимки используют Drive Light Properties OFF:
ambient objects не обязаны темнеть вместе с sky. Для автоматического изменения primary
light color/intensity включить явный Drive Light Properties.

[Editor 2.3 before](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor24/before-editor.png)

[Editor 2.4 after](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor24/after-editor.png)

[Day](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor24/after-day.png)

[Sunset](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor24/after-sunset.png)

[Night](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor24/after-night.png)

[Authored HDR sun](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor24/after-known-sun.png)

[Game View](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor24/after-game.png)

[Original Hospital modular source](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor24/hospital-game.png)

[AA OFF diagonal](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor24/aa-off.png)

[FXAA High diagonal](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor24/aa-high.png)

Known-sun HDR — низкоразрешённый orientation/readback fixture (128×64); его квадратный
яркий blob не является high-quality photographic HDRI или качеством процедурного disc.

PNG — lossless conversion исходного GPU PPM, никакой генерации/ретуши. Снимки просмотрены;
мелкая геометрия Hospital видна как отдельные pieces, но это не полноценная live 3D asset preview.

## 11. Будущий parent-child Transform

Минимальный будущий план: отдельный Parent component с logical scene ID, cycle validation,
clone/reference remap hooks, cached topological traversal/dirty propagation, world matrix
cache и согласованная local/world Inspector/Undo policy. Render interpolation должен учитывать
parent/child poses; physics нужен явный world-space integration contract и shear restrictions.
Serializer migration и deletion/reparent policy обязательны. Это не внедрено в flat World.

## 12. Ограничения и ручной checklist

Поддерживается static glTF 2.0 выбранной scene; не поддерживаются skin/animation/morph,
mandatory/compressed primitive extensions, material UV1/KHR_texture_transform, shear world TRS,
полный импорт lights/cameras/advanced material extensions. Primitives внутри одного glTF mesh
остаются material ranges; semantic parts, отсутствующие в source nodes/meshes, не восстанавливаются
автоматически. Import/preflight синхронны по явному действию, resource GPU load — прежний async.

Native CUA input automation не выполнялась; ImGui/GLFW events — программная integration проверка.
Ручной checklist:

- XY/XZ/YZ drag, axis/uniform scale, negative scale/snap/Undo/Escape, Local/World tooltip.
- Open/Recent clean/dirty/missing, Save/Discard/Cancel, modal move/resize/DPI 125/150/200%.
- Import Hospital as Single/Separate/Instantiate; выбрать Floor/Ceiling/doors отдельно.
- Drag mesh/Imported Scene в Scene View; surface/empty-space placement, cancel, Undo/Redo.
- G в viewport и при вводе имени/search/чисел; selection/history остаются прежними.
- AA OFF/Low/High при том же resolution, HDR/SDR и resize; ImGui text остаётся резким.
- Known-sun HDR sync, Visible Disc warning, elevation day→sunset→night, Drive Properties OFF/ON.
- Asset cards/search/locate/previews/ResourceManager state, save scene/restart/Open, Play/Stop.

После завершения этого этапа остановиться; scripting не начинать.

## 13. Изменённые файлы реализации

- `CMakeLists.txt`
- `WhispEngine.cpp`
- `engine/CMakeLists.txt`
- `engine/core/Application.cpp`
- `engine/core/Application.h`
- `engine/core/RenderDiagnosticSample.h`
- `engine/editor/AssetCatalog.cpp`
- `engine/editor/AssetImport.cpp`
- `engine/editor/AssetImport.h`
- `engine/editor/EditorAssets.cpp`
- `engine/editor/EditorImportUi.cpp`
- `engine/editor/EditorLayer.cpp`
- `engine/editor/EditorLayer.h`
- `engine/editor/EditorPreferences.cpp`
- `engine/editor/EditorPreferences.h`
- `engine/editor/EditorTransactions.cpp`
- `engine/editor/EditorUi.cpp`
- `engine/editor/EditorVisualSettings.cpp`
- `engine/editor/LightGizmos.cpp`
- `engine/editor/SunSynchronization.h`
- `engine/render/EditorVisuals.h`
- `engine/render/IRenderAdapter.h`
- `engine/render/Shadows.h`
- `engine/render/backends/dx12/Dx12EditorVisuals.cpp`
- `engine/render/backends/dx12/Dx12Environment.cpp`
- `engine/render/backends/dx12/Dx12RenderAdapter.cpp`
- `engine/render/backends/dx12/Dx12RenderAdapter.h`
- `engine/render/backends/dx12/Dx12Shadows.cpp`
- `engine/scene/SceneSettings.cpp`
- `engine/scene/SceneSettings.h`
- `engine/shaders/dx12/editor_visuals.hlsl`
- `engine/assets/environments/editor24_known_sun.hdr`
- `engine/assets/environments/editor24_known_sun.sun.json`
- `engine/editor/EditorAssetPreview.cpp`
- `engine/editor/EditorLevelWorkflow.cpp`
- `engine/editor/EditorScalePlanes.cpp`
- `engine/editor/GltfSceneImport.cpp`
- `engine/editor/GltfSceneImport.h`
- `engine/editor/TransformMath.h`
- `engine/render/AntiAliasing.h`
- `tests/CaptureEditor24Tracy.ps1`
- `tests/EDITOR_2_4_REPORT.md`
- `tests/EditorLevelWorkflowRegression.cpp`
- `tests/GenerateEditor24Assets.py`
- `tests/PrepareEditor24Baseline.py`
- `tests/RunEditor24Diagnostics.ps1`
- `tests/RunEditor24VisualCaptures.ps1`
- `tests/SummarizeEditor24Diagnostics.py`

Source Hospital removals/new user parts сохранены отдельно и не входят в этот список.
