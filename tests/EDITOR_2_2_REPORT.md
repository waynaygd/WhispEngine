# Editor 2.2 — Visual Quality, Input Fixes & Usability (DX12)

14 октября 2026. Ветка `job_system`, HEAD `ae75fc16ab24aaac80aa9b0c670a3ba2da37597f`.
Commit, push, merge, PR и переключение веток не выполнялись. Lua runtime не начинался.
Vulkan не изменялся, не собирался и не запускался.

## 1. Исходные проблемы и сохранение работы

Изучены Editor 2.0, Editor Visual Polish, Shadow Quality, IBL и Light System reports,
фактические ECS/editor/scene/render/resource paths. Editor 2.1 уже находился в рабочем дереве.
До изменений сохранены 23 локальных файла в `out/diagnostics/editor22/before-tree.zip`,
git diff/status и SHA-256 tracked files. Исходные regression: **23/23 PASS, 85.44 s**.
В отдельной копии восстановлены исходные renderer/shaders/resources для before-измерений.

Подтверждённые причины:

| Наблюдение | Фактический путь | Исправление |
|---|---|---|
| Ступени/зернистость shadows | 3x3 point comparison PCF, дискретные уровни visibility | Continuous weighted tent PCF 5x5, отдельные качества/softness |
| Sky исчезает в Play | DX12 visual pass и HDR request явно возвращались при `play` | Authored Scene Background, shared Edit/Play, отдельный editor override |
| F стреляет и фокусирует | Fire action был F; дополнительно raw F напрямую вызывал Spawn в Application, обходя ImGui | Единственный diagnostic R consumer, default OFF, Edit-only F/W/E/R |
| Мелкий устаревший UI | Stock dark theme/default typography, малые padding | Graphite/accent theme, 16px bundled font base, согласованное DPI scaling |
| Inspector/Assets перегружены | Правые labels, открытые technical fields, файл-список без Create workflow | Registry presentation metadata, property tables/sections, cached content browser |

Native ввод пользователя здесь не автоматизирован. Воспроизведение сделано по коду,
реальным DX12 кадрам/readback и connected synthetic ImGui key events; это не mouse click-through.

## 2. Shadow Quality before/after

Одна stabilized directional orthographic map сохранена. Camera-fit, snapping, inverse-transpose
normals, mirrored PSO, geometry-derived receiver-plane correction, bias и edge fade сохранены.
Filtering выполняется в **main pixel shader**, а не в depth/shadow pass.

Новый режим привязан к центрам texels. Для каждого из 25 point comparisons вычисляется
собственный receiver-plane depth correction; separable tent weights непрерывно зависят от
fractional receiver UV. Радиус поддержки `filterRadius + .5` ограничен .5…2.5 texels.
При смене ближайшего texel крайние weights равны нулю, поэтому граница не получает
старую ступень фиксированных 1/9 visibility. Random/temporal sampling не используется.
Softness 0 сохраняет Hard-equivalence. Legacy PCF 3x3 оставлен для сравнения.

`ShadowConstants.coverage.w` ранее был reserved; теперь передаёт filter size. CB по-прежнему
112 bytes / 28 DWORD. Vertex/instance stride, normal matrices, batching keys не менялись.

| Preset | Resolution | Filter | Radius | Raw depth map / two frame slots |
|---|---:|---|---:|---:|
| Low | 512 | Hard | 1.5 | 1 / 2 MiB |
| Medium | 1024 | Legacy PCF 3x3 | 1.5 | 4 / 8 MiB |
| High | 2048 | Tent 5x5 | 1.5 | 16 / 32 MiB |
| Ultra | 2048 | Tent 5x5 | 2 | 16 / 32 MiB |
| Custom | Existing controls | Hard/3x3/5x5 | 0…2 | Depends on resolution |

