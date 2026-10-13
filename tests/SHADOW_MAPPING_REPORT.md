# WhispEngine — Directional Shadow Mapping, DX12

10 октября 2026, Windows, MSVC x64 Release, NVIDIA GeForce RTX 4060 Laptop GPU, Tracy 0.14.1. Работа выполнена в `job_system` поверх незакоммиченной Lighting System. Перед изменениями сохранены `out/diagnostics/shadows/before-shadow.patch`, `before-shadow-files.zip` и `before-shadow-status.txt`. Commit/push/merge/PR, смена ветки и изменения main не выполнялись.

## 1. Исходная архитектура

Изучены отчёты Physics Stress/Stability/Fixed Timestep, Render Interpolation, GPU Instancing и Lighting System. RenderSystem собирает готовые mesh/material/shader/texture handles и интерполированные RenderPose, затем готовит model/MVP/tint/inverse-transpose normal matrix. MeshVertex уже содержит position/normal/UV, stride 32. RenderInstanceData после Lighting System — 192 байта: Model offset 0, MVP 64, Tint 128, три padded normal rows 144. Асинхронная загрузка и GPU finalization остаются прежними.

Основной DX12 material PSO использует D32 depth и R8G8B8A8_UNORM color target. Forward shader вычисляет Ambient + Lambert, sampling texture и индивидуальный tint. Все draws используют общий mesh/index buffer; compatible consecutive packets объединяются. До этого этапа depth-only pass, shadow texture, world position в PS и GPU timestamps отсутствовали.

## 2. Архитектура и источник истины

`Application::m_ShadowSettings` — единственная редактируемая конфигурация теней, сохраняющаяся в течение сессии редактора. RenderSystem получает const pointer. `ShadowFrame` и `ShadowConstants` — производные snapshots; DX12 не читает World, физические Transform или историю interpolation. Направление берётся из **тех же LightingConstants**, что и Lambert. Отдельного «направления теней» нет.

В IRenderAdapter добавлены необязательные BeginShadowPass / DrawShadowMesh / DrawShadowMeshInstanced / EndShadowPass, BeginMainPass / EndMainPass, preview и диагностические timings/capture. Default BeginShadowPass=false сохраняет forward rendering неподдерживающего backend. Все GPU resources, descriptors, PSO, query heaps и barriers остаются в DX12. Vulkan-код не изменялся, Vulkan не собирался и не запускался: проверочная конфигурация строго `ENABLE_DX12=ON, ENABLE_VULKAN=OFF`.

## 3. Depth-only pass

Порядок: Gather → Prepare → Main batches + Shadow batches → Shadow depth → Main lighting → debug/editor. Shadow batching объединяет соседние packets с одинаковым mesh; texture/shader/tint не разрывают opaque depth batch. Используется тот же подготовленный массив instances. Обычный depth VS читает Model и LightVP из root constants; instanced VS читает четыре Model columns из существующего VB slot 1. PS отсутствует, RTV count=0, depth write включён. Геометрия не загружается заново.

После pass восстанавливаются viewport, scissor, color/depth targets текущего editor viewport либо backbuffer. Основной color/depth не очищается повторно. Draw counters для main и shadow разделены.

## 4. Shadow map

Один directional map на каждый из двух frame slots. Texture R32_TYPELESS, ALLOW_DEPTH_STENCIL; DSV D32_FLOAT, SRV R32_FLOAT. Clear depth=1. Собственный DSV heap; shader-visible SRV выделяется из существующего descriptor pool. Component mapping повторяет R в RGB и устанавливает alpha=1 для grayscale ImGui preview.

Каждый frame: PIXEL_SHADER_RESOURCE → DEPTH_WRITE → PIXEL_SHADER_RESOURCE. Readback временно использует COPY_SOURCE и восстанавливает PS_RESOURCE. Разрешения 512/1024/2048/4096; неподдерживаемое значение заменяется на 1024. Два maps без учёта GPU allocation padding занимают соответственно 2/8/32/128 MiB. Default 1024: coverage 48 даёт 0.046875 world units/texel; куб 0.8 занимает около 17 texels. 4096 не выбран default из-за большего расхода памяти и measured depth cost.

