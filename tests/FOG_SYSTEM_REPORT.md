# Fog System: Distance, Height, Volumetric

Ветка `scripting`, Release/DX12. Все три режима реализованы через FogComponent. Коммиты и push не выполнялись; исходное удаление файла в `enc_temp_folder` не затрагивалось.

## Использование

1. `Create → Environment → Fog` создаёт Entity с FogComponent, Tag и обычным Transform, без Collider/Rigidbody. Также доступен `Inspector → Add Component → Fog`.
2. Выберите Mode и меняйте параметры в Inspector. Эффект работает в Edit, Play и Pause без перезапуска.
3. Enabled полностью отключает выбранный компонент. При нескольких включённых Fog выбирается компонент с наименьшим Entity index. Inspector и Console предупреждают о конфликте. Некорректные прямые ECS-данные игнорируются с диагностикой.
4. Fog глобальный: Transform Entity не влияет на эффект. Высота слоя определяется явным Base Height.
5. Undo/Redo, Scene Save/Load, prefab snapshots и Runtime World используют существующий ComponentRegistry. Отдельного формата сериализации Fog нет. Сцены без компонента сохраняют обычный rendering path.

Диагностическая сцена: `test_projects/legacy_workspace/Scenes/fog_diagnostic.json`, автоматически staged в `assets/scenes/fog_diagnostic.json`. В ней есть плоскость, три Cube на разных расстояниях/высотах, Directional/Point/Spot Lights и Fog в Volumetric Mode. Для кадров автотеста камера: `(0, 3, -8)`, yaw/pitch = 0. Камера редактора не сериализуется этой сценой; при открытии можно выбрать нужный ракурс самостоятельно и переключать Mode в Inspector.

## Inspector и валидация

| Группа | Свойства |
| --- | --- |
| Общие | Enabled, Mode, Fog Color (linear RGB), Intensity, Max Distance / Sky |
| Distance | Start Distance, End Distance |
| Height profile | Base Height, Height Falloff; показываются в Height и Volumetric |
| Height | Height Density |
| Volumetric | Scattering Color (albedo), Extinction, Anisotropy, Quality, Scene Lighting |

Intensity — коэффициент 0–1: blend intensity в Distance, множитель плотности в Height/Volumetric. Fog Color задаёт рассеянный цвет Distance/Height; в Volumetric при отключённом Scene Lighting он задаёт постоянную радиацию окружения. При включённом Scene Lighting используются реальные lights и ambient; цвет среды задаётся Scattering Color.

Проверяются конечность всех чисел, enum и RGB. End Distance должен строго превышать Start Distance; расстояния ограничены 10000, Base Height — ±10000; Height Density/Extinction/Falloff — 0–10; anisotropy — ±0.95, scattering albedo — 0–1. Невалидный Inspector setter восстанавливает прежний компонент. При десериализации проверяются итоговые согласованные значения, поэтому порядок полей JSON не мешает загрузке больших Start/End.

## Интеграция pipeline

`RenderSystem::Update` собирает Fog из активного World и передаёт immutable FogFrame с текущими view/projection через IRenderAdapter. DX12 выполняет:

```text
shadow map → HDR sky/main geometry → Fog → editor grid → tone mapping/FXAA → ImGui/gizmos
```

Fog — один полноэкранный HDR-pass независимо от режима, без изменения material shaders и без второго renderer. Режимы взаимоисключающие: Distance/Height не накладываются повторно поверх Volumetric.

Нужные ресурсы:

- Scene Color: существующий линейный `R32G32B32A32_FLOAT` HDR target. Если HDR/AA прежде не требовались, активный Fog включает тот же HDR path.
- Scene Depth: ресурс теперь `R32_TYPELESS`, с прежним `D32_FLOAT` DSV и временным `R32_FLOAT` SRV. Это относится к Viewport и native/backbuffer depth.
- Дополнительный HDR target того же размера/формата для Fog. После pass источники меняются местами; in-place read/write отсутствует.
- SRV table: Scene Color, Scene Depth, primary directional shadow map либо null SRV. Existing StructuredBuffer lights привязан отдельным root SRV; primary Directional/ambient передаются в constants.
- 512-byte upload CB на frame slot/pass, CPU descriptor heaps и timestamp readback/query heap.

До pass цвет переводится RTV→SRV, Fog target SRV→RTV, depth DEPTH_WRITE→PIXEL_SHADER_RESOURCE. После pass depth возвращается в DEPTH_WRITE и снова привязывается DSV для editor grid; новый цвет остаётся RTV до существующего HDR output. Tone mapping и FXAA выполняются после Fog; ImGui, Light/PlayerStart/selection overlays не затуманиваются.