4096 остаётся ручным выбором (64 MiB/map, 128 MiB/two slots), автоматически не включается.
Presets меняют только resolution/filter/softness; пользовательские bias/projection сохраняются.
UI показывает effective coverage, world units/texel и память одной карты. Например camera fit
84.082 units при 2048 даёт около .0411 world units/texel; широкий coverage всё ещё уменьшает density.

Контрольный readback при одинаковой geometry/camera: fractional edge pixels **85 → 113**,
число blue-channel уровней затенённого receiver **9 → 67**. Dark shadow core/location сохранены:
845 pixels, centroid X=190.876. Zero-bias inclined и mirrored plane: **0 affected pixels**.
Это доказательство сглаживания quantization, а не обещание устранить любой shadow terminator.
JSON: `out/diagnostics/editor22/shadow-edge-comparison.json`; исходные PPM сохранены отдельно.

Smooth normals/normal maps не увеличивают polygon silhouette low-poly mesh. Grazing-angle
terminator и предел одной map остаются; CSM/PCSS/TAA и искусственный большой normal bias не добавлялись.

## 3. Scene Skybox architecture

`SceneBackground` хранит Solid/Gradient/Cubemap, четыре linear colors, intensity, blend,
HDR asset, rotation и use-IBL flag. Единственный authored owner — Application SceneSettings.
Visible intensity независима от IBL intensity. При use-IBL asset/rotation разрешаются из
EnvironmentSettings; иначе используется собственный HDR key/yaw. Это один активный источник настроек.

Edit по умолчанию и Play показывают authored background. Опциональный Background Override
находится в editor preferences и применяется только в Edit. Grid/gizmos/selection helpers
скрываются в Play/Pause. Sky работает в viewport и в full-window rendering без editor viewport.

Используются прежние EnvironmentResource/EnvironmentLoader/ResourceManager и HDR decoder.
Source-radiance sky cube отделена от IBL cube/convolution: 64…512, power of two по panorama width/4.
IBL остаётся 64/16/prefilter/LUT. Source sky генерируется CPU worker при существующем preprocessing,
а не в render loop. Synthetic GPU assets без `sky` используют прежнюю environment cube fallback.

HDR sky активирует существующий RGBA32_FLOAT/output pipeline **даже при IBL OFF**. Освещение IBL
при этом не включается. Используются существующие exposure/Reinhard/output transfer controls;
новый tone-mapping algorithm или второй HDR loader не создавались. Gradient/Solid сохраняют
прежний SDR path при IBL OFF. Включение HDR sky поэтому может изменить общий output относительно
старого clipped SDR sky — это намеренное использование существующего color pipeline.

GPU upload проходит в текущем command list, descriptors копируются в frame-local table.
Old texture/SRV/staging живут в существующем retirement mechanism до fence. Intensity/yaw
не вызывают повторный upload или shader compilation. Missing/failed HDR имеет gradient fallback;
last ready matching-key cube сохраняется при неудачном reload.

Raw sky memory: 64 = .375 MiB, 128 = 1.5 MiB, 256 = 6 MiB, 512 = 24 MiB, плюс transient staging/retirement.
GPU oracle отдельно загрузил 256 cube и доказал, что берётся `sky`, а не low-resolution IBL cube.
Upload submit wall был 4.759 ms с debug layer, включая Present; это **не GPU timestamp**.
Synthetic CPU preprocessing 512x256 panorama / 128 sky / 8 IBL / 16 samples: около 188 ms,
включая first BRDF LUT initialization; это не изолированная стоимость sky generation или disk latency.
Полный asset wall latency/driver residency отдельно не инструментированы.

## 4. Input routing

Edit: F Focus, W/E/R Move/Rotate/Scale. Play: F не Focus и не Fire; R — diagnostic projectile
только при `Enable Projectile Test Input`, default OFF. Настройка доступна View → Navigation
and snapping и сохраняется в editor preferences, отсутствует в scene JSON.

