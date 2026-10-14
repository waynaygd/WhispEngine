# Editor Visual Polish / Editor 2.1 — DX12

Дата: 14 октября 2026. Ветка `job_system`, исходный HEAD
`ae75fc16ab24aaac80aa9b0c670a3ba2da37597f`. Рабочее дерево перед задачей было чистым.
Состояние и SHA-256 616 tracked files сохранены в `out/diagnostics/editor-visual-polish`.
Commit, push, PR, merge и переключения веток не выполнялись. Vulkan не изменялся и не собирался.

## Результат

- ImGui line-grid заменён shader-based world grid на Y=0: minor/major lines,
  красная X и синяя Z оси, antialiasing, distance/horizon fade.
- Grid рисуется только в Edit, включая корректное скрытие в Play и Pause.
- Добавлены Solid, Procedural Gradient и Cubemap editor background modes.
- Панель View → Editor Visuals, editor preferences, отдельные helper diagnostics,
  GPU readback regression tests и воспроизводимые OFF/ON measurements.

## Grid pass и depth

Один fullscreen triangle, 3 вершины через SV_VertexID: CPU mesh и массивов линий нет.
Камера передаёт position, right/up/forward, tan(FOV/2), aspect, near/far. Pixel shader
восстанавливает world ray и пересечение с Y=0. Ray не нормализован, его forward
компонента равна 1, поэтому параметр пересечения t совпадает с view-space Z.

Depth пишется в SV_DEPTH по той же DX projection:

```text
depth = far/(far-near) - (far*near)/(far-near)/t
```

PSO использует существующий viewport D32_FLOAT DSV, LESS_EQUAL и ZERO depth write mask.
Небольшой positive depth offset 1e-7 уводит helper за coplanar geometry; grid не меняет
scene depth. Порядок: shadow pass → HDR setup (если включён существующий IBL/HDR path)
→ background → обычные scene batches → depth-tested grid → существующий HDR output → ImGui.
Это world helper, а не overlay по готовому изображению. Объекты выше плоскости закрывают grid;
геометрия ниже плоскости не закрывает линии, находящиеся перед ней.

Линии вычисляются из world X/Z через frac и fwidth. Minor/major periods плавно исчезают,
когда становятся subpixel. Fade зависит от world distance; окончание ограничено 95% camera
far plane, чтобы не получать заметную границу clipping. Почти горизонтальные rays затухают,
пересечения за камерой/near/far отбрасываются. Поддерживается камера ниже Y=0.
«Infinite» означает отсутствие CPU quad bounds и следование ray intersections при перемещении;
видимость всё равно ограничена fade/frustum и точностью float world coordinates.

`EditorGridVisible` требует active viewport, enabled helpers, show-grid, ненулевую opacity
и `!play`. Pause остаётся Runtime/Play mode, поэтому grid также скрыт. UI показывает
Hide grid in Play как readonly checked policy: включать runtime grid нельзя по условиям задачи.
Show grid, minor/major spacing, line opacity и fade distance редактируются в реальном времени.

## Background и color pipeline

Sky triangle рисуется до scene geometry, не пишет depth и имеет background depth=1.
World ray использует camera orientation; translation не двигает sky. Modes:

| Mode | Реализация |
|---|---|
| Solid clear color | Constant-color background triangle, configurable linear color/intensity. |
| Procedural Gradient | Top / horizon / ground tint, ray-height blend exponent и intensity. |
| Cubemap | TextureCube sampling base radiance mip, sky rotation/intensity, independent HDR asset key. |

Asset Browser HDR payload можно перетащить в HDR environment field. Use scene environment
копирует asset key из Scene Settings, не меняя IBL enable/intensity/rotation. Sky asset,
rotation и intensity — editor preferences, не второй источник scene lighting settings.
Все новые background passes также editor-only: в Play возвращается прежний runtime clear.

