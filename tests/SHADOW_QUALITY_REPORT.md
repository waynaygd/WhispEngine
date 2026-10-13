# WhispEngine — Shadow Quality & Stability, DX12

13 октября 2026. Windows, MSVC x64 Release, RTX 4060 Laptop GPU, Tracy 0.14.1. Ветка `job_system`. DX12-only cache: `ENABLE_DX12=ON`, `ENABLE_VULKAN=OFF`. Vulkan не изменялся, не собирался и не запускался. PBR не начинался; commit/push/merge/PR и смена ветки не выполнялись.

До изменений выполнен `git status`. Все 57 исходных dirty/new файлов сохранены: `out/diagnostics/shadow-quality/before-tree.zip` с путями относительно repository и SHA256 manifest `before-sha256.json`; также сохранены исходный архив `before-files.zip`, `before.patch` и `before-status.txt`. Материалы, ResourceManager, loaders, PhysicsStressScene, Job System, физика, timestep и реализация interpolation сохранены. Изменения относительно этого snapshot перечислены ниже.

## 1. Диагностика и границы доказательств

Прочитаны SHADOW_MAPPING_REPORT, MATERIAL_SYSTEM_REPORT, LIGHTING_SYSTEM_REPORT и GPU_INSTANCING_REPORT; проверены RenderSystem, camera projection, instance data, MaterialState, Lighting/ShadowSettings, DX12 main/depth PSO и HLSL, editor и regression.

Новое пользовательское вложение содержит только текст задания. Исходные пользовательские скриншоты здесь недоступны, поэтому конкретный синий объект на них не идентифицирован. Создан отдельный синий наклонный receiver с аналитическими нормалями и всего двумя треугольниками. Его полосы можно воспроизвести независимо от материалов, interpolation, smooth-normal генерации и z-fighting.

| Дефект | Подтверждённая причина и воспроизведение | Результат |
|---|---|---|
| Диагональные полосы на контрольной наклонной плоскости | Point comparison использовал глубину текущего pixel, хотя map хранит глубину центра другого texel. При отключении всех bias: 17 075 затенённых пикселей в контрольном ROI | Receiver-plane correction: 0 таких пикселей, включая зеркальную плоскость |
| Исчезновение близкой тени | Fixed focus = camera + forward × 32; квадрат coverage не содержит ближний camera frustum. Направление взгляда переносит область далеко от близкого receiver | GPU fixture теряет контрольную тень в 4/4 fixed-focus ориентациях; fitted mode сохраняет её во всех четырёх |
| Некорректность negative scale | Отрицательный model determinant меняет winding, исходный CullBack не учитывал отражение | Отдельные mirrored PSO для main/depth и ordinary/instanced; настоящий backface продолжает отсекаться |
| Ступенчатый Hard / широкий Soft | Плотность coverage/resolution и footprint 3×3 point comparisons | Hard остаётся одним point compare; Soft имеет управляемый радиус в texels |
| Резкий край области | Вне UV/depth frustum visibility сразу становилась 1 | Плавный fade внутри последних 5% XY и depth extent; coverage debug отличает выход из карты |

Со старыми штатными bias контрольная двухтреугольная плоскость была чистой и до исправления. Не утверждается, что все пользовательские полосы вызваны той же причиной. Остаточный shadow terminator у грубой гладко затенённой геометрии и самозатенение в углублениях моделей описаны отдельно.

## 2. Математический и графический путь

RenderSystem получает интерполированный RenderPose, строит Model/MVP/inverse-transpose normal matrix и immutable per-instance snapshot. Shadow и main используют **одни и те же** model/normal данные. Depth VS: `LightVP * Model * position`; ordinary — root constants, instanced — существующий instance VB. Матрицы column-major, column vectors, LH view. Near depth = 0, far = 1; D32_FLOAT clear = 1, LESS/LESS_EQUAL comparison, positive raster depth bias удаляет сохранённый depth от света. Reversed Z не используется.

PS восстанавливает world position, затем shadow clip/NDC. UV = `(x*.5+.5, .5-y*.5)`: Y инвертируется один раз. Geometry outside map не получает ложную тень. Texture sampling, индивидуальный tint и формула `BaseColor * (Ambient + Lambert * Visibility)` сохранены; Ambient не затеняется. Gamma/sRGB/tone mapping не менялись.