Raw F обход удалён, duplicate GameplayState fire path удалён. Binding R остаётся в InputManager,
а rising edge потребляется один раз после current-frame UI. Gate учитывает Play/Pause,
viewport focus, WantCaptureKeyboard/WantTextInput, modal/tool activity и camera controls.
Blocked/held key потребляется до release: выход из text field или включение opt-in при удержании
не создаёт неожиданный shot. Projectile/physics implementation не менялись.

Viewport использует NoNavInputs, поскольку его клавиши принадлежат world/camera routing;
ImGui toolbar/Inspector/text/modal controls сохраняют keyboard navigation/capture.
Synthetic ImGui F достигает реального FocusEntity в Edit и блокируется при Inspector focus.
Physical R контракт проверен pure edge/gating tests, native R key press здесь не воспроизводился.

## 5. ImGui Theme

Graphite palette, сдержанный blue accent, selected/hover/active/disabled contrast,
единые rounding/borders/padding, tabs и tooltips. Используется bundled ImGui font,
base 16px; системные пути и внешние font assets не добавлены.
GLFW content scale обновляет style sizes и FontScaleDpi без накопительного масштабирования.
CPU regression проверяет 1x → 2x → 1x и согласованное font/padding scaling.
Native HiDPI monitor switching не проверялся; это пункт ручного checklist.

## 6. Inspector layout

ComponentRegistry/typed callbacks/serialization IDs сохранены. Presentation metadata
`section/advanced/internal` не попадает в scene data. Labels слева в fixed-width column,
values справа; цветные X/Y/Z, collapsible component headers и Reset/Remove context actions.
Persistent world/entity IDs скрыты из обычного Inspector. Readonly controls disabled,
optional overrides явно показывают inheritance. Setter validation выводится у изменённого field.
Compact XYZ используют 3 significant digits для отображения, NoRoundToFormat сохраняет точность;
в узких/DPI panels переключаются на stacked layout. Полное значение доступно в tooltip.
Grouped drags заканчивают существующую transaction при release и очищают property drafts.
Name/Mesh/Rigidbody/Collider headers collapsed по умолчанию; technical render diagnostics тоже collapsed.
HDR asset field сохраняет invalid typing draft отдельно, не записывая невалидный путь в scene.

Material: Surface, Base Color, Metallic/Roughness, Normal Map, AO, Rendering, Advanced.
Основные три секции открыты, остальные collapsed. Legacy shader and technical physics fields
перенесены в Advanced. Registry остаётся источником property access; второй Material System нет.

## 7. Asset Browser

AssetCatalog индексирует folders/typed entries только при startup/Refresh или успешном CRUD.
Directory list, breadcrumbs, search/type filter, List/Grid, name/type/status, context actions,
empty state, type icons и selected metadata preview. Preview запрашивает существующий async loader;
отдельный thumbnail renderer/GPU infrastructure не создан.
Cached reads не вызывают filesystem scans; тест проверяет 500 reads с неизменным revision.
Material/mesh/texture/HDR preview показывает существующие CPU resource data и Loading/Ready/Failed.

## 8. Material workflow и safety

Create Material → имя/директория `materials/*.json` → schema v4 PBR → Material Editor.
Base/Normal/MR/AO принимают typed texture DND. Save/Revert используют существующий atomic MaterialLoader.
Material DND назначает entity/slot через прежний AssignAsset/ComponentRegistry transaction.
Создание/rename/delete блокируются в Play и при несохранённых material edits.

Rename/Delete требуют explicit confirmation и разрешены только для session-created materials.
Referenced assets блокируются: Editor World, pending/Undo/Redo snapshots и saved project JSON
проверяются перед действием. Никакого автоматического переписывания references нет: сначала
reassign/clear history и save. Invalid/oversized JSON или symlink при reference audit блокируют действие.
Paths проходят portable-key, root/canonical/symlink/extension checks; overwrite existing asset запрещён.