## 5. Light View-Projection

`Shadows.h::BuildDirectionalShadowFrame`: normalized direction распространения лучей задаёт forward LH view. Для почти вертикального света выбирается up=Z, иначе up=Y; нулевой/non-finite direction безопасно заменяется на вниз. Orthographic width/height=coverage, depth extent=depthRange, clip depth [0,1]. Центр coverage: camera position + camera forward × focusDistance. Defaults coverage=48, focusDistance=32, depthRange=100 покрывают существующий Stress Test.

Light-space XY center округляется до целого texel. CPU oracle проверяет отсутствие изменения XY projection при движении на 0.2 texel и точный шаг 2/resolution после пересечения границы. Это стабилизация поступательного движения камеры; вращение камеры/света может менять projection. Auto-fit bounds и cascades не добавлялись.

## 6. World-to-shadow

Column-major matrices и column vectors согласованы с прежними Model/MVP. Обычный b0 теперь содержит MVP, tint, Model: 144 полезных байта в прежнем aligned 256-byte CB. Instanced world position получен из индивидуального Model offset 0; stride 192 не менялся. Position и нормали world-space интерполируются до PS. Normal matrix и нормализация из Lighting System сохранены.

PS: clip=LightVP×worldPosition, NDC=clip.xyz/clip.w, UV=(x×0.5+0.5, 0.5−y×0.5). UV за [0,1], z за [0,1] или w≤0 возвращают visibility=1. Вторая инверсия Y не выполняется. Проверены depth buffer, движение caster, receiver и света, а также surfaces вне frustum.

## 7. Lighting shader и fallback

`Final.rgb = Texture.rgb × Tint.rgb × (Ambient + DirectionalDiffuse × Visibility)`.

Ambient не затеняется. Light direction для Lambert по-прежнему противоположна направлению лучей. Alpha=1, texture sampler, material overrides и unlit shader сохранены. Выключенные Shadows, Directional, global Lighting, нулевая directional intensity/strength и ошибки создания map/PSO дают существующее lit/unlit поведение. Mesh без валидных normals остаётся unlit; его геометрия может отбрасывать opaque shadow.

Root signature material: b0 CBV, t0 table, b1 Lighting constants, b2 Normal constants, b3 Shadow constants (24 DWORD), t1 shadow table; всего 52 DWORD. Shadow constants копируются в command stream, поэтому предыдущие draws не читают перезаписанный CB. Reflection разрешает у instanced shaders только совместимые LightingCB b1 (48 bytes) и ShadowCB b3 (96 bytes), сохраняя запрет per-draw b0/b2. Неподдерживаемый instance variant сохраняет ordinary PSO. `textured_unlit.hlsl` не изменён этим этапом.

## 8. Bias и PCF

Default rasterizer constant DepthBias=100, SlopeScaledDepthBias=1. Receiver bias=0.0003 в normalized depth; дополнительный normal-dependent term=0.0002×(1−N·L). Значения ограничиваются и защищены от NaN. Raster bias/slope меняют только PSO; depth VS bytecode кешируется, HLSL повторно не компилируется при редактировании bias. Остальные параметры передаются через constants.

Hard: один SampleCmpLevelZero, LESS_EQUAL, point comparison sampler, white border. PCF: девять point comparisons по сетке 3×3 с шагом 1/resolution, среднее visibility. Strength смешивает visibility с 1. GPU regression подтверждает fractional PCF edge pixels и отсутствие acne на контрольном bright floor. Полное отсутствие acne/Peter Panning для произвольного масштаба сцены не обещается: bias и coverage остаются настройками.

## 9. GPU Instancing