Root ShadowCB вырос с 96 до 112 bytes: LightVP и три float4. Reflection проверяет `sizeof(ShadowConstants)`, root содержит 28 DWORD для b3. Общая material root signature занимает 57 DWORD, ниже лимита 64. Instance payload остаётся 192 bytes; stride, input layouts и normal rows не изменены. `shadow_depth.hlsl` проверен и оставлен без изменений.

## 3. Receiver-plane depth correction

До per-pixel выхода из frustum вычисляются `ddx/ddy(UV, depth)`. Решение 2×2 даёт геометрический градиент depth по shadow UV. Для **каждого** tap, включая Hard:

```text
sampleCenter = (floor(sampleUV / inverseResolution) + 0.5) * inverseResolution
compareDepth = receiverDepth - bias + dot(depthGradient, sampleCenter - receiverUV)
```

Это поправка на плоскость receiver, а не дополнительный большой constant bias. Градиент происходит из геометрической world position, а не из предположения о совпадении smooth normals и граней. При почти вырожденном UV Jacobian correction безопасно отключается; пользовательские raster/receiver/normal bias остаются применимыми. OFF переключатель в Advanced нужен для диагностики и воспроизводит полосы.

Минимальный normalized depth floor `5e-6` покрывает D32/raster subpixel/transform roundoff. Полностью убрать его нельзя обещать даже при пользовательском Receiver Bias = 0. При depth extent 100 это 0.0005 world units. Обычная и зеркальная контрольные плоскости с нулевыми пользовательскими bias проходят. При Shadows/Receive OFF uniform branch не выполняет лишние derivatives и sampling.

## 4. Bias и culling

- Constant Bias: прежний D32 rasterizer DepthBias integer, default 100. Это не фиксированное смещение в world units: величина зависит от floating depth exponent.
- Slope Bias: прежний slope-scaled rasterizer term, default 1.
- Receiver Bias: normalized [0,1] depth, default 0.00002 вместо 0.0003.
- Normal Bias: **теперь world normal offset, заданный в shadow texels**, default 0.25. Shader умножает на effective coverage/resolution и `sqrt(1-(N·L)^2)`. Старый параметр лишь вычитался из depth в зависимости от normal; старые численные значения нельзя переносить без учёта новых единиц.

Direction означает направление лучей; offset использует нормаль поверхности, Lambert — `-direction`. Нормали по-прежнему inverse-transpose и normalized. Проверены rotation, non-uniform scale, reflected geometry, incline, contact и низкий угол света. GPU contact oracle проверяет затенённый pixel у основания; visible contact image сохранён. Это не гарантия нулевого Peter Panning для любого размера объекта и grazing angle.

Глобальное front-face culling не включено: открытые и тонкие receivers остаются пригодными. Обычный CullBack сохраняется; Double Sided использует CullNone. Для отрицательного determinant singlesided packet получает производный `reverseWinding`; отдельные mirrored PSO меняют FrontCounterClockwise. Флаг не добавлен в material JSON и не стал вторым источником material settings. Его учитывают main/shadow batch keys. Double Sided canonicalizes mirror flag, поэтому не создаёт лишние batches.

Все новые PSO участвуют в прежнем fence retirement. При частичном отказе создания instance PSO capability сбрасывается и ordinary fallback сохраняется.

## 5. Shadow projection

Два режима:

1. **Stabilized camera fit**, default. Near camera frustum до Shadow Distance заключён в bounding sphere. Центр — середина near/far segment; radius рассчитывается по FOV/aspect/far corners и округляется вверх до world unit. Эффективная width — max(minimum Coverage, sphere diameter), плюс guard для texel snapping. Depth extent — max(Depth Range, width + 20), чтобы оставить дополнительное пространство для casters.
2. **Fixed forward focus**, прежний алгоритм. Coverage и Focus Distance остаются непосредственно управляемыми; полезен для сравнения, конкретных сцен и меньшей map density cost.

Sphere размер не зависит от yaw/pitch/light orientation: вращение камеры не «накачивает» scale projection. Light-space XY center округляется до texel; старый CPU snap oracle сохранён. Camera translation перемещает область ступенями texel. CPU oracle проверяет все восемь frustum corners при нескольких yaw/pitch и постоянство projection scale. Actual DX12 проверяет также три повернутых main camera view, camera-relative map rotation/translation и ожидаемую область тени.