File CRUD имеет отдельный дисковый домен и не находится в scene Undo. Assignment/overrides — в scene Undo.
Created asset ownership намеренно session-only: после restart существующие пользовательские assets
не становятся автоматически разрешёнными для удаления. Это ограничение текущего безопасного workflow.
Output root — существующий AssetPaths root (в этих запусках `out/build/shadows-dx12/assets`).
Новые файлы не продвигаются автоматически в source `engine/assets`; сохраняйте authored assets
перед удалением build tree. Пользовательские source assets не удалялись и не перезаписывались.

## 9. Lua/Prefab extension points

Browser AssetType registration содержит name/icon/folder/extensions/kind. Новый type можно
зарегистрировать до Refresh; layout не требует списка hardcoded UI buttons.
Regression регистрирует Future Script/.lua с non-assignable None kind. Это только проверка
extension point, не fake ScriptComponent. Будущие Lua/Prefab handlers и typed assignment
должны добавить реальные components/loaders; serialized properties используют ComponentRegistry.
Lua/sol2 runtime, bindings и Prefab systems здесь не реализованы.

## 10. Serialization

SceneSerializer остаётся v4. `settings.background.version=1` — optional versioned subsection.
v4 без background и legacy v1/v2/v3 получают default SceneBackground. Legacy compatibility
проверена через реальный LoadWorld, v4 — через restore/save domain. Invalid HDR path/version/color
rejects staged settings. Editor override, projectile opt-in и browser view не сохраняются в scene.

## 11. Undo/Redo и Runtime isolation

Background UI использует существующую SceneSettings transaction, dirty/savepoint/Undo/Redo.
Play сохраняет background вместе с authoring settings и клонирует Runtime World;
Stop восстанавливает background/settings/history/entity selection. Runtime sky changes не
переносятся в Editor. New resets scene defaults; stress Clear/Restart не создают новый background owner.
Assignment/Reset/components продолжают использовать прежние World-scoped references.

## 12. DX12 readback

Actual tests сохраняют depth/color images и проверяют numeric/pixel oracles:

- Hard/3x3/5x5, 512/1024/2048/4096, zero-bias inclined/mirrored plane, contact, low angle,
  camera fit/movement, negative/nonuniform scale, normal mapping и ordinary/instance equivalence.
- Grid occlusion геометрией выше Y=0, geometry below plane, horizon/below-plane camera.
- Gradient color oracle, cube face/yaw/translation oracle, linear HDR readback,
  high-resolution source vs IBL cube, independent HDR output and full-window Play sky.
- Actual async HDR → scene sky в Edit/Play/Pause/Stop, 500-cube batching.
- Material create/atomic-save/assign → changed viewport pixels; compatible 501 instances
  сохраняют 1 scene draw. Surface values уже находятся per-instance.
- Repeated sky replacement in flight, shader fallback, resize/shutdown.

DX12 debug counters: **0 ERROR/CORRUPTION**. Expected missing-resource/fault-injected shader
engine log messages не являются debug-layer errors. Tests используют exact preference-file
backup/restore, чтобы Application::Run не оставлял test settings пользователя.

## 13. Regression results

Release build PASS. Финальный полный набор: **25/25 PASS, 103.40 s**
(`out/diagnostics/editor22/ctest-final.log`). Исходные 23 tests сохранены; добавлены
EditorUsabilityRegression и EditorUsabilityDx12Regression. Прежние visual tests обновлены
под новый authored Play-background contract, grid/fallback/depth coverage не удалены.

Первый полный прогон выявил неверное ожидание test о split batch: surface parameters уже
per-instance, поэтому совместимый новый material сохраняет один draw. Assertion исправлен
после проверки фактического batch key; renderer ради теста не менялся.
Initial/provisional logs сохранены. 70 защищённых tracked files (Physics/Jobs/timestep/
interpolation/Vulkan/ResourceManager/MaterialLoader/SceneSerializer/old reports) byte-identical.
Все 23 исходных локальных файла сохранены: 6 byte-identical, 17 расширены, удалённых нет.