Для загрузки используется существующий ResourceManager::LoadAsync<EnvironmentResource>
и Environment loader: Radiance HDR/equirectangular asset уже конвертируется в cubemap.
Материалы и ResourceManager не переписывались; CPU payload и cache можно переиспользовать
с IBL. Sky хранит отдельную GPU source cube, чтобы выбор editor background не подменял
lighting environment. Загружается только source mip, без дополнительных GPU IBL convolutions.
При текущем Environment loader source cube имеет 64×64 faces: 384 KiB float RGBA payload
плюс allocation alignment. Raw EnvironmentCube upload API допускает source resolution до 1024.

Upload записывается в текущий command list, без отдельного GPU wait в editor frame.
Staging buffers, предыдущие textures и SRV heaps удерживаются в существующих per-frame
retirement lists до соответствующего fence. Source key/version избегают повторных uploads;
hot-reload version загружает новый cube безопасно. Pending/failed/missing sky использует
gradient fallback. Ошибка compilation/PSO отключает только helpers, сохраняя scene rendering.

Color pipeline не заменялся. В действующем RGBA32_FLOAT HDR target sky/grid output остаётся
linear и проходит прежний exposure/tone mapping/output. В RGBA8_UNORM LDR viewport shader
кодирует output в sRGB, как существующий LDR rendering path. HDR textures импортируются
как linear radiance. Нового gamma/tone-mapping режима и новых render target formats нет.

## Editor integration и сохранность Editor 2.0

`EditorVisualSettings` / `EditorVisualFrame` не содержат DX12 handles. Application передаёт
frame после ImGui camera/viewport updates; backend gate требует editor viewport. Main
RenderSystem не знает деталей DX12, его batching и snapshots не менялись.
Настройки доступны через View → Editor Visuals; demo открывает соответствующую dock tab.
Ширины property controls ограничены шириной панели, длинные описания переносятся.

Grid visibility сохраняет прежний root preferences key `grid`; новые optional `visuals`
и panel visibility расширяют editor-preferences.json v1 обратно совместимо. Старые prefs
получают defaults, invalid ranges/NaN/unsafe HDR keys отвергаются. Layout по-прежнему в
imgui.ini. Обычные изменения параметров обновляют root constants, не компилируют shader.

Scene JSON, authored components, dirty savepoint, Undo records и Runtime World не изменяются
при настройке visual preferences. Графические настройки не добавляют ECS entities и не
участвуют в shadow casting, material batches, instance buffers или render interpolation.
Существующие light icons/range/cone gizmos и selected entity AABB highlight сохранены;
нового stencil outline/billboard renderer не добавлено.

## Validation

Release build: PASS. Полный финальный CTest: **23/23 PASS, 81.58 s** (`ctest-final.log`).
Прежние 20 suites сохранены; новые CPU/DX12/shader fallback tests добавлены отдельно.
Последующая правка только ширины UI controls проверяется visual smoke `final-settings.png` и targeted log `ctest-final-ui.log`.
DX12 integration checks требуют **0 ERROR/CORRUPTION** через GetValidationErrorCount().
Shader-fallback test намеренно пишет один engine Logger ERROR об отсутствующем visual shader;
это ожидаемое сообщение теста, не ошибка DX12 Debug Layer.

`EditorVisualPolishRegression.cpp` проверяет:

- Edit / Play / Pause policy, show-grid и helper enable, defaults/range/NaN validation.
- Preference roundtrip и migration без `visuals`, unsafe asset rejection.
- Отсутствие visual state в SceneSerializer / scene dirty / authored World.
- GPU image difference grid ON/OFF, более 1000 foreground samples для точного сравнения
  occluded-object pixels, видимые линии вне объектов и grid перед geometry ниже Y=0.
- Pixel equality обычного и instanced rendering, неизменность Play image от show-grid.
- Gradient numerical color oracle и top/bottom contrast, horizon и below-plane camera.
- Шесть различимых cubemap faces, ожидаемые sRGB colors, rotation и translation invariance.
- Linear float HDR readback без лишней gamma conversion, missing cube gradient fallback.
- Повторные cube replacements при frames in flight, resize и shutdown.
- Отсутствующий visual shader: scene pixels совпадают с visuals disabled; staged shader
  автоматически восстанавливается даже при исключении.