Shadow и main используют одинаковый span immutable prepared instances. Backend кеширует VB ranges в пределах scene submission; main batch может использовать subrange более крупного shadow mesh batch. Поэтому массив загружается **один раз**, без per-caster buffers и дублирования upload для двух passes. EndMainPass и BeginFrame очищают cache; изменение CPU данных между scene submissions не переиспользует старый диапазон.

Проверено actual DX12: 500 кубов + floor → 1 main instanced draw и 1 shadow instanced draw, 501 casters. При instancing OFF → 501/501. Shadow instancing не зависит от material instance shader: position-only depth variant общий. Ошибка instance upload сохраняет ordinary shadow fallback.

## 10. ImGui

В существующей Lighting panel добавлен collapsible Shadows: Enabled, fixed 3×3 PCF, Resolution, Coverage, Depth Bias, Slope Bias, Strength. Advanced: Focus distance, Depth range, Receiver Bias, Normal Bias. Preview показывает последнюю доступную depth map. Первый размер/позиция и item width заданы компактно; другие окна редактора не переписаны.

Настройки применяются при следующем RenderSystem Update. Resource recreation при смене resolution защищена fences; предыдущий SRV может уже присутствовать в ImGui draw data. Play/Stop, Restart и Clear меняют scene entities, сохраняют Application settings. Lifecycle diagnostic теперь явно проверяет сохранность ShadowSettings. Персистентность между перезапусками приложения и scene serialization не добавлялась.

## 11. Визуальная проверка

Реальные DX12 editor runs: active Stress Test 100/250/500/1000, 300 frames; отдельные 500 без interpolation; существующий 744-frame lifecycle с Stop/Resume, Restart/Clear/Recreate. Физические параметры и timestep не изменены. Editor backbuffer captured через GPU readback: `out/diagnostics/shadows/active-*.ppm`, `editor-final.ppm`.

Контрольные изображения обычного forward path: `out/build/shadows-dx12/shadow-readback/{off,hard,pcf,moved}.ppm`, depth.pgm; OBJ `african_head-{cast,receive}.ppm` и glTF `lighting_sphere-{cast,receive}.ppm`. PNG-копии для просмотра находятся рядом. Снимок редактора использует упорядоченный локальный `out/build/shadows-dx12/imgui.ini`; исходный UI других окон не изменялся.

## 12. GPU readback

320×240, top-down orthographic camera; colored texture (128,192,64), tint (0.8,0.7,0.6), Ambient=0.2, directional intensity=0.8, rays=(1,−1,0). Red floor OFF=78, hard-shadow=20, bright floor=78. Все RGB shadow channels проверены против Texture×Tint×Ambient с tolerance 2 UNORM levels. Ordinary/instanced кадры сравниваются с tolerance 1; complex mesh pairs совпали побитово. Depth map 512² содержит конечные [0,1] значения, реальную геометрию и untouched depth=1.

OBJ african_head: 1339 vertices/7476 indices; контрольный cast изменил 521 floor pixels, приём тени — 1141 model pixels. glTF lighting_sphere: 151/672; 474 floor pixels и 412 receiver pixels. Обе модели импортированы штатным ResourceManager/Assimp. Тест нормализует размер уже импортированного mesh для общей аналитической камеры; loader/node-transform semantics не менялись.

## 13. Regression

Новый ShadowMappingRegression включает CPU matrix/sanitation/snap oracle; production RenderSystem recorder для 100/250/500/1000; matching Model/Normal при interpolation OFF/ON, alpha 0/0.5/0.999, translation/rotation/nonuniform scale, teleport/pause/resume/delete/clear/generation reuse и instance fallback.