## 14. Performance

Одинаковый 744x411 viewport, Release, Tracy disconnected, debug layer OFF, 100 warmup +1200
samples/case. 14 cases × before/after: actual default scene, 100/500/1000, material-heavy,
четыре shadow presets, sky OFF/HDR ON, Inspector selected/collapsed, Browser List/Grid.
Selection применяется после scene creation. Первые ошибочные empty-Inspector серии сохранены
в provisional directories и не используются для финального сравнения.

Instancing OFF/ON анализируются отдельно; из каждого 300-frame block исключены 30 transition frames.
CSV содержит UI/Inspector/Browser CPU, gather/prepare/submit, shadow/main/frame GPU timestamps,
CPU frame, draws, coverage, raw depth memory, source preprocessing/upload diagnostics.
HDR sky ON включает HDR target/output; это шире стоимости одного sky triangle.
Driver clocks/power не фиксировались. Изменение CPU frame не приписывается pixel shader;
изменения shadow GPU при неизменном depth pipeline тоже не доказывают filtering cost.

Финальные 56 groups прошли sample/count/timestamp validation; все CPU/GPU p50/p95/p99:
`out/diagnostics/editor22/performance-summary.json`, полные p50 tables: `performance-tables.md`.
Browser cases используют content window 760x240, чтобы видимые rows/cards входили в измерение.

Реальные p50, ms, instancing ON:

| Case | UI before/after | Inspector before/after | Shadow GPU before/after | Main GPU before/after | Frame GPU before/after | CPU frame before/after |
|---|---:|---:|---:|---:|---:|---:|
| default | 0.197 / 0.095 | 0.058 / 0.041 | 0.025 / 0.025 | 0.088 / 0.119 | 0.181 / 0.199 | 3.341 / 1.695 |
| 100 | 0.235 / 0.124 | 0.062 / 0.049 | 0.054 / 0.042 | 0.090 / 0.103 | 0.225 / 0.223 | 3.951 / 2.389 |
| 500 | 0.246 / 0.171 | 0.050 / 0.055 | 0.112 / 0.081 | 0.205 / 0.169 | 0.591 / 0.424 | 7.569 / 6.025 |
| 1000 | 0.435 / 0.346 | 0.071 / 0.084 | 0.207 / 0.210 | 0.364 / 0.415 | 0.969 / 0.979 | 12.909 / 14.192 |
| materials | 0.118 / 0.097 | 0.029 / 0.028 | 0.026 / 0.028 | 0.057 / 0.080 | 0.133 / 0.161 | 1.706 / 2.492 |
| shadow-low | 0.320 / 0.277 | 0.068 / 0.085 | 0.020 / 0.006 | 0.167 / 0.041 | 0.386 / 0.074 | 7.596 / 8.103 |
| shadow-medium | 0.373 / 0.303 | 0.077 / 0.094 | 0.029 / 0.033 | 0.170 / 0.182 | 0.428 / 0.443 | 8.836 / 9.936 |
| shadow-high | 0.356 / 0.166 | 0.073 / 0.055 | 0.124 / 0.104 | 0.240 / 0.226 | 0.633 / 0.497 | 8.678 / 5.573 |
| shadow-ultra | 0.312 / 0.164 | 0.065 / 0.053 | 0.131 / 0.097 | 0.241 / 0.214 | 0.602 / 0.489 | 8.048 / 5.699 |
| sky-off | 0.332 / 0.218 | 0.070 / 0.071 | 0.057 / 0.172 | 0.093 / 0.370 | 0.354 / 0.790 | 7.670 / 8.214 |
| sky-on | 0.406 / 0.212 | 0.079 / 0.062 | 0.091 / 0.151 | 0.162 / 0.471 | 0.424 / 1.134 | 9.296 / 8.307 |