- Настоящий Application async HDR workflow, editor sky при выключенном scene IBL,
  500 cubes + floor → 1 scene draw / 501 instances, Play/Pause/Stop и exact Editor World restore.

Существующие suites отдельно подтверждают lighting, shadows, PBR, PBR maps, IBL, OBJ/glTF,
instancing, interpolation, resource lifetime, serialization, history и Runtime isolation.
Реальные DX12 captures `final-gradient.png`, `final-cubemap.png`, `final-settings.png`
визуально проверены. Native mouse/keyboard walkthrough не выполнялся: доступная automation
среда не предоставляет native app control. Captures/API tests не выдаются за ручные clicks,
drag-and-drop или gizmo walkthrough.

## Производительность

GPU: NVIDIA GeForce RTX 4060 Laptop GPU; MSVC Release, DX12, прежние Tracy flags build.
Все measurements — Edit mode, одинаковый viewport **744×411**, camera/layout/physics
parameters и сцена для каждого размера. 100 warmup + 1200 recorded frames на case,
instancing OFF/ON в блоках по 300 (600 samples на вариант). Sky-ready filtering предусмотрен
для async loading; в этих measurements cubemap уже ready во всех 1200 recorded samples.
Depth/shadow/lighting configuration не менялась ради результатов. Диагностика Debug Layer
выполнялась отдельно от timing runs; shader initialization исключена warmup.

Modes: off — оба helper passes выключены; grid — grid + solid background; gradient —
grid + gradient; cubemap — grid + studio HDR cube. В каждом ON case ровно 1 background
и 1 grid draw, отдельно от scene draw statistics. В OFF — 0/0. Table times в ms,
CPU p50/p95/p99 — linear interpolated quantiles, остальные — mean.

| Cubes | Mode | Instancing | CPU p50/p95/p99 | GPU frame mean | Main GPU mean | Visual submit CPU mean | Scene draws |
|---|---|---|---|---|---|---|---|
| 100 | off | OFF | 3.336/4.832/6.405 | 0.080 | 0.024 | 0.000 | 101 |
| 100 | off | ON | 3.355/4.862/5.625 | 0.086 | 0.022 | 0.000 | 1 |
| 100 | grid | OFF | 2.829/4.935/5.752 | 0.088 | 0.036 | 0.011 | 101 |
| 100 | grid | ON | 3.092/4.941/5.901 | 0.096 | 0.038 | 0.011 | 1 |
| 100 | gradient | OFF | 1.877/2.651/3.569 | 0.087 | 0.037 | 0.009 | 101 |
| 100 | gradient | ON | 1.769/2.376/2.872 | 0.095 | 0.039 | 0.007 | 1 |
| 100 | cubemap | OFF | 2.002/3.860/4.591 | 0.091 | 0.038 | 0.007 | 101 |
| 100 | cubemap | ON | 1.911/3.885/4.663 | 0.094 | 0.039 | 0.007 | 1 |
| 500 | off | OFF | 7.627/9.656/10.479 | 0.126 | 0.046 | 0.000 | 501 |
| 500 | off | ON | 7.851/9.697/11.148 | 0.153 | 0.049 | 0.000 | 1 |
| 500 | grid | OFF | 7.851/9.563/10.606 | 0.143 | 0.065 | 0.015 | 501 |
| 500 | grid | ON | 7.629/9.010/9.680 | 0.161 | 0.069 | 0.017 | 1 |
| 500 | gradient | OFF | 8.042/9.681/11.049 | 0.150 | 0.069 | 0.017 | 501 |
| 500 | gradient | ON | 7.824/9.753/11.173 | 0.152 | 0.065 | 0.018 | 1 |
| 500 | cubemap | OFF | 8.340/10.182/11.740 | 0.150 | 0.069 | 0.014 | 501 |
| 500 | cubemap | ON | 7.999/9.709/10.655 | 0.163 | 0.071 | 0.014 | 1 |
| 1000 | off | OFF | 13.685/15.051/15.838 | 0.191 | 0.082 | 0.000 | 1001 |
| 1000 | off | ON | 12.589/14.142/14.970 | 0.197 | 0.073 | 0.000 | 1 |
| 1000 | grid | OFF | 13.757/15.321/16.425 | 0.205 | 0.102 | 0.021 | 1001 |
| 1000 | grid | ON | 12.294/14.014/16.293 | 0.205 | 0.092 | 0.019 | 1 |
| 1000 | gradient | OFF | 13.476/15.972/19.344 | 0.308 | 0.124 | 0.019 | 1001 |
| 1000 | gradient | ON | 12.125/14.980/15.903 | 0.281 | 0.110 | 0.016 | 1 |
| 1000 | cubemap | OFF | 13.274/15.945/19.938 | 0.181 | 0.089 | 0.015 | 1001 |
| 1000 | cubemap | ON | 12.360/15.177/18.567 | 0.332 | 0.119 | 0.013 | 1 |