В измеренной stress camera default effective coverage = 84.082031, depth extent = 104.082031; 2048 даёт 0.041056 world units/texel. Старый fixed 48/1024 = 0.046875. Новый профиль расширяет покрытие при немного большей плотности texels, расходуя больше памяти и depth fill. Settings остаются Application runtime/session state; Play/Stop/Restart/Clear не сбрасывают их. Disk serialization не добавлена.

## 6. Исчезновение и края карты

Ошибка focus не связана с потерей texture resource при повороте: ресурсы, viewport/scissor и depth direction работают корректно. Fixed area действительно могла не содержать видимый близкий receiver. Camera-fit исправляет проверенный сценарий без CSM. Depth margin также снижает обрезание casters в обычных сценах.

Map остаётся конечной. Receivers вне её XY/depth bounds возвращают lit; последние 5% extent плавно уменьшают shadow strength. GPU edge sweep проверяет монотонный fade и lit результат за границей, сохраняет четыре изображения. Fade скрывает резкую границу, но не создаёт отсутствующий occluder. Coverage debug: зелёный — внутри, красный — вне frustum; Visibility debug показывает фактический shadow factor.

## 7. Hard/Soft и параметры качества

Hard — один `SampleCmpLevelZero`, comparison POINT sampler, без аппаратного bilinear blur. Автоматический oracle проверяет отсутствие fractional filtered pixels в контрольной внутренней области. Soft — 3×3 point comparisons с шагом `radius/resolution`, default radius 1. Получено 85 fractional edge pixels; radius 0 даёт изображение Hard побитово. Проверены радиусы 0/0.5/1/2 и resolutions 512/1024/2048/4096. Taps вне UV явно lit; white border не создаёт ошибку сравнения при выходе tap за карту.

Новый default: Enabled, camera-fit, 2048, minimum coverage 48, distance 32, minimum depth 100, constant 100, slope 1, receiver 0.00002, normal 0.25 texel, Soft radius 1, strength 1, edge fade 0.05, receiver-plane correction ON. Fixed/1024 доступен как более дешёвый профиль. 5×5, jitter, TAA и blur не добавлены.

## 8. ImGui и демонстрация

Lighting → Shadows: Enabled, Shadow quality demo, Hard/Soft, Radius (texels), Fit near camera frustum, Restore defaults, Resolution, Minimum Coverage/Coverage, Constant/Slope Bias, Strength. Advanced: Shadow Distance/Focus Distance, Depth Range, normalized Receiver Bias, Normal (texels), Receiver Plane Correction, edge fade, Shaded/Visibility/Coverage debug. Depth map preview сохранён.

Выводятся effective coverage, world units/texel, light near/far 0/extent, shadow draws и GPU ms. CPU submission statistics берутся из предыдущего RenderSystem snapshot, поскольку backend обнуляет counters в BeginFrame. GPU timings — последний завершённый query frame, не синхронный текущий UI frame.

`--shadow-demo` создаёт floor/contact cube/rotated nonuniform cube/negative-scale cube/blue inclined OBJ/african_head OBJ/lighting_sphere glTF/оба Material Slots mesh. Это paused render demo, не модификация физических stress parameters. Default texture/tint на диагностических объектах позволяет видеть форму; slot models сохраняют свои импортированные материалы и textures. Кнопка не сбрасывает пользовательские настройки света. Редактор целиком не переписан.

## 9. Визуальные результаты

GPU readback PPM/PGM и просмотренные PNG находятся в `out/diagnostics/shadow-quality`. Просмотрены baseline zero-bias, исправленная плоскость, Hard/Soft, editor Soft/Visibility, camera/coverage/contact images. Это просмотр сохранённых **actual GPU images**, не ручной click-through редактора. Не заявляется ручная проверка всех controls мышью.

- `before-images/zero-bias.png` → `after-images/zero-bias.png`: диагональные полосы → ровная плоскость.
- `after-images/caster-{off,hard,soft}.png`: отсутствие, резкий край и PCF смягчение.
- `after-images/fixed-camera-*.png` → `camera-*.png`: воспроизведение fixed focus clipping → устойчивое nearby coverage.
- `actual-camera-{0,1,2}[-off].png`: разные настоящие main camera views.
- `normal-*.png`, `correction-off.png`, `mirror.png`, `contact.png`, `cube-*.png`, `light-*.png`, `edge-*.png`, `depth.png`, `visibility.png`, `coverage.png`.
- `editor-{off,hard,soft,visibility,coverage}.png`, `material-slots.png`, `stress-500.png`: обычные editor runs.