Ресурсы разделены по двум frame slots и pass index. Повторное использование constants/readback происходит после существующего fence wait. Resize откладывает освобождение старых targets через existing retired resources; Shutdown освобождает Fog после GPU wait. Temporal history отсутствует, поэтому teleport, Play/Stop и смена сцены не требуют history reset. Cached targets при Disabled могут оставаться выделенными, но Fog draw не выполняется.

Backend без поддержки Fog показывает явную ошибку в Inspector/Console. Другой эффект под названием Volumetric не подставляется.

## Distance Fog

Позиция восстанавливается по реальной projection matrix. Для DX depth `d`:

```text
viewZ = projection[14] / (d - projection[10])
L = viewZ / dot(normalizedWorldRay, cameraForward)
a = intensity * saturate((L - startDistance) / (endDistance - startDistance))
C = lerp(sceneLinearColor, fogLinearColor, a)
```

Используется евклидово расстояние в мировых единицах, а не нелинейное значение depth. Clear depth/sky интегрируется до Max Distance / Sky. Геометрический Distance Fog достигает максимального действия на End Distance. Обработка делается до sRGB encoding, без равномерной цветной плашки поверх кадра.

## Height Fog

Профиль среды:

```text
rho(y) = heightDensity * exp(-falloff * max(y - baseHeight, 0))
tau = intensity * integral(rho(cameraY + rayY*s), s=0..min(L,maxDistance))
T = exp(-tau)
C = sceneLinearColor*T + fogLinearColor*(1-T)
```

Ниже Base Height плотность постоянна: это исключает экспоненциальное переполнение при взгляде вниз. Выше слоя интеграл вычисляется аналитически через разность экспонент; при пересечении Base Height луч разбивается на два отрезка. Для горизонтальных/почти горизонтальных лучей используется устойчивый предел. Это интеграл всей длины луча, а не выбор плотности только по высоте поверхности.

## Настоящий Volumetric Fog

Выбран depth-bounded per-pixel ray marcher. Объём задан аналитическим пространственным полем плотности, выборки выполняются в world-space точках между камерой и видимой поверхностью/Max Distance. Froxel grid для глобальной среды не обязателен: этот вариант не требует 3D textures, separate injection pass, upsampling или temporal reprojection, сохраняет точные границы opaque geometry и позволяет непосредственно проверить транспорт света.

```text
sigmaT(x) = intensity * extinction * exp(-falloff * max(x.y-baseHeight,0))
sigmaS(x) = sigmaT(x) * scatteringAlbedo
stepT = exp(-sigmaT * stepLength)
scattered += accumulatedT * (1-stepT) * albedo * incidentLighting(x, ray)
accumulatedT *= stepT
output = sceneLinearColor * accumulatedT + scattered
```

Incident lighting включает primary Directional, ambient и первые 64 дополнительных lights существующего deterministic light buffer: Directional, Point (inverse square/range cutoff), Spot (дополнительно smooth cone). Point/Spot вычисляются вдоль объёмного луча, включая участки перед геометрией и на sky rays. Это single scattering, с нормированной Henyey–Greenstein phase function и настраиваемым anisotropy.

Primary directional shadow map действительно сэмплируется в world-space на каждом шаге, с receiver bias/strength и fade к границе coverage. Вне shadow-map coverage среда освещена. Point/Spot shadows не добавлены: в существующем движке эти shadow flags также зарезервированы.

Шаги стратифицированы фиксированным per-pixel hash. Это устраняет согласованные прямоугольные полосы shadow slices; hash не зависит от номера кадра. Неподвижный ракурс воспроизводит одинаковый HDR readback. Есть early-out при transmittance < 1e-4. Low/Medium/High задают максимум **16/32/64** шагов, меняя реальную стоимость; в непрозрачной среде early-out может уменьшить число вычислений.

## Производительность

Измерения локальной Windows/DX12 Release-сборки: один primary Directional с shadow map, Point и Spot, Max Distance 70, без temporal filtering. После 12 warmup frames усреднены 8 frames каждого пресета. CPU — только запись Fog pass, GPU — timestamp queries вокруг его draw; tone mapping, main/shadow pass и readback в это GPU-время не входят. Модель GPU отдельно не определялась. Это ориентир для данной машины/сцены, не гарантия FPS.