DX12: light/receiver/caster movement, ambient/unlit/zero intensity/direction/strength, hard/PCF, out-of-frustum, all resolutions, PSO bias recreation, 501 batching и отсутствие двойного instance upload. Fault fixture временно портит **копию** shadow HLSL в build и восстанавливает её RAII; image совпадает с Shadows OFF. Descriptor exhaustion тоже возвращает Lambert rendering, после освобождения maps восстанавливаются. Проверены уже записанные команды после удаления mesh/shader/texture/map и реальный ImGui preview с resize/retirement.

CTest: 7/7, включая ShadowMappingDx12Regression с `WHISP_DX12_DEBUG=1`. Прежние LightingRegression --dx12 и GpuInstancingRegression --dx12 также прошли, тесты не ослаблялись. Автоматический DX12 test добавлен в CTest; без --dx12 executable выполняет общие CPU/recorder checks.

## 14. Resource lifetime

Frame slot ждёт последний fence перед reset allocator, instance pages и release retired resources/descriptors. Maps раздельны по slot. При recreation/deletion во время recording COM resources и SRV indices сохраняются в retirement queue текущего frame. Обычное удаление вне recording ждёт GPU. PSO replacement также удерживает прежние PSO до fence. Null SRV + disabled constants безопасны при отсутствии map.

Debug layer включается opt-in `WHISP_DX12_DEBUG=1`. InfoQueue ERROR/CORRUPTION проверяется regression и при shutdown. Финальные GPU regressions зарегистрировали 0 ошибок. Existing renderer по-прежнему ждёт fence после Present; CPU/GPU overlap не перерабатывался. Shutdown освобождает maps/PSO/bytecode/query heaps/readbacks и сбрасывает cache/timing state.

## 15. CPU performance

Benchmark: одна и та же paused PhysicsStressScene, неизменные camera/light/poses, 100 warmup frames, затем шесть блоков по 300: OFF ordinary/instanced, Hard ordinary/instanced, PCF ordinary/instanced. После каждого переключения исключены первые 30 samples; ниже median оставшихся 270. UI и Tracy включены, GPU debug layer в benchmark выключен. Каждый CSV проверен на 1800 строк, flags, draw counts и valid GPU queries.

Все времена в ms.