Исходные PPM/PGM — данные regression; PNG conversion не меняет цветовую модель. Аналитическая inclined plane не имеет полос даже при пользовательских bias = 0. Complex model grooves/occlusion и небольшая irregular terminator граница в Visibility debug не выдаются за устранённый универсальный mesh-quality дефект.

`comparison.png` объединяет восемь контрольных readbacks. Actual camera fixture намеренно использует 512 при fitted coverage около 84: её PCF edge показывает грубую плотность map, примерно 0.164 world units/texel. Это ожидаемое ограничение дешёвого режима; default editor 2048 имеет вчетверо более плотную сетку. Не заявляется, что низкое разрешение стало визуально эквивалентно 2048.

## 10. GPU readback и regression

Полный CTest: **10/10 passed**, 23.84 s (`ctest-final.log`). Затем расширенный ShadowQualityRegression повторён с low-angle analytic position и сохранением дополнительных bias images (`quality-readback-final.log`). Отдельно прошли прежние LightingRegression/GpuInstancingRegression `--dx12`, MaterialSystemRegression/ShadowMappingRegression `--dx12`, production lifecycle и interpolation OFF. Debug Layer ERROR/CORRUPTION = 0 по executable assertions и engine shutdown checks.

ShadowQualityRegression выполняет реальные GPU draws/readback, проверяет:

- Аналитический inclined plane ROI >10 000 pixels, shadow OFF/ON, all resolutions, Hard/Soft, zero user bias, correction OFF/ON, normal offsets.
- Mirror singlesided plane и точное ordinary/instanced RGBA равенство.
- Caster shadow location: 845 dark pixels, centroid X=190.876 при ожидаемом смещении вправо; проверяется геометрия, не только средний цвет.
- Hard fractional pixels = 0; Soft fractional edges >4 (получено 85); radius 0 == Hard.
- Fixed-focus clipping reproduction, fitted camera yaw/translation, три main camera viewpoints, контакт с floor, low-angle analytic world-to-pixel shadow location.
- Shadow visibility/coverage debug, frustum-edge fade, rotated nonuniform cube, high/low/reversed light, конечный [0,1] depth readback.

Прежние regression сохраняют OBJ african_head и glTF lighting_sphere cast/receive, shader/resource fallback, resize и editor preview resource recreation, destroy-before-submit/retirement/shutdown, interpolation alpha 0/0.5/0.999, teleport/pause/resume, creation/delete/generation reuse/Clear/Restart и 100/250/500/1000 batching. Material regression дополнительно проверяет mirrored main PSO, **настоящий** backface после rotation без переноса модели за camera near plane, Double Sided, shared assets/live edits, multi-submesh texture/tint и cast/receive flags. Старое ожидаемое исчезновение зеркального model заменено проверкой правильного winding, а не удалением culling tests.

## 11. CPU/GPU performance

Исходный benchmark снят **до shader/backend changes**. Один paused stress process на count, одинаковые camera/light/materials, 100 warmup frames, шесть blocks по 300: OFF/Hard/Soft × instancing OFF/ON. Из каждого block исключены первые 30 frames; 270 samples/mode, valid GPU timestamps и batching проверены для каждой строки. Tracy disconnected в обоих сравнительных профилях; debug layer выключен для timing. Физика не исполняется в paused benchmark и её настройки не изменены.

`before`: исходный fixed focus, 1024, coverage 48, depth 100, receiver 0.0003, старый normal-dependent depth term 0.0002. `after-fixed`: тот же projection/resolution/receiver bias, новый normal world offset выключен, correction/fade включены. Normal bias имеет другой смысл, поэтому его старое число не названо идентичным настройкам нового offset. `after-default`: новые defaults, effective coverage 84.082/depth 104.082, 2048.

Реальные p50, ms, Soft + instancing ON:

| Cubes | Profile | Shadow CPU | Shadow GPU | Main GPU | GPU frame | CPU frame |
|---:|---|---:|---:|---:|---:|---:|
| 100 | before | .0269 | .0225 | .0184 | .2447 | 7.2737 |
| 100 | after-fixed | .0179 | .0399 | .0317 | .4710 | 6.9440 |
| 100 | after-default | .0178 | .0963 | .0276 | .4690 | 6.9256 |
| 250 | before | .0277 | .0307 | .0594 | .2775 | 7.4322 |
| 250 | after-fixed | .0212 | .0379 | .0512 | .5018 | 6.9407 |
| 250 | after-default | .0214 | .0963 | .0328 | .5130 | 6.9440 |
| 500 | before | .0417 | .0389 | .0348 | .1505 | 8.9424 |
| 500 | after-fixed | .0249 | .0635 | .0707 | .5468 | 6.9431 |
| 500 | after-default | .0257 | .1341 | .0420 | .5898 | 6.9267 |
| 1000 | before | .0452 | .1290 | .1014 | .4035 | 9.9672 |
| 1000 | after-fixed | .0297 | .1075 | .0922 | .6083 | 6.9085 |
| 1000 | after-default | .0304 | .1751 | .0604 | .6410 | 6.9255 |

500/default Soft: Shadow GPU p50/p95/p99 = .134/.160/.261; Main = .042/.046/.059; GPU Frame = .590/1.038/4.090; CPU Frame = 6.927/8.873/10.842. Ordinary/instanced default Soft: main+shadow draws 501/501 → 1/1; Shadow GPU .138 → .134, Main .090 → .042. Instance upload остаётся **96 192 bytes**, 501 instances; данные main/shadow не дублируются.

Полные 72 rows, все resolutions/profiles/modes, CPU/GPU p50/p95/p99: `performance.md`, machine-readable `percentiles.csv`; исходные CSV в `before/`, `after-fixed/`, `after-default/`. Warmup/mode transient spikes не включены в steady percentiles. Это один sequential run, без power/clock lock и статистического доказательства небольших дельт. Shadow depth GPU иногда изменился даже в fixed profile с тем же geometry pipeline — признак системной вариативности, а не стоимости pixel shader. GPU frame включает ImGui/другие команды. Изменение CPU frame не приписывается shader; universal FPS improvement не заявлен.

Default 2048 требует 32 MiB для двух raw depth maps против 8 MiB при 1024 и увеличивает depth fill. Измеримый выигрыш — расширение near-camera coverage, уменьшение texel footprint и устранение контрольных полос; профиль Fixed/1024 оставлен для стоимости/качества. 4096 проверен readback/resource recreation, но его performance sweep в этой задаче не измерялся и он не выбран default.

## 12. Tracy

Отдельный изолированный capture `quality-500.tracy`: 1 903 frames, 14.21 s, 1 807 113 zones, 5 427 215 bytes. `tracy-shadow.csv`: ShadowPass 1900 events, mean 49.388 µs; ShadowPrepare 1.161 µs; ShadowInstancedDraw 600 events, mean 12.204 µs; ShadowResolve 3.133 µs. Это смешанные OFF/ordinary/instanced modes, не steady GPU medians. GPU времена выше — hardware timestamps. Trace не подключался к одновременно запущенным regression process.

## 13. Изменённые файлы

Изменены относительно snapshot: `CMakeLists.txt`, `WhispEngine.cpp`, `engine/core/Application.h/.cpp`, `RenderDiagnosticSample.h`, `engine/ecs/systems/RenderSystem.cpp`, `engine/editor/EditorLayer.cpp`, `engine/render/RenderInstanceData.h`, `MaterialState.h`, `Shadows.h`, `engine/render/backends/dx12/Dx12RenderAdapter.h/.cpp`, `Dx12Shadows.cpp`, `engine/shaders/dx12/textured.hlsl`, `tests/MaterialSystemRegression.cpp`, `tests/ShadowMappingRegression.cpp`.

Новые: `engine/assets/models/shadow_inclined.obj`, `tests/ShadowQualityRegression.cpp`, `RunShadowQualityDiagnostics.ps1`, `SummarizeShadowQualityDiagnostics.py`, этот отчёт. Прежние отчёты не переписаны; их старые defaults/limitations относятся к соответствующим этапам.

## 14. Воспроизведение

Из repository, x64 VS Developer Command Prompt, существующий DX12-only cache:

```bat
chcp 65001
cmake --build out/build/shadows-dx12 --parallel 8
ctest --test-dir out/build/shadows-dx12 --output-on-failure
```

Чистый build, если dependencies доступны:

```bat
cmake -S . -B out/build/shadow-quality-dx12 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DASSIMP_INSTALL_PDB=OFF -DENABLE_DX12=ON -DENABLE_VULKAN=OFF -DENABLE_TRACY=ON -DWHISP_BUILD_PHYSICS_TESTS=ON
cmake --build out/build/shadow-quality-dx12 --parallel 8
ctest --test-dir out/build/shadow-quality-dx12 --output-on-failure
```