На 500 cubes с instancing main GPU: OFF 0.049 ms; grid 0.069; gradient 0.065;
cubemap 0.071. Helper CPU submit: 0.014–0.018 ms, scene batching остаётся 1 draw.
На 1000 cubes с instancing main GPU: 0.073 → 0.092/0.110/0.119 ms.
В измеренном viewport helper submit занимает около 7–21 microseconds CPU. GPU frame
включает также output/UI и меняется сильнее: cubemap 1000 ON 0.332 vs OFF 0.197 ms.
Это остаётся малым относительно CPU frame ~12 ms данного workload, но не является
обещанием одинаковой стоимости на любом GPU/viewport. Стоимость fullscreen passes растёт
с количеством pixels; HDR RGBA32 path имеет другую bandwidth стоимость.

CPU timings на laptop заметно меняются между последовательными runs (frequency/scheduler/
background work). Снижение CPU frame в части ON cases не приписывается pixel shaders;
отдельные GPU timestamps и helper CPU submit приведены явно. Производительность физики,
Job System или timestep не оптимизировалась в этом этапе.
Raw CSV/logs, gather/prepare/submit, UI, GPU valid flags, helper draw counters и sky-ready
сохранены в `out/diagnostics/editor-visual-polish/*.csv` и `performance-summary.json`.
`RunEditorVisualPolishDiagnostics.ps1` восстанавливает исходные preferences/INI bytes в finally.

## Изменённые файлы

- `CMakeLists.txt`
- `WhispEngine.cpp`
- `engine/CMakeLists.txt`
- `engine/core/Application.cpp`
- `engine/core/RenderDiagnosticSample.h`
- `engine/editor/EditorLayer.cpp`
- `engine/editor/EditorLayer.h`
- `engine/editor/EditorPreferences.cpp`
- `engine/editor/EditorPreferences.h`
- `engine/editor/EditorUi.cpp`
- `engine/editor/EditorVisualSettings.cpp`
- `engine/editor/LightGizmos.cpp`
- `engine/render/EditorVisuals.h`
- `engine/render/IRenderAdapter.h`
- `engine/render/backends/dx12/Dx12EditorVisuals.cpp`
- `engine/render/backends/dx12/Dx12RenderAdapter.cpp`
- `engine/render/backends/dx12/Dx12RenderAdapter.h`
- `engine/render/backends/dx12/Dx12Shadows.cpp`
- `engine/shaders/dx12/editor_visuals.hlsl`
- `tests/EDITOR_VISUAL_POLISH_REPORT.md`
- `tests/EditorVisualPolishRegression.cpp`
- `tests/RunEditorVisualPolishDiagnostics.ps1`
- `tests/SummarizeEditorVisualPolish.py`

`preservation-audit.json` подтверждает неизменность 95 защищённых tracked файлов: Vulkan,
ECS/physics, fixed clock, interpolation, scene serialization, ResourceManager/loaders,
Job System и предыдущие reports. Existing main material/shadow/PBR/IBL shaders не менялись.
В Dx12Shadows.cpp добавлены только main-pass hooks для editor helpers, его shadow algorithms
не изменены. Deleted tracked files: 0; HEAD и branch сохранены.

## Команды воспроизведения