| Размер | Preset | Шагов | CPU, мс | GPU, мс |
| --- | --- | ---: | ---: | ---: |
| 400×300 | Low | 16 | 0.056 | 0.247 |
| 400×300 | Medium | 32 | 0.051 | 0.483 |
| 400×300 | High | 64 | 0.053 | 0.956 |
| 1280×720 | Low | 16 | 0.076 | 2.389 |
| 1280×720 | Medium | 32 | 0.042 | 3.186 |
| 1280×720 | High | 64 | 0.063 | 4.348 |

Сырые данные: `tests/FOG_SYSTEM_PERFORMANCE.csv`. Дополнительный cached Fog target на два frame slots занимает примерно 4 MiB при 400×300 и 30 MiB при 1280×720, включая D3D12 allocation alignment. Если до Fog HDR targets не использовались, ещё столько же занимает включённый existing HDR path: суммарно около 8/60 MiB. Timestamp/CB/descriptor overhead мал и отдельно не детализирован. Во время resize прежние resources временно удерживаются до fence.

## Проверки

Работа выполнена последовательно:

1. Distance: Release + Fog CPU/DX12 — 2/2 PASS. Разные расстояния трёх Cube, цвет, Disabled и DX12 validation.
2. Height: Release + Fog CPU/DX12 — 2/2 PASS. Аналитический integral против numerical quadrature, пересечение слоя, горизонтальный луч, камера вверх/вниз, пространственное различие с Distance.
3. Volumetric: Release + финальные Fog CPU/DX12 — 2/2 PASS, **42 CPU / 204 native checks**. Beer–Lambert transmittance на GPU, zero extinction/global intensity, Directional/Point/Spot на sky rays, anisotropy, density, directional volume shadows, stationary stability, camera motion, three presets/two sizes, disk Scene Save/Load, Play/Pause/Stop, resize, FXAA, Delete/Undo Fog и New Scene. DX12 ERROR/CORRUPTION = 0.

Disabled в Distance побайтно вернул baseline. При нулевом volumetric extinction отличие от обычного non-HDR path не превышает 1 UNORM code на 10 каналах из 480000 — округление при переходе через HDR output; линейный HDR transport не добавляет scattering.

Полная Release-сборка всех targets выполнена через MSVC 2022/Ninja (`cmake --build out/build/x64-release -j 4`). `git diff --check` проходит; новые исходники также проверены на trailing whitespace.

Полный CTest: первый прогон **38/43 PASS**, 211.34 секунды. Результаты не скрыты:

- `Editor2Regression` / `Editor2Dx12Regression` ожидали ровно 16 builtin components. Обновлены до 17 с явной проверкой stable descriptor Fog. Оба затем PASS.
- `MaterialSystemDx12Regression` остановился на `conflict save`, `Editor25Dx12Regression` — на `randomized ordinary/instanced equivalence`. Оба прошли повторно без изменений material/rendering кода или ослабления assertions. Причина однократных сбоев отдельно не установлена.
- `LuaGameplayControllerDx12Regression` сначала остановился на Play free-fly capture, в общем повторе — на mouse look; отдельный изолированный запуск полностью PASS (12.25 секунды). GLFW/ImGui routing, Application input и Lua-контроллер в этой работе не менялись. Нестабильность этих native mouse checks при последовательном запуске остаётся замечанием к воспроизводимости, а не доказанной ошибкой Fog.

Повтор пяти тестов: **4/5 PASS** (42.19 секунды); последующий изолированный Lua: **1/1 PASS** (12.27 секунды CTest). Таким образом, **каждый из 43 тестов получил PASS**, но чистого единого полного прогона 43/43 не заявляется. Physics/CharacterMovement, interpolation, shadows/PBR/IBL/lights, Multi-Selection, Editor workflow/import, PlayerStart и asset operations проверены этим набором. Новые Fog CPU/DX12 тесты прошли во всех финальных прогонах; DX12 validation Fog — без ошибок.

Логи: `out/fog-distance-tests.log`, `out/fog-height-tests.log`, `out/fog-final-tests.log`, `out/fog-full-ctest.log`, `out/fog-recheck.log`, `out/fog-lua-isolated.log`. Это локальные build artifacts, не исходные файлы.

Runner `tests/RunLuaScriptingDiagnostics.ps1` получил необязательный `-TestRegex`. Он сохраняет и восстанавливает пользовательские `imgui.ini`/`editor-preferences.json` побайтно. Для Fog используйте `-TestRegex '^Fog(Dx12)?Regression$'`; без фильтра запускается весь набор. Укажите установленный `ctest.exe` через `-CTest` при отсутствии его в PATH. Native Lua input tests управляют курсором в собственных GLFW-окнах.

