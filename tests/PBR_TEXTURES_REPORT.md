# PBR Textures + Normal Mapping — DX12, этап 2

Дата проверки: 2026-10-13. Ветка: `job_system`. Конфигурация: Release, `ENABLE_DX12=ON`, `ENABLE_VULKAN=OFF`, `ENABLE_TRACY=ON`.

Реализованы Base Color, Normal, Metallic-Roughness и AO, Material v4, импорт статических glTF MR-материалов, редактор и одинаковый ordinary/instanced shader contract. Emissive хранится только как подготовленная ссылка. IBL, tone mapping и другие следующие этапы не начинались.

## 1. Исходный pipeline и аудит

Изучены PBR Core, Material System, Lighting, Shadow Mapping, Shadow Quality и GPU Instancing reports и соответствующий код. До изменений выполнены `git status` и проверка ветки. 68 исходных изменённых/новых файлов сохранены в `out/diagnostics/pbr-textures/before-tree.zip`; рядом находятся `before.patch`, `before-status.txt`, `before-sha256.json`.

Исходный путь: ECS → MaterialResolver/RenderSystem → interpolated render pose → render packets/instance batches → IRenderAdapter → DX12 PSO/vertex buffers → HLSL. ResourceManager уже обеспечивал async loading, shared caches и material reload. Эти системы расширены, параллельной системы материалов нет.

MeshVertex содержал position/normal/UV0, но не tangent. Assimp импортировал OBJ/glTF, генерировал отсутствующие normals; geometry baking выполнял PreTransformVertices. DX12 использовал position/normal/UV layout, normal inverse-transpose и отдельные обычный/instanced PSO. PBR Core имел ручное декодирование четырёх texels перед single-mip bilinear filtering. Render targets — UNORM, с ручным output sRGB conversion. Root signature занимала 64 DWORD.

TextureLoader поддерживает PNG/JPEG через stb, DDS BC1/BC3 через CPU decode RGBA8, embedded compressed images и aiTexel. Загрузчик текстур не переписан. DDS mip chains и compressed GPU storage в этом этапе не добавлены.

## 2. Root signature: отдельный этап A

Сначала, до текстур, Lighting b1, Shadow b3 и Camera b5 перенесены с root constants на root CBV. Footprint уменьшился **64 → 26 DWORD**. Выполнены существующие CPU/GPU регрессии: `ctest-bindings.log`, `ctest-bindings-material-pbr.log`, `lighting-bindings.log`, `instancing-bindings.log` в каталоге диагностики. Ошибки canonical-path проверки Save в sandbox устранены запуском проверки вне sandbox, без ослабления проверки путей.

Финальный MaterialCB b4 расширен с 4 до 8 constants, итог — **30 DWORD**:

| Root | Binding | DWORD |
|---:|---|---:|
| 0 | b0 object CBV | 2 |
| 1 | t0 и t2–t4 material SRV table | 1 |
| 2 | b1 Lighting CBV | 2 |
| 3 | b2 Normal constants | 12 |
| 4 | b3 Shadow CBV | 2 |
| 5 | t1 shadow SRV table | 1 |
| 6 | b4 Material constants | 8 |
| 7 | b5 Camera CBV | 2 |

Reflection принимает legacy MaterialCB 16 bytes и новый 32 bytes; Lighting 48, Shadow 112, Camera 16 bytes. Instanced variant не требует object/normal CB. Shader compile/PSO failures сохраняют прежний fallback. `textured_unlit.hlsl` не изменён.