| Cubes | Resolution | Mode | Instancing | Main/shadow draws | CPU frame | Gather | Prepare | Submit | Shadow CPU | Shadow GPU | Main GPU | GPU frame |
|---:|---:|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 100 | 1024 | OFF | OFF | 101/0 | 1.363 | 0.173 | 0.009 | 0.034 | 0.002 | 0.000 | 0.009 | 0.044 |
| 100 | 1024 | OFF | ON | 1/0 | 1.384 | 0.172 | 0.009 | 0.008 | 0.002 | 0.000 | 0.010 | 0.044 |
| 100 | 1024 | Hard | OFF | 101/101 | 1.497 | 0.174 | 0.009 | 0.032 | 0.029 | 0.005 | 0.017 | 0.051 |
| 100 | 1024 | Hard | ON | 1/1 | 1.542 | 0.175 | 0.010 | 0.005 | 0.015 | 0.005 | 0.014 | 0.050 |
| 100 | 1024 | PCF | OFF | 101/101 | 2.162 | 0.180 | 0.010 | 0.036 | 0.031 | 0.012 | 0.033 | 0.113 |
| 100 | 1024 | PCF | ON | 1/1 | 1.535 | 0.171 | 0.009 | 0.004 | 0.012 | 0.013 | 0.027 | 0.115 |
| 1000 | 1024 | OFF | OFF | 1001/0 | 3.735 | 1.704 | 0.101 | 0.251 | 0.003 | 0.000 | 0.046 | 0.085 |
| 1000 | 1024 | OFF | ON | 1/0 | 6.943 | 1.671 | 0.104 | 0.020 | 0.003 | 0.000 | 0.053 | 0.130 |
| 1000 | 1024 | Hard | OFF | 1001/1001 | 6.948 | 1.682 | 0.113 | 0.234 | 0.155 | 0.052 | 0.158 | 0.451 |
| 1000 | 1024 | Hard | ON | 1/1 | 6.943 | 1.676 | 0.103 | 0.006 | 0.028 | 0.015 | 0.022 | 0.072 |
| 1000 | 1024 | PCF | OFF | 1001/1001 | 6.939 | 1.671 | 0.102 | 0.235 | 0.155 | 0.050 | 0.160 | 0.432 |
| 1000 | 1024 | PCF | ON | 1/1 | 6.949 | 1.678 | 0.102 | 0.006 | 0.029 | 0.069 | 0.061 | 0.350 |
| 250 | 1024 | OFF | OFF | 251/0 | 1.685 | 0.409 | 0.022 | 0.070 | 0.002 | 0.000 | 0.013 | 0.050 |
| 250 | 1024 | OFF | ON | 1/0 | 1.578 | 0.406 | 0.022 | 0.009 | 0.002 | 0.000 | 0.012 | 0.046 |
| 250 | 1024 | Hard | OFF | 251/251 | 1.771 | 0.407 | 0.022 | 0.064 | 0.047 | 0.015 | 0.016 | 0.091 |
| 250 | 1024 | Hard | ON | 1/1 | 1.849 | 0.410 | 0.023 | 0.005 | 0.016 | 0.024 | 0.053 | 0.158 |
| 250 | 1024 | PCF | OFF | 251/251 | 2.747 | 0.456 | 0.024 | 0.086 | 0.065 | 0.019 | 0.041 | 0.193 |
| 250 | 1024 | PCF | ON | 1/1 | 2.986 | 0.542 | 0.024 | 0.005 | 0.020 | 0.027 | 0.061 | 0.189 |
| 500 | 1024 | OFF | OFF | 501/0 | 2.379 | 0.825 | 0.049 | 0.136 | 0.003 | 0.000 | 0.025 | 0.060 |
| 500 | 1024 | OFF | ON | 1/0 | 2.224 | 0.823 | 0.049 | 0.013 | 0.003 | 0.000 | 0.018 | 0.052 |
| 500 | 1024 | Hard | OFF | 501/501 | 2.917 | 0.873 | 0.051 | 0.136 | 0.107 | 0.023 | 0.015 | 0.092 |
| 500 | 1024 | Hard | ON | 1/1 | 3.754 | 0.951 | 0.060 | 0.006 | 0.028 | 0.023 | 0.016 | 0.105 |
| 500 | 1024 | PCF | OFF | 501/501 | 2.894 | 0.867 | 0.051 | 0.133 | 0.104 | 0.023 | 0.015 | 0.093 |
| 500 | 1024 | PCF | ON | 1/1 | 3.349 | 0.974 | 0.054 | 0.006 | 0.026 | 0.036 | 0.041 | 0.191 |

Полная таблица с Gather, Prepare, Submit, Shadow CPU и всеми GPU times: `out/diagnostics/shadows/performance.md`; исходные CSV и семь Tracy captures рядом. Эти wall frame times включают editor, Present/fence wait и внешнюю scheduling нагрузку; многие samples близки к 6.94 ms. Серии последовательные, GPU clocks/power не фиксировались. Из этих чисел нельзя приписывать изменения CPU frame pixel shader или утверждать точный общий speedup. Достоверный структурный результат: 501/501 → 1/1 draws и один instance upload. Physics settings не менялись.

## 16. GPU timestamps

DIRECT queue GetTimestampFrequency, TIMESTAMP query heap: 66 entries per slot (frame start/end и до 16 scene submissions × shadow/main start/end), readback per slot. ResolveQueryData записывает только initialized contiguous queries; чтение после completed slot fence. Present сохраняет timings **отправленного frame**, CSV не использует предыдущие values. Более 16 submissions помечают aggregate invalid. Query allocation failure не ломает rendering.