PowerShell, из repository:

```powershell
./tests/RunShadowQualityDiagnostics.ps1
# Без локального Tracy capture:
./tests/RunShadowQualityDiagnostics.ps1 -SkipCapture
python tests/SummarizeShadowQualityDiagnostics.py out/diagnostics/shadow-quality
./out/diagnostics/tracy-tools/tracy-csvexport.exe -f Shadow out/diagnostics/shadow-quality/quality-500.tracy > out/diagnostics/shadow-quality/tracy-shadow.csv
Push-Location out/build/shadows-dx12
$env:WHISP_DX12_DEBUG='1'
./ShadowQualityRegression.exe --check ../../diagnostics/shadow-quality/after-images
./ShadowMappingRegression.exe --dx12
./MaterialSystemRegression.exe --dx12
./WhispEngine.exe --shadow-demo
./WhispEngine.exe --shadow-demo --hard-shadows --shadow-resolution 512
./WhispEngine.exe --shadow-demo --shadow-debug 2
./WhispEngine.exe --shadow-demo --no-instancing --no-interpolation
./WhispEngine.exe --shadow-benchmark 500 fixed.csv --shadow-fixed --shadow-resolution 1024 --shadow-receiver-bias .0003 --shadow-normal-bias 0
./WhispEngine.exe --shadow-benchmark 500 default.csv
Pop-Location
git diff --check
```

Script пишет собственные diagnostics, проверяет counts/timestamps/exit codes; требуется matching Tracy capture CLI 0.14.1 и localhost:8086. Regression запускается с debug layer; измерения без него. На этой машине Save tests требуют запуска вне tool sandbox, запрещающего `weakly_canonical` directory query assets; проверки путей MaterialLoader не ослаблялись. Первая sandbox попытка и provisional culling test failure сохранены в логах; финальные результаты приведены выше.

Baseline был снят до правок командами `RunShadowDiagnostics.ps1 -ArtifactDirectory out/diagnostics/shadow-quality/before -SkipCapture -SkipActive -SkipRegression` и `ShadowQualityRegression --baseline .../before-images`. `--baseline` только отключает новые quality assertions и **не переключает новый executable обратно на старый shader**. Для повторения старого результата нужен исходный snapshot `before-tree.zip`/patch в отдельной копии исходников, затем old DX12 build; текущую рабочую копию нельзя считать baseline.

## 15. Остаточные ограничения и следующий этап

Одна square orthographic map. Coverage/Depth Range sanitization сохраняет пределы 500/1000; экстремальные FOV/aspect/distance могут превысить эти пределы, и полное покрытие такого frustum не гарантируется. Very distant/large scenes и casters вне fitted sphere + depth margin требуют большей области или следующей архитектуры. Низкий свет может проецировать длинные тени за пределы карты/receiver; fade делает границу понятнее, но не восстанавливает отсутствующую геометрию.

Размер projection стабилен при yaw/pitch, но changing FOV/aspect/distance/coverage/resolution и light direction изменяет grid. Не обещается отсутствие shimmering при непрерывном вращении света или изменении lens. Нет hysteresis/temporal shadow filtering. У low-poly glTF sphere (особенно с baked nonuniform scale) остаётся geometric shadow terminator около grazing angles; Visibility debug может показывать мелкие irregular boundaries. Smooth normals не меняют shadow polygon silhouette. Большой normal bias для сокрытия этого дефекта не включён; нужна лучшая геометрия либо отдельное обоснованное terminator решение. User screenshots нужны для точного сопоставления оставшегося дефекта исходного синего объекта.

Masked/Transparent, skinning/morph shadow depth, point/spot shadows не добавлены. Shared Material Assets, overrides, slots, cast/receive, Double Sided, texture/tint и runtime edits сохранены. Debug frustum wireframe не добавлен; coverage overlay и depth/visibility previews дают диагностику без новой системы gizmos. UI controls не проверялись ручным click-through.

CSM не требуется для устранения **проверенных** ошибок bias/winding/focus. Для большого near-to-far диапазона с высокой плотностью texels CSM остаётся логичным следующим отдельным этапом. PBR, linear/sRGB pipeline и другие графические функции автоматически не начинаются.