[DX12 root signature limits](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-limits) и [alignment constants](https://learn.microsoft.com/en-us/windows/win32/direct3d12/constants) определяют стоимость root entries и 256-byte CB alignment.

## 3. Texture semantics и bindings

Material table содержит четыре contiguous descriptors: Base Color t0, Normal t2, MR t3, AO t4. Shadow t1 и sampler s1 остаются отдельными. Материалы используют linear-wrap s0.

| Слот | SRV | Каналы / fallback |
|---|---|---|
| Base Color | UNORM_SRGB | RGB color, alpha linear; white |
| Normal | UNORM | RGB tangent normal; geometric normal |
| Metallic-Roughness | UNORM | G roughness, B metallic; factors |
| AO | UNORM | R occlusion; 1 |

Images хранятся как R8G8B8A8_TYPELESS. Один resource/cache key может одновременно использоваться через SRGB и linear views. Legacy/UI SRV остаётся UNORM. Shader marker `PBR_TEXTURE_PIPELINE_V1` выбирает новый semantic contract; custom/untagged/unlit shaders сохраняют старый путь. Маска реальной доступности handles запрещает sampling отсутствующего слота.

## 4. Color space

SRGB SRV декодирует Base Color RGB **до** аппаратной интерполяции. Это заменяет ручные четыре Loads, сохраняя linear filtering colors. Normal/MR/AO SRV остаются linear: значения каналов не подвергаются gamma conversion. SRGB view не преобразует alpha.

Base Color factor уже linear; tint проходит существующее декодирование; BRDF работает в linear. Финальный результат кодируется существующей точной sRGB-функцией один раз в UNORM target. Clear/UI contract не изменён. Нет дополнительной gamma conversion или tone mapping. Mipmaps не генерируются: все texture resources имеют один mip, поэтому дальние поверхности могут alias.

## 5. Material schema v4

Канонический legacy `texturePath` остаётся ссылкой Base Color. Добавлены `normalTexturePath`, `metallicRoughnessTexturePath`, `occlusionTexturePath`, `emissiveTexturePath`, `normalScale`, `occlusionStrength`, `textureTexCoords[4]`. Последний массив описывает Base/Normal/MR/AO UV selection.

Normal Scale валидируется как finite [0,16], AO Strength [0,1], UV indices — целые [0,7]. Пути должны быть безопасными относительными resource keys. Absolute/URI/path escape отклоняются. Ссылки overrides проверяются теми же правилами. Explicit zero и empty overrides не превращаются в «отсутствует override».

Save записывает v4 атомарно через sibling `.saving`, flush и MoveFileEx с REPLACE_EXISTING/WRITE_THROUGH. Для Windows sharing/lock violation добавлен ограниченный retry до 62 ms; другие ошибки немедленно возвращаются с Win32 code. Нет fallback с truncation исходного файла.

## 6. Legacy, cache, overrides

Читаются v1/v2/v3/v4; старые assets не перезаписываются автоматически. Сохраняются defaults → imported slot → entity asset → slot asset → overrides → legacy path precedence, shared cache identity, Dirty/Save/Revert, Undo и SceneSerializer roundtrip. ResourceManager загружает дополнительные зависимости через существующие async jobs. Отсутствующая карта отключает только свой слот.

Legacy material default остаётся Lambert с прежними numeric factors. glTF material default соответствует glTF PBR. Тестовый red/green glTF fixture теперь явно задаёт metallic=0, roughness=0.5, отражая прежний смысл цветной неметаллической краски.

## 7. glTF Metallic-Roughness

Импортируются baseColorFactor/texture, metallicFactor, roughnessFactor, MR texture, normal texture/scale, AO texture/strength. Отсутствующие glTF metallic/roughness factors равны 1, normal scale/AO strength равны 1; отдельный regression fixture проверяет omitted fields.

External и embedded images используют существующие resource keys, включая `model#embedded/index`. Shared images не дублируются по роли. Проверены submesh materials, mirrored/nonuniform node transforms и reload.

Assimp в текущей версии выдаёт AO strength через `$tex.file.strength`; поддержан этот фактический key. Аудит локального Assimp показал, что PreTransformVertices преобразует tangents inverse-transpose. Вместо этого только статическое node baking выполнено локально: позиции model matrix, normals inverse-transpose, tangents linear matrix + orthogonalization. Остальной импорт и ResourceManager сохраняются.

Контракт factors, каналов, tangent.w и UV0 соответствует [glTF 2.0 specification](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc).

## 8. UV

Поддержан только UV0. Nonzero texCoord сохраняется для диагностики и отключает соответствующий слот; он не интерпретируется как UV0. Отсутствующий UV0 отключает texture mapping, Base Color использует white. `hasUV0` сохраняется в mesh cache. WMESH cache version 6 инвалидирует прежние layout/cache записи для reimport.

Assimp glTF importer и FlipUVs вместе сохраняют glTF UV convention; authored bitangent/sign не переворачиваются повторно. OBJ сохраняет существующий UV flip.

## 9. Tangents

MeshVertex расширен до **48 bytes**, tangent float4 offset 32; position/normal/UV prefix сохранён. Static assertions проверяют stride/offsets.

Корректные authored glTF tangents сохраняются, tangent.w восстанавливается из Assimp bitangent. Единственный общий `TangentSpace.h` генерирует недостающие tangents и для импортированных, и для программных meshes: UV gradients, angle-weighted accumulation, Gram-Schmidt, handedness из cross(N,T)·B. Mirrored corners с несовместимыми signs разделяются. Degenerate UV/triangles, missing normals и nonfinite inputs дают unavailable tangent, а не NaN.

MikkTSpace рассмотрен: dependency не добавлена. Генератор не заявлен bit-exact MikkTSpace; для normal maps, запечённых с Mikk и без authored tangents, возможны seam differences. Authored glTF tangents предпочтительны.

## 10. TBN и normal mapping

World N = normalize(inverse-transpose(model) × N). World T = model linear × T с последующей orthogonalization к N. Sign учитывает authored w и determinant model; shader вычисляет orientation по нормализованным колонкам для устойчивости при scale. B = cross(N,T) × sign.

Pixel shader повторно ортогонализирует интерполированный T. Texture RGB → [-1,1], normalScale применяется к XY, затем normalize и TBN → world normalize. Invalid basis возвращает geometric N. Lambert/PBR получают одну mapped N. Normal map OFF и normalScale=0 возвращают исходное освещение. Singular/missing normals сохраняют unlit fallback.

## 11. MR

Metallic = factor × B; roughness = factor × G, затем существующие BRDF bounds. R не используется как metallic/roughness. Без MR texture используются factors. Per-instance factors остаются индивидуальными и не разделяют batch.

## 12. AO

AO = lerp(1, texture.R, occlusionStrength). Умножается только ambient approximation, один раз. Direct diffuse/specular и directional shadow visibility AO не затемняет. Отсутствующая AO карта даёт 1.

## 13. Material Editor и demo

Добавлены четыре texture controls с указанием sRGB/linear и packing, Normal Scale, AO Strength, UV selection diagnostics, loading/missing indicators. Сохранены assets/slots, индивидуальные overrides, Save/Revert, Dirty guard, Undo, browser и runtime reload.

Lighting/material controls включают Normal Mapping, MR Maps, AO Maps и Lambert/PBR comparison. Runtime map mask сохраняется при Play/Stop, Clear/Restart; создание новой demo намеренно восстанавливает maps ON. Поведение использует существующую семантику engine runtime settings. Material/scene roundtrip и lifecycle проверены автоматически; полного ручного click-through всех ImGui действий не проводилось.

`--pbr-textures-demo` содержит MR sphere grid, patterned Base/Normal/ORM, floor, external и embedded glTF spheres, включая negative/nonuniform transforms. Контрольные textures/models созданы для проекта, сторонние licensed assets не заимствованы. Реальные editor captures находятся в `out/diagnostics/pbr-textures/editor-*.png`, сравнение — `editor-comparison.png`. Captures просмотрены визуально.

## 14. GPU Instancing

RenderInstanceData расширен **224 → 240 bytes**, old prefix сохранён. Texture parameters offset 224: normalScale, AO strength, enabled-map bits, orientation diagnostic. Model/MVP/tint/normal rows/surface/base factor сохраняются индивидуально. Данные собираются из существующей interpolated pose; simulation/history не менялись.

Main batch key включает Base/Normal/MR/AO bindings. Разные map bindings разделяют main batch; различающиеся factors/scales/tint остаются в одном. Shadow key зависит от geometry/state, поэтому texture-only split не разделяет shadow batch.

500 cubes + floor: **1 main draw + 1 shadow draw**, instance upload **120240 bytes**; обычный путь — 501 draws в каждом pass. Данные экземпляров повторно используются в shadow pass, нового buffer на материал нет. Pixel equivalence ordinary/instanced проверена bitwise.

## 15. Shadows

Hard/Soft PCF, receiver-plane correction, projection fitting, Cast/Receive, strength и negative-scale culling сохранены. Bias/receiver correction используют geometric normal, а BRDF — mapped normal. Visibility один раз умножает полный direct BRDF; ambient × AO находится вне visibility.

`shadow_depth.hlsl` byte-identical исходному. Изменения Dx12Shadows касаются только CBV binding/dirty flags, не алгоритма теней. Старые ShadowMapping/ShadowQuality GPU regressions проходят.

## 16. Настоящий GPU readback

Новый PbrTexturesRegression использует DX12 Debug Layer и readback 320×240, независимый double-precision BRDF/TBN oracle и 15 samples/case, tolerance 2/255. Это не shader compilation-only check.

- Проверены masks 0/1/2/4/7, Base SRGB vs linear data, scale 0/0.5/1/2, ambient/direct, Lambert/PBR, rotation/nonuniform/negative scale.
- Ordinary/instanced images bitwise identical. Degenerate UV возвращает geometric N; missing N возвращает unlit.
- Normal map сдвигает highlight centroid X **159.5 → 80**; отдельный intermediate normal даёт X=140. Проверяется положение блика, не только mean color.
- MR G=51/204 при B=255 и независимом R: peak R **255 → 21**, half-height area **4884 → 36800**. Проверяется ширина и metallic response.
- AO direct-only images совпадают, ambient-only меняется по R/strength.
- PNG, JPEG, DDS BC1 и BC3 проходят loader → GPU → oracle; embedded glTF PNG также проверен.
- OBJ/material slots покрыты существующими Lighting/Material GPU suites; новые external/embedded/unsupported-UV glTF — новой suite.
- Shadowed receiver сверяется с ambientAO + direct×visibility, tolerance 3/255, более 20 shadow samples.
- 180 recorded ordinary draws с чередующимися lighting snapshots пересекают три upload pages; ресурсы уничтожаются/заменяются до submission, ожидаемые left/right colors сохраняются.
- Reload, missing slots, delete/Clear, resize, PSO failure, retirement и shutdown покрыты новой и сохранёнными suites.

В завершённых GPU regressions **0 DX12 ERROR/CORRUPTION**. Readbacks: `out/build/shadows-dx12/pbr-textures-readback/*.ppm`; удобные PNG-копии также в каталоге диагностики.

## 17. Regression results

Финальная production версия: **14/14 CTest PASS, 34.98 s**, `ctest-final-cache.log`. После добавления дополнительных тестовых assertions для omitted glTF defaults, degenerate UV и missing normals: **2/2 PbrTextures CTest PASS, 3.13 s**, `ctest-textures-final.log`. Последние изменения после полного CTest — тесты и static assertions, без изменения runtime поведения.

Дополнительно после финального binding cache выполнены `LightingRegression --dx12` и `GpuInstancingRegression --dx12`, оба PASS: `lighting-final-cache.log`, `instancing-final-cache.log`.

CPU suite проверяет Material 1–4/atomic save/cache/overrides/scene serialization, tangents/mirrored corners, glTF node transforms, batch split и interpolated pose. Сохранённые Physics, Resources, RenderInterpolation, Materials, Lighting, Instancing, PBR Core и Shadows suites не удалены. Play/Stop покрывается существующим lifecycle/interpolation поведением и tests; отдельного полного ручного прогона Play/Stop именно новой demo не заявляем.

## 18. Производительность и Tracy

Восемь DX12 runs: 100/250/500/1000 cubes, instancing OFF/ON. Physics paused, одинаковые transforms/light/shadows и materials. Debug Layer OFF в performance runs. 100 warmup frames, затем два цикла четырёх режимов по 300 frames. После исключения первых 30 frames каждого блока — **540 samples/group, 32 groups, 17280 steady samples** из 19200 raw.

Core = stage-2 executable с map mask 0 (factors only), Normal=1, MR+AO=6, All=7. Это сравнение sampling modes в одной реализации, **не отдельный старый stage-1 executable**. Во всех режимах одинаковые resident textures, table/layout/240-byte stride и vertex tangent preparation.

Main GPU p50, ms, instancing ON:

| Cubes | Core | Normal | MR+AO | All |
|---:|---:|---:|---:|---:|
| 100 | 0.013312 | 0.030720 | 0.033792 | 0.058368 |
| 250 | 0.020480 | 0.056320 | 0.054272 | 0.075776 |
| 500 | 0.039936 | 0.055296 | 0.067584 | 0.112640 |
| 1000 | 0.064512 | 0.121856 | 0.133120 | 0.192512 |

All maps, p50 ms:

| Cubes | Submit OFF | Submit ON | Main GPU OFF | Main GPU ON | CPU frame OFF | CPU frame ON |
|---:|---:|---:|---:|---:|---:|---:|
| 100 | 0.0420 | 0.0158 | 0.057344 | 0.058368 | 3.8310 | 3.7534 |
| 250 | 0.0730 | 0.0164 | 0.076800 | 0.075776 | 4.6450 | 4.7726 |
| 500 | 0.1246 | 0.0172 | 0.142336 | 0.112640 | 6.6835 | 6.4622 |
| 1000 | 0.2308 | 0.0172 | 0.136192 | 0.192512 | 9.4061 | 8.5733 |

Полные Gather/MaterialResolve/Prepare/BatchBuild/Submit/Main GPU/Shadow GPU/GPU Frame/CPU Frame **p50/p95/p99**: `out/diagnostics/pbr-textures/performance.csv`; краткая таблица — `performance.md`; исходные восемь `perf-*-i*.csv` сохраняются.

Для 500 ON All main GPU p95=0.132096, p99=0.141312 ms. Shadow p50 меняется Core 0.057344 → All 0.146432 ms при неизменённом depth pipeline. Clocks/background не зафиксированы; на 1000 main GPU OFF быстрее ON. Поэтому эти runs подтверждают batching и измеренную CPU submission cost, но не доказывают универсальный FPS gain или чистую причинную shader delta. CPU frame variation не приписывается pixel shader.

Frame constants upload: 768 bytes для homogeneous scene. ON instance uploads: 24240/60240/120240/240240 bytes; ordinary object uploads: 25856/64256/128256/256256 bytes. Live texture allocation: **196608 bytes**, один shared white/normal/ORM набор; активная material table — **4 descriptors**, во всех режимах. Это текущая allocation texture resources и active-frame table usage, не peak VRAM и не общий heap размер.

Первый dataset до binding caches сохранён в `initial-performance/`. После исключения повторных snapshot comparisons/table lookups и canonical-path normalization ordinary All Submit p50 500 cubes уменьшился 0.2699 → 0.1246 ms. Это отдельные runs, не строгий paired benchmark.

Tracy capture `textures-500.tracy`: **2503 frames / 10.9 s / 1331918 zones / 4604.76 KiB**. Он записан отдельным run с подключённым profiler, не смешан с восемью сравнительными runs. CPU zone means: Gather 2.011471, Prepare 0.050867, BatchBuild 0.046173, Submit 0.015866, ShadowPass 0.037066 ms, по 2500 events. MaterialResolve 0.003347 ms — **per-slot event**, 1251999 events, не per-frame aggregate. Hardware GPU timestamps берутся из CSV, не из CPU Tracy zones. Экспорты `tracy-*.csv` сохранены.

## 19. Lifetime и limits

Два fenced frame slots владеют append-only persistently mapped 64 KiB upload pages. Snapshot Lighting/Shadow/Camera занимает 768 bytes (offsets 0/256/512); новые значения не перезаписывают ранее записанные command lists. Pages переиспользуются только после fence соответствующего slot. Identical current snapshots переиспользуются, dirty tracking снимает redundant copies.

SRV heap: 128 persistent descriptors + два frame arenas по 8192 tables × 4 = **65664 descriptors reserved**. Persistent resource limit 128 сохранён. Frame tables immutable до fence, cached по четырём handles и semantic shader contract. Texture destruction/replacement инвалидирует CPU binding cache и удерживает старый resource/descriptors для pending GPU work. При исчерпании capacity draw безопасно отклоняется, GPU не получает uninitialized descriptor. Нет resource/buffer allocation на каждый material.

Live texture bytes вычисляются по GetResourceAllocationInfo, не по размеру PNG. Resource caches могут удерживать loaded assets до shutdown; retired resources/CB pages/vertex buffers/shadow map/heap reservation не входят в эту texture telemetry. Существующий ordinary object pool limit 2048 сохранён.

## 20. Изменённые файлы

Относительно сохранённого состояния предыдущих этапов изменены:

- `CMakeLists.txt`, `WhispEngine.cpp` — fixtures/tests/CLI.
- `engine/core/Application.{h,cpp}`, `RenderDiagnosticSample.h` — demo/benchmark/telemetry.
- `engine/ecs/systems/RenderSystem.{h,cpp}` — slot resolution, payload/batching/global map masks.
- `engine/editor/EditorLayer.cpp` — Material Editor controls.
- `engine/render/IRenderAdapter.h`, `RenderInstanceData.h`, `backends/dx12/Dx12RenderAdapter.{h,cpp}`, `Dx12Shadows.cpp` — binding contract/lifetime/telemetry.
- `engine/resources/MaterialResource.h`, `MaterialSerialization.h`, `MeshData.h`, `ResourceManager.h`, `loaders/MaterialLoader.cpp`, `loaders/MeshLoader.cpp` — v4/import/dependencies/cache.
- `engine/shaders/dx12/textured.hlsl` — semantic sampling/TBN/MR/AO.
- `test_projects/legacy_workspace/Assets/models/material_slots.gltf` — explicit paint factors.
- `tests/GpuInstancingRegression.cpp`, `MaterialSystemRegression.cpp`, `PbrCoreRegression.cpp` — совместимый новый contract/defaults.

Добавлены `engine/resources/TangentSpace.h`, `tests/PbrTexturesRegression.cpp`, `tests/RunPbrTexturesDiagnostics.ps1`, `tests/SummarizePbrTexturesDiagnostics.py`, этот отчёт, `test_projects/legacy_workspace/Assets/materials/pbr_textures.material.json`, модели `pbr_textures{,_embedded,_uv1}.gltf`, textures `pbr_base.png`, `pbr_normal.png`, `pbr_orm.png`, `pbr_control.jpg`, `pbr_control.dds`, `pbr_control_bc3.dds`.

Hash manifest `out/diagnostics/pbr-textures/changed-files.json` отделяет изменения этого этапа от уже существовавших. Предыдущие reports, physics/job/timestep/interpolation implementation, Vulkan и unlit/depth shaders сохранены. Ни одного исходного файла не удалено. Commit/push/merge/PR не выполнялись.

## 21. Команды воспроизведения

Из корня репозитория, PowerShell. Для build нужен VS developer environment; `chcp 65001` нужен Ninja showIncludes с русским Windows user path.

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
# Конфигурация уже существующего build directory:
& $cmake -S . -B out/build/shadows-dx12 -DENABLE_DX12=ON -DENABLE_VULKAN=OFF -DENABLE_TRACY=ON -DCMAKE_BUILD_TYPE=Release
# В Developer PowerShell / после VsDevCmd.bat -arch=x64 -host_arch=x64:
chcp 65001
& $cmake --build out/build/shadows-dx12 --parallel 8
& $ctest --test-dir out/build/shadows-dx12 --output-on-failure
& $ctest --test-dir out/build/shadows-dx12 -R PbrTextures --output-on-failure

Push-Location out/build/shadows-dx12
$env:WHISP_DX12_DEBUG='1'
.\PbrTexturesRegression.exe --dx12
.\LightingRegression.exe --dx12
.\GpuInstancingRegression.exe --dx12
.\WhispEngine.exe --pbr-textures-demo
# Сравнения: --no-normal-mapping / --no-pbr-textures / --lambert / --no-instancing
.\WhispEngine.exe --pbr-textures-demo --frames 160 --capture-frame editor-mapped.ppm
Remove-Item Env:WHISP_DX12_DEBUG
.\WhispEngine.exe --pbr-textures-benchmark 500 perf-500-i1.csv
.\WhispEngine.exe --pbr-textures-benchmark 500 perf-500-i0.csv --no-instancing
Pop-Location

# Все counts, оба instancing modes, GPU regressions, captures и отдельный Tracy run:
powershell -ExecutionPolicy Bypass -File tests/RunPbrTexturesDiagnostics.ps1
# Если Tracy capture utility не подготовлена:
powershell -ExecutionPolicy Bypass -File tests/RunPbrTexturesDiagnostics.ps1 -SkipCapture
& 'C:\Users\Тимофей\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' tests/SummarizePbrTexturesDiagnostics.py out/diagnostics/pbr-textures
```

Script ожидает `out/diagnostics/tracy-tools/tracy-capture.exe`. Capture utility command: `tracy-capture.exe -o textures-500.tracy -a 127.0.0.1 -s 60 -f`; engine отдельным процессом запускается с `--pbr-textures-benchmark 500 trace-500.csv --wait-tracy`. Для экспорта используется имеющаяся `tracy-csvexport.exe -f RenderSubmit textures-500.tracy` (опции перед filename); другие zone names указаны выше. Network только localhost profiler. CTest/Save могут требовать обычного Windows process вне ограниченного filesystem sandbox; validation при этом не отключается.

## 22. Остаточные ограничения

Только DX12: Vulkan source/shaders не изменялись, Vulkan build/runtime не запускались. UV0 only, один mip, fixed linear-wrap sampler; glTF sampler variations, texture transforms/extensions, skinning/animation не реализованы. Authored static tangents и MR core поддержаны, fallback generator не bit-exact MikkTSpace.

Emissive — schema reference без shader contribution. Нет IBL: metals без direct light могут быть тёмными. Нет HDR/tone mapping, masked/transparent, дополнительных light types или нового shadow algorithm. DDS support доказана для BC1/BC3 через decoded RGBA8 mip0, не для произвольного DDS format. Отдельный ручной UI lifecycle walkthrough остаётся вне заявленной автоматической проверки. Следующий этап IBL автоматически не начинался.