Shadow GPU охватывает clear/transitions/depth/restoration; Main GPU — scene material draws; GPU frame — command list work с clear/editor, без Present/display latency. CPU Tracy зоны: ShadowPrepare, ShadowPass, ShadowDepthDraw, ShadowInstancedDraw, ShadowResourceTransition, ShadowResolve, ShadowBind, прежние RenderGather/Prepare/Submit. GPU duration не вычисляется из CPU зоны.

В capture 500/1024 подтверждены 1900 ShadowPrepare/ShadowPass, 300600 ShadowDepthDraw (600 ordinary frames × 501), 600 ShadowInstancedDraw и по 1200 transitions в каждом направлении. Экспорт: `out/diagnostics/tracy-tools/tracy-csvexport.exe -f Shadow out/diagnostics/shadows/shadow-500-1024.tracy > out/diagnostics/shadows/tracy-shadow-zones.csv`.

500 cubes, PCF + instancing, median 270 samples; separate runs.

| Resolution | Two maps MiB | PCF instanced Shadow GPU ms | Main GPU ms | GPU frame ms |
|---:|---:|---:|---:|---:|
| 512 | 2 | 0.027 | 0.060 | 0.210 |
| 1024 | 8 | 0.036 | 0.041 | 0.191 |
| 2048 | 32 | 0.122 | 0.036 | 0.308 |
| 4096 | 128 | 0.179 | 0.031 | 0.485 |

## 17. Изменённые файлы этого этапа

- Новые: `engine/render/Shadows.h`, `engine/render/backends/dx12/Dx12Shadows.cpp`, `engine/shaders/dx12/shadow_depth.hlsl`.
- Общая интеграция: `engine/render/IRenderAdapter.h`, `engine/render/RenderInstanceData.h` (submission counters; payload unchanged), `engine/ecs/systems/RenderSystem.{h,cpp}`, `engine/core/Application.{h,cpp}`, `engine/core/RenderDiagnosticSample.h`, `WhispEngine.cpp`, `engine/editor/EditorLayer.cpp`.
- DX12: `engine/render/backends/dx12/Dx12RenderAdapter.{h,cpp}`, `engine/shaders/dx12/textured.hlsl`, `engine/CMakeLists.txt`, `CMakeLists.txt`.
- Tests/tools/report: `tests/ShadowMappingRegression.cpp`, `tests/RunShadowDiagnostics.ps1`, `tests/SummarizeShadowDiagnostics.py`, этот отчёт.

Прежние локальные Lighting.h, MeshData/MeshLoader, RenderInstanceData, LightingRegression, GpuInstancingRegression, unlit shader, glTF fixture и Lighting report сохранены. JobSystem, PhysicsSystem, FixedStepClock, Broadphase, ResourceManager, RenderInterpolation и Vulkan backend не изменены этим этапом.

## 18. Воспроизведение

В **x64 Native Tools Command Prompt for VS 2022**, из корня репозитория (cmake/ctest доступны через VS tools):

```bat
chcp 65001
cmake -S . -B out/build/shadows-dx12 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DASSIMP_INSTALL_PDB=OFF -DENABLE_DX12=ON -DENABLE_VULKAN=OFF -DENABLE_TRACY=ON -DWHISP_BUILD_PHYSICS_TESTS=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DFETCHCONTENT_SOURCE_DIR_GLFW=out/build/x64-release/_deps/glfw-src -DFETCHCONTENT_SOURCE_DIR_ASSIMP=out/build/x64-release/_deps/assimp-src -DFETCHCONTENT_SOURCE_DIR_STB=out/build/x64-release/_deps/stb-src -DFETCHCONTENT_SOURCE_DIR_TRACY=out/build/x64-release/_deps/tracy-src -DFETCHCONTENT_SOURCE_DIR_ENKITS=out/build/x64-release/_deps/enkits-src
cmake --build out/build/shadows-dx12 --parallel 8
cd out/build/shadows-dx12
ctest --output-on-failure
set WHISP_DX12_DEBUG=1
ShadowMappingRegression.exe --dx12
LightingRegression.exe --dx12
GpuInstancingRegression.exe --dx12
set WHISP_DX12_DEBUG=
WhispEngine.exe --stress 500
WhispEngine.exe --stress 500 --hard-shadows
WhispEngine.exe --stress 500 --no-shadows
WhispEngine.exe --stress 500 --frames 300 --capture-frame editor.ppm
WhispEngine.exe --shadow-benchmark 500 demo-paused.csv --capture-frame editor-paused.ppm
```