PowerShell из repo root; VS2022 x64 developer environment. Проверенный build:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && chcp 65001 > nul && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out/build/shadows-dx12 --parallel 8'
$ctest='C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
$env:WHISP_DX12_DEBUG='1'
& $ctest --test-dir out/build/shadows-dx12 --output-on-failure
& $ctest --test-dir out/build/shadows-dx12 -R 'EditorVisualPolish' --output-on-failure
```

Текущий CMake cache: Release, ENABLE_DX12=ON, ENABLE_VULKAN=OFF, ENABLE_TRACY=ON,
WHISP_BUILD_PHYSICS_TESTS=ON. Если нужен новый configure — использовать те же flags и
существующие FETCHCONTENT_SOURCE_DIR caches из out/build/x64-release/_deps; Vulkan не включать.

Из DX12 build cwd:

```powershell
Push-Location out/build/shadows-dx12
./EditorVisualPolishRegression.exe
./EditorVisualPolishRegression.exe --dx12
./EditorVisualPolishRegression.exe --shader-fallback
./WhispEngine.exe --editor-polish-demo
./WhispEngine.exe --editor-polish-demo --editor-gradient --frames 180 --capture-frame ../../diagnostics/editor-visual-polish/final-gradient.ppm
./WhispEngine.exe --editor-polish-demo --editor-cubemap environments/studio.hdr --frames 240 --capture-frame ../../diagnostics/editor-visual-polish/final-cubemap.ppm
./WhispEngine.exe --stress 500 --frames 180  # Runtime: grid/sky helper passes suppressed
Pop-Location
```

Measurements (отдельный run без WHISP_DX12_DEBUG):

```powershell
Remove-Item Env:WHISP_DX12_DEBUG -ErrorAction SilentlyContinue
./tests/RunEditorVisualPolishDiagnostics.ps1
& 'C:/Users/Тимофей/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/SummarizeEditorVisualPolish.py
# Подмножество: ./tests/RunEditorVisualPolishDiagnostics.ps1 -Counts 500
```

Для отдельных measurements из build cwd:

```powershell
./WhispEngine.exe --render-benchmark 500 ../../diagnostics/editor-visual-polish/off-500.csv --fixed-editor-layout --no-editor-visuals
./WhispEngine.exe --render-benchmark 500 ../../diagnostics/editor-visual-polish/grid-500.csv --fixed-editor-layout --editor-grid-only
./WhispEngine.exe --render-benchmark 500 ../../diagnostics/editor-visual-polish/gradient-500.csv --fixed-editor-layout --editor-gradient
./WhispEngine.exe --render-benchmark 500 ../../diagnostics/editor-visual-polish/cubemap-500.csv --fixed-editor-layout --editor-cubemap environments/studio.hdr
```

Одиночные runs сохраняют обычные editor preferences на shutdown; для одинакового layout
и автоматического восстановления настроек следует использовать diagnostic script.

## Ограничения и будущие расширения

- Grid ограничен camera frustum/fade и float precision, не бесконечным draw distance.
- Hide in Play заблокирован true, поскольку это обязательный editor-only контракт.
- Cubemap assets сейчас приходят через существующий Radiance HDR loader; новый native DDS
  cubemap / six-image import не добавлялся. Detail sky ограничен текущим 64-face environment cube.
- Loader выполняет своё обычное CPU IBL preprocessing даже для sky-only asset; работа
  асинхронна, до readiness используется gradient. GPU sky upload копирует только radiance cube.
- В LDR viewport значения radiance >1 ограничиваются UNORM target; можно уменьшить sky intensity
  либо использовать существующий HDR/IBL color pipeline. Новый tone mapper не вводился.
- Grid следует DSV supported opaque geometry. Текущий RenderSystem явно поддерживает opaque
  surfaces; при будущем полноценном transparent pass потребуется согласовать его порядок с grid.
- Дополнительный stencil outline и новый 3D billboard pass не делались; существующие editor
  icons/selection helpers сохранены. Full native interactive UX проверка ограничена средой.

Render/editor settings, generic frame contract и shared EnvironmentResource готовы для
будущего environment workflow; scene/runtime background policy можно добавить отдельно,
не превращая editor helper в simulation entity и не меняя текущую Runtime isolation.