## Диагностические кадры

Сохранённые PNG из реального DX12 readback, 400×300:

- `tests/diagnostics/fog/disabled.png` — baseline, без Fog.
- `tests/diagnostics/fog/distance.png` — Distance, чёрный Fog Color для измерения затухания.
- `tests/diagnostics/fog/height.png` — Height, тот же диагностический чёрный цвет.
- `tests/diagnostics/fog/volumetric.png` — объёмный Directional scattering.
- `tests/diagnostics/fog/volumetric-no-sun.png` — та же среда при нулевой интенсивности Directional.
- `tests/diagnostics/fog/volumetric-quality-2.png` — High, primary directional shadows и цветные Point/Spot.

Тест пересоздаёт дополнительные PPM в `out/build/x64-release/fog-captures`, включая absorption, все presets и shadowed Medium. Кадры просмотрены: отражают разные режимы/освещение; после стратификации крупные shadow slice bands устранены, но пространственное зерно остаётся.

## Изменённые файлы

- `engine/render/Fog.h`, `engine/ecs/components/FogComponent.h`, `engine/ecs/FogGather.h`: настройки, CPU reference math/validation, deterministic ECS gathering.
- `engine/ecs/ComponentRegistry.cpp`: обычные properties, сериализация, атомарная cross-field валидация.
- `engine/ecs/systems/RenderSystem.h`, `RenderSystem.cpp`, `engine/render/IRenderAdapter.h`: FogFrame, синхронизация активного World, warnings, backend diagnostics.
- `engine/render/backends/dx12/Dx12Fog.cpp`, `Dx12RenderAdapter.h`, `Dx12RenderAdapter.cpp`: Fog resources/PSO/root signature, immutable constants, depth SRV, lifecycle, timing.
- `engine/render/backends/dx12/Dx12Environment.cpp`, `Dx12Shadows.cpp`: HDR integration point, один pass до editor grid/output.
- `engine/shaders/dx12/fog.hlsl`: три режима, реконструкция, analytic Height, lit volume integration.
- `engine/editor/EditorLayer.h`, `EditorTransactions.cpp`, `EditorUi.cpp`, `EditorInspector.cpp`: Create Fog, Add Component/Inspector sections, diagnostics, существующий Undo.
- `test_projects/legacy_workspace/Scenes/fog_diagnostic.json`: диагностическая сцена.
- `CMakeLists.txt`, `engine/CMakeLists.txt`: новый модуль, shader/scene staging, CPU/DX12 tests.
- `tests/FogRegression.cpp`, `Editor2Regression.cpp`, `RunLuaScriptingDiagnostics.ps1`, `FOG_SYSTEM_PERFORMANCE.csv`, `diagnostics/fog/*.png`, `FOG_SYSTEM_REPORT.md`: проверки (включая расширенный registry), измерения и документация.

## Ограничения и ручная проверка

- Global single medium, без локальных fog volumes, procedural noise/textures, multiple scattering и volumetric material injection.
- Full-resolution ray marching масштабируется с разрешением, числом шагов и lights. До 64 дополнительных lights обходятся напрямую, без отдельного froxel/light culling; primary sun всегда учитывается. Большое число lights требует будущей оптимизации.
- Temporal accumulation/denoising отсутствуют. На Low/Medium, тонких shadow features и ярких близких Point/Spot заметно пространственное зерно; при движении возможно изменение зерна/undersampling. High уменьшает шум. Полностью smooth cinematic volumetrics не заявляются.
- Volumetric directional shadows — single depth comparison с edge fade; фильтрация слабее surface PCF, ограничена существующим coverage. Point/Spot volumetric shadows не поддерживаются.
- Medium имеет постоянную плотность ниже Base Height и экспоненциальный спад выше; высота Fog Entity не используется. Height/Volumetric ограничены Max Distance; Sky интегрируется на это же расстояние.
- Расчёт ограничен opaque scene depth существующего renderer. Независимые слои прозрачности/частиц не добавлялись. Ambient аппроксимирован равномерным освещением, без отдельного volumetric IBL integration.
- Интерактивный ручной QA не проводился. Проверить удобство Inspector и Create menu, сохранённый пользовательский docking layout, внешний вид в рабочих сценах/разных ракурсах и физическую мышь Lua FPS. Автоматизированные DX12 readback/native input tests не заменяют все аппаратные сценарии.