Для первой установки без cached dependencies уберите FETCHCONTENT_SOURCE_DIR_* и FULLY_DISCONNECTED. Runtime working directory — build: triangle DXIL и материал/shadow HLSL копируются build targets. На этой машине первоначальная CMake detection исказила русский `/showIncludes` prefix; локальные generated CMake compiler settings исправлены и Engine/tests принудительно пересобраны. Prefix Ninja теперь `Примечание: включение файла: `. При похожей проблеме не доверять incremental headers: исправить codepage/prefix и повторить сборку. Source CMake не содержит машинного обходного пути.

Из PowerShell в корне:

```powershell
./tests/RunShadowDiagnostics.ps1
./tests/RunShadowDiagnostics.ps1 -Counts 500 -Resolution 512 -SkipRegression -SkipActive
./tests/RunShadowDiagnostics.ps1 -Counts 500 -Resolution 2048 -SkipRegression -SkipActive
./tests/RunShadowDiagnostics.ps1 -Counts 500 -Resolution 4096 -SkipRegression -SkipActive
python tests/SummarizeShadowDiagnostics.py out/diagnostics/shadows
```

Capture executable по умолчанию `out/diagnostics/tracy-tools/tracy-capture.exe`; `-SkipCapture` позволяет повторить CSV без Tracy TCP. `--shadow-benchmark 500 output.csv --shadow-resolution 1024` запускает шесть paused comparison blocks. `--wait-tracy` ожидает локальное подключение. Сохранены captures `shadow-{100,250,500,1000}-1024.tracy` и resolution sweep 500. Sandbox разрешение для локального Tracy TCP было получено; внешняя публикация не выполнялась.

## 19. Ограничения

Один directional opaque map, fixed square orthographic coverage, без каскадов/auto-fit/caster culling. Geometry вне coverage может не отбрасывать видимую тень; receiver вне frustum явно lit. Большая coverage уменьшает детализацию; rotation и изменение coverage/resolution могут менять texel grid. Transparent/alpha-tested material shadows, skinned/morphed meshes, negative-scale winding policy и различие cast/receive flags не добавлены. Existing visible render packets считаются opaque casters; hidden objects не собираются. Fixed 3×3 PCF не имитирует физическую penumbra.

Предыдущий frame map preview может быть stale при отключённых Shadows; это последняя доступная map. Settings не сериализуются на диск. Общие GPU time limits и descriptor pool остаются конечными с безопасным fallback. Производительные concurrent frames, CSM, point/spot lights, Vulkan shadows, PBR, gamma/tone mapping, SSAO/bloom/deferred не реализованы.

## 20. Подготовка следующих этапов

Этап A — отдельный Material System: явные opaque/alpha-tested pass contracts, cast/receive flags, совместимый batch key и material serialization. Сохранить depth-only adapter boundary и shared prepared poses.

Этап B — PBR: сначала определить linear/sRGB texture и render target policy, затем metallic/roughness/BRDF/IBL; shadow visibility применять к прямому light contribution. Не заменять текущий Lambert без отдельного этапа и readback oracle.

Этап C — редактор: scene render settings serialization, layout presets и понятные handles светового frustum; preview/debug overlays через backend capabilities. Для нескольких lights потребуется light gather и явное соответствие light ↔ shadow resources. Эти этапы только описаны, реализации в текущей задаче нет.