Default UI .197→.095 ms, Inspector .058→.041 ms: idle UI не ухудшился в этом run.
500/1000 сохраняют 1 draw при instancing ON; OFF даёт соответственно 501/1001.
500 HDR sky ON: GPU frame 1.134 ms против .790 sky OFF в after series; ON включает HDR output.
1000 CPU frame вырос 12.909→14.192 ms при UI .435→.346 ms; small render-submit/GPU/clock variance
не позволяет приписать этот рост UI или pixel shader. Scene gather/prepare/submit приведены в JSON.
Разные runs показывают заметные изменения даже неизменного depth GPU pipeline; no guaranteed FPS gain.

Selected/collapsed Inspector и Browser list/grid CPU p50:

| Case | UI before/after | Inspector before/after | Browser before/after |
|---|---:|---:|---:|
| 500 | 0.246 / 0.171 | 0.050 / 0.055 | 0.001 / 0.002 |
| inspector-collapsed | 0.299 / 0.152 | 0.002 / 0.002 | 0.002 / 0.002 |
| browser-list | 0.419 / 0.247 | 0.078 / 0.069 | 0.033 / 0.039 |
| browser-grid | 0.370 / 0.250 | 0.071 / 0.067 | 0.031 / 0.043 |

After 500 selected: exact p50/p95/p99, ms:

| Metric | p50 | p95 | p99 |
|---|---:|---:|---:|
| editorUiMs | 0.171 | 0.378 | 0.448 |
| inspectorMs | 0.055 | 0.112 | 0.170 |
| assetBrowserMs | 0.002 | 0.003 | 0.007 |
| shadowGpuMs | 0.081 | 0.118 | 0.188 |
| mainGpuMs | 0.169 | 0.221 | 0.753 |
| frameGpuMs | 0.424 | 2.233 | 3.401 |
| frameMs | 6.025 | 10.262 | 12.561 |

Native browser-grid до 2.2 отсутствовал: before/grid case является старым list с тем же layout.
Grid browser adds type/status/cards: его индивидуальная UI cost выше простого old list;
общий editor UI при этом не получил постоянного filesystem scan.
Tracy 0.14.1: `before-500.tracy` 2,151,732 bytes, `after-500.tracy` 1,818,096 bytes;
EditorLayer CSV exports сохранены рядом. Captures включают warmup и не используются как
steady-state percentile baseline; disconnected numerical CSV анализируется отдельно.
GPU metrics получены DX12 timestamps; Tracy содержит существующие CPU zones.

## 15. Native UX checklist и ограничения

Native mouse/keyboard click-through **не выполнялся**. Synthetic ImGui/API tests не заменяют его.
Короткая проверка пользователя:

1. Asset Browser → Create Material, выбрать `materials/user_test.json`; назначить texture maps, Save.
2. DND material на модель/slot, изменить metallic/roughness, Undo/Redo assignment.
3. Создать Point/Spot, изменить Transform; проверить основные/Advanced fields и Reset/Remove.
4. Scene Settings → Shadows: сравнить Hard/Medium/High/Ultra, contact/low-angle/slopes.
5. Scene Background → HDR, rotation/intensity/use-IBL; выключить IBL, убедиться, что sky остаётся HDR.
6. Play/Pause/Stop: sky остаётся, grid/gizmos скрыты; runtime background edit возвращается на Stop.
7. Edit: viewport focus → F, W/E/R; text/Inspector/modal focus блокируют world shortcuts.
8. Play: R OFF не стреляет; включить Projectile Test Input, R press = один shot, hold не повторяет; F не стреляет.
9. Save scene, reopen, проверить background/material/light settings и scene dirty.
10. Rename/Delete session-created unreferenced material через confirmation; references должны блокировать.
11. Проверить theme/labels/scroll и DPI scaling на реальном мониторе.

Сохраняются one-map/low-poly terminator ограничения, opaque surface support, limited 512 source sky,
64 IBL, отсутствие GPU thumbnails/full asset dependency graph/automatic reference migration.
Full asset wall latency и driver residency отдельно не измерены. No CSM/PCSS/TAA/Lua/Prefab runtime. Asset Browser при малой высоте требует resize/scroll;
для полного Inspector предусмотрен отдельный tall-window capture/checklist.

## 16. Changed files

Полный список изменений **относительно сохранённого Editor 2.1**, а не только HEAD:
`out/diagnostics/editor22/editor22-changed-files.json`.

- Application/input/gameplay routing; SceneSettings/Lighting equality/background.
- EditorLayer/Inspector/Assets/Preferences/UI/InputRouting/LightGizmos/VisualSettings.
- Новые AssetCatalog и EditorTheme.
- Shadows settings и textured.hlsl; DX12 visual/environment/UI integration.
- EnvironmentResource/EnvironmentLoader source cube; RenderDiagnosticSample.
- WhispEngine diagnostic CLI и CMake targets.
- Новые usability/prefs-restore tests, extended visual/shadow regression.
- Baseline/performance scripts и этот report.

## 17. Reproduction commands

Из repository root, VS x64 Native Tools shell. Paths с пробелами заключать в quotes:

```powershell
cmake --build out/build/shadows-dx12 --parallel 8
$env:WHISP_DX12_DEBUG='1'
ctest --test-dir out/build/shadows-dx12 --output-on-failure
Push-Location out/build/shadows-dx12
./EditorUsabilityRegression.exe --dx12
./EditorVisualPolishRegression.exe --dx12
./ShadowQualityRegression.exe --check ../../diagnostics/editor22/quality-images
./WhispEngine.exe --editor2-demo --editor-cubemap environments/studio.hdr --select-first --scene-background-panel
./WhispEngine.exe --shadow-demo --shadow-quality 2
Pop-Location
$env:WHISP_DX12_DEBUG='0'
python tests/PrepareEditor22Baseline.py
cmake -S out/diagnostics/editor22/baseline-source -B out/diagnostics/editor22/baseline-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
cmake --build out/diagnostics/editor22/baseline-build --parallel 8
Copy-Item out/diagnostics/editor22/baseline-build/EditorBaseline.exe out/diagnostics/editor22/baseline-runtime/EditorBaseline.exe
./tests/RunEditor22Diagnostics.ps1 -BuildDirectory out/diagnostics/editor22/baseline-runtime -Executable EditorBaseline.exe -Phase before
./tests/RunEditor22Diagnostics.ps1 -Phase after
python tests/SummarizeEditor22Diagnostics.py out/diagnostics/editor22
./tests/RunEditor22Tracy.ps1
$env:WHISP_DX12_DEBUG='1'
./tests/RunEditor22VisualCaptures.ps1
./tests/RunEditor22VisualCaptures.ps1 -Cases inspector,gradient
./tests/RunEditor22VisualCaptures.ps1 -BuildDirectory out/diagnostics/editor22/baseline-runtime -Executable EditorBaseline.exe -Phase before
git diff --check
```

Actual CMake/CTest used VS 2022 bundled binaries; build commands вызываются после VsDevCmd
с `chcp 65001` для Ninja/showIncludes в Cyrillic path. Runtime tests выполнялись вне tool sandbox,
не ослабляя MaterialLoader path/canonical checks. Cache: Release/DX12 ON/Vulkan OFF/Tracy ON.
Baseline source reconstruction требует сохранённого before-tree.zip и исходного HEAD;
baseline-runtime содержит исходные assets/shaders/DLL. Нельзя запускать before executable
с текущими shaders и называть такой запуск исходным rendering.

### Actual screenshots

До/после full editor, Inspector, Edit/Play sky и shadow fixtures сохраняются в
`out/diagnostics/editor22`. PNG являются conversion настоящего GPU readback, без перерисовки.
Visual QA выполнен: выбранные entities/controls читаются, границы panel не перекрываются,
Play sky виден, grid/gizmos отсутствуют. Before/after sky output отличается также из-за
намеренного HDR output activation. Play stress geometry движется физикой; pixel-identical
sky/world oracles проверяются отдельными GPU fixtures, не выдумываются по этим screenshots.


Before full editor:

![Before full editor](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor22/before-editor.png)

After full editor:

![After full editor](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor22/after-editor.png)

Before Inspector:

![Before Inspector](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor22/before-inspector.png)

After Inspector:

![After Inspector](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor22/after-inspector.png)

After procedural gradient:

![After procedural gradient](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor22/after-gradient.png)

Before Play (sky absent):

![Before Play (sky absent)](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor22/before-play-sky.png)

After Edit sky/grid:

![After Edit sky/grid](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor22/after-edit-sky.png)

After Play sky / no grid:

![After Play sky / no grid](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor22/after-play-sky.png)

Before shadow edge:

![Before shadow edge](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor22/before-caster-soft.png)

After shadow edge:

![After shadow edge](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor22/after-caster-soft.png)

After contact control:

![After contact control](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor22/after-contact.png)

Exact files changed relative to Editor 2.1:

- `CMakeLists.txt`
- `WhispEngine.cpp`
- `engine/CMakeLists.txt`
- `engine/core/Application.cpp`
- `engine/core/Application.h`
- `engine/core/RenderDiagnosticSample.h`
- `engine/ecs/ComponentRegistry.cpp`
- `engine/ecs/ComponentRegistry.h`
- `engine/editor/AssetCatalog.cpp`
- `engine/editor/AssetCatalog.h`
- `engine/editor/EditorAssets.cpp`
- `engine/editor/EditorInspector.cpp`
- `engine/editor/EditorLayer.cpp`
- `engine/editor/EditorLayer.h`
- `engine/editor/EditorPreferences.cpp`
- `engine/editor/EditorPreferences.h`
- `engine/editor/EditorTheme.h`
- `engine/editor/EditorUi.cpp`
- `engine/editor/EditorVisualSettings.cpp`
- `engine/editor/EditorWidgets.h`
- `engine/editor/InputRouting.h`
- `engine/editor/LightGizmos.cpp`
- `engine/game/states/GameplayState.cpp`
- `engine/game/states/GameplayState.h`
- `engine/render/EditorVisuals.h`
- `engine/render/Lighting.h`
- `engine/render/Shadows.h`
- `engine/render/backends/dx12/Dx12EditorVisuals.cpp`
- `engine/render/backends/dx12/Dx12Environment.cpp`
- `engine/render/backends/dx12/Dx12RenderAdapter.cpp`
- `engine/render/backends/dx12/Dx12RenderAdapter.h`
- `engine/resources/EnvironmentResource.h`
- `engine/resources/loaders/EnvironmentLoader.cpp`
- `engine/scene/SceneSettings.cpp`
- `engine/scene/SceneSettings.h`
- `engine/shaders/dx12/textured.hlsl`
- `tests/EDITOR_2_2_REPORT.md`
- `tests/Editor2Regression.cpp`
- `tests/EditorTestPreferences.h`
- `tests/EditorUsabilityRegression.cpp`
- `tests/EditorVisualPolishRegression.cpp`
- `tests/PrepareEditor22Baseline.py`
- `tests/RunEditor22Diagnostics.ps1`
- `tests/RunEditor22Tracy.ps1`
- `tests/RunEditor22VisualCaptures.ps1`
- `tests/ShadowQualityRegression.cpp`
- `tests/SummarizeEditor22Diagnostics.py`

Final audit: branch/HEAD unchanged, 70 protected files byte-identical, all original local files present.
Editor preferences bytes preserved; final audit restored an ImGui-only canonicalization change
(removal of redundant DockSpace Window token by older integration tests) from the captured backup.
