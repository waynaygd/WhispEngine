# Editor 2.3 — Scene Workflow, Asset Import & Interaction Polish (DX12)

Ветка `job_system`, исходный HEAD `3c54e9c653fb6d03f5d6181974df90992f0b137b`.
Перед работой дерево было чистым. Commit, push, merge, PR и переключение веток
не выполнялись. Vulkan, Job System, Physics, Fixed Timestep, PBR, Forward+ и IBL
алгоритмы не переписывались. Lua/sol2, ScriptComponent и prefab runtime не добавлялись.

## 1. Диагностика

Изучены Editor 2.0/2.1/2.2, Light System и IBL reports, реальные editor/ECS/resource
и DX12 paths. Обнаружены следующие причины и ограничения:

| Проблема | Причина / решение |
|---|---|
| Light volume clipping | Оба конца сегмента должны были пройти point projection. Заменено сегментным near/far и viewport clipping. |
| Copy/paste | Не было сериализуемого clipboard. Добавлен ComponentRegistry snapshot без SceneIdentity. |
| Alt+Drag | ImGuizmo сообщает изменение delta, которое не всегда означает движение новой операции. Проверяется фактическое изменение матрицы. |
| Hierarchy Delete | Shortcut был ограничен Viewport focus. Добавлен общий контракт Hierarchy/Viewport entity keys. |
| Sky / directional | Направление HDR sun неизвестно автоматически. Добавлены явные metadata и три режима синхронизации. |
| Handles против mesh picking | Mesh picking выполнялся раньше handles. Порядок исправлен, захват сохраняется до release/cancel. |
| Primitives | Не было общего Create → 3D Object. Добавлены воспроизводимые OBJ assets через существующий MeshLoader. |
| Scene Browser | Open был только полем пути. Добавлен индекс сцен, поиск, даты и Recent Scenes. |
| New Folder | Не было безопасного создания каталогов. Добавлена отдельная filesystem операция вне Scene Undo. |
| Import | Не было dependency-aware публикации. Добавлен bounded scan, staging, decoder validation и rollback. |
| Material focus | Открытие панели не гарантировало её активацию. Используются focus и раскрытие существующей вкладки. |
| Precision / deselect | Неочевидный text input, viewport-only shortcuts и старые UI flags. Добавлены подсказки, validation и очистка selection/drafts/capture. |

## 2. Принятые решения и сохранность

ECS, ComponentRegistry, SceneSerializer v4, ResourceManager и editor transaction
архитектура сохранены. Clipboard хранит JSON values, импорт использует существующие
декодеры, primitives являются обычными mesh assets. GPU vertex/instance layouts
остались прежними: 48 / 240 bytes.

Исходные status/HEAD и SHA-256 сохранены в `out/diagnostics/editor23`.
INI/preferences сохранены отдельно; тесты и диагностические runners восстанавливают
их в `finally`/RAII. Тестовые assets используют отдельные owned directories и удаляют
только свои файлы. Дополнительный архив runtime assets —
`runtime-assets-before-final.zip`. Финальная проверка `preservation-audit.json`: 314 исходных engine/tests файлов,
0 удалений; все прежние tests/reports byte-identical. Branch/HEAD прежние.
Оригинальные `imgui.ini` и `editor-preferences.json` в build восстановлены byte-for-byte;
owned fixtures и source primitive caches отсутствуют. Все 64 ранее сохранённых
non-cache runtime assets byte-identical; runtime snapshot, перезаписанный прежним
тестом, восстановлен из архива после всех запусков.
Изменены только перечисленные implementation files; Job/Physics/interpolation/
ResourceManager/ComponentRegistry/SceneSerializer и Vulkan исходники byte-identical.

## 3. Изменённые файлы

Полный фактический список приведён в конце отчёта. Основные группы:

- `EditorLayer`, `EditorUi`, `EditorInspector`, `EditorTransactions`, `EditorAssets`:
  input, selection, clipboard, duplication, dialogs, material focus.
- `LightGizmoMath`, `LightGizmos`, `InputRouting`: clipping, priority, capture, DPI.
- `AssetImport.*`, `EditorImportUi`, `AssetCatalog`, `AssetPaths`: import и поиск assets.
- `SceneCatalog`, `EditorPreferences`: portable scene list и recents.
- `SunSynchronization`, `SceneSettings`, `EditorVisuals`, DX12 editor visuals/HLSL:
  scene sun settings и visual disc.
- `PrecisionInputs`: text input, bounds, precision и Escape snapshots без ECS pointers.
- Primitive OBJ sources, default PBR material, regression и diagnostics runners.

## 4. UX before/after

Теперь пользователь может создавать primitives, копировать entities, дублировать
их движением gizmo, выбирать сцену из списка и импортировать ресурс с зависимостями.
Hierarchy/Viewport используют одну selection; пустая selection не заменяется первым
объектом. Scene Settings и Asset Browser остаются доступны.

GPU capture images и before/after measurements находятся в
`out/diagnostics/editor23`. Это реальные frame captures и программные input tests;
они не являются полноценным native mouse/keyboard walkthrough.

Проверены реальные PNG 1280×720: прежняя PBR/HDR scene before/after, primitives
с тенями и gradient/solar disc. Viewport crop (256,144)–(966,538) прежней сцены
имеет 62 изменённых пикселя из 279740 (0.022%), p99 difference=0;
различия локализованы в области editor gizmo (локальные координаты 257..291,
184..207), max=214. Scene geometry/material/sky visually preserved. Это UI frame
comparison; строгий ordinary/instanced pixel regression выполняется отдельно.

- [Editor 2.2 before](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor23/before-editor.png)
- [Editor 2.3 after](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor23/after-editor.png)
- [Primitives + gradient + Sun](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor23/after-primitives.png)

## 5. Duplication и clipboard

Ctrl+C захватывает зарегистрированные component values. Ctrl+V создаёт новую Entity,
восстанавливает компоненты и выдаёт новый SceneIdentity. Material references остаются
shared keys. Clipboard не содержит pointers/Entity handles и переживает смену сцены.
Copy/paste не изменяет Editor World в Play; text/modal/active control забирает клавиши.

Alt+Move создаёт копию при первом фактическом изменении матрицы. До этого Alt/click
не создаёт entity. Копия становится target существующего ImGuizmo drag. Последующие
движения не создают новых копий; snap/local/world используют прежний gizmo path.
Создание и движение — один transaction. Escape удаляет копию и возвращает оригинал;
Undo/Redo используют свежие generation handles и history remapping.

Существующие компоненты не содержат сериализуемых entity-to-entity references.
При их добавлении нужен явный ComponentRegistry remapping contract: внутренние ссылки
дубликата должны преобразовываться через logical scene ID mapping, внешние —
сохраняться только как validated logical references. Raw index/world handles копировать
нельзя; текущий clipboard не обещает копирование будущих entity graphs.

## 6. Selection и keyboard contract

Entity keys разрешены в Edit при focus Hierarchy/Viewport, без text/modal, активного
tool или camera control. Delete удаляет mesh/light/empty entities через transactional
deletion. Numeric/name/search inputs сохраняют свои стандартные клавиши.

Esc сначала отменяет input/transaction/gizmo, затем снимает selection. Пустой click
Viewport/Hierarchy и Edit → Clear Selection очищают selection/drafts. Выбор объекта
и его снятие сами по себе не добавляют Undo record. RMB camera и captured drag
блокируют случайную смену selection. Inspector показывает `No entity selected`.

## 7. Gizmo clipping, picking и capture

World segment переводится в camera space, отсекается по near/far и проецируется.
Liang–Barsky clipping ограничивает экранные координаты [0,1]. Параметры clipping и
projection используют double для больших радиусов и пересечений возле near plane.
Невидимые части не соединяются ошибочными endpoint coordinates. Selected light
volume может оставаться видимым, когда его центр за пределами viewport.

ImDrawList commands дополнительно ограничены прямоугольником viewport image.
Transform gizmo также получает этот clip rect. Handles масштабируются по DPI;
hover highlight соответствует hit rectangle. Offscreen handles не получают priority.

Порядок: UI/modal → уже captured tool → видимый light handle → transform gizmo →
light icon → mesh → empty-space deselect. ImGui active button плюс отдельное capture
состояние удерживают light drag; release вне viewport завершает transaction,
Escape/focus loss отменяет его. Play/Stop/New очищают состояния capture/ImGuizmo.
Прежний Xray/CPU icon occlusion contract сохранён.

## 8. Primitive generation

`tests/GenerateEditorPrimitives.py` генерирует deterministic unit-size Y-up assets:
Cube, Sphere (32×16), Cylinder (32), Cone (32), Plane. Stable keys:
`models/primitives/<type>.obj`. Они являются исходными project assets, не GPU handles.
Normals/UV0/winding заданы генератором; tangents генерирует существующий MeshLoader.
Pole degenerates удаляются. Bounds вычисляет прежний mesh/picking path.

Create добавляет Tag, Transform, SceneIdentity, MeshRenderer и Material; общий
`materials/default_primitive.material.json` — opaque PBR, metallic 0, roughness 0.6.
Rigidbody/Collider автоматически не добавляются. Physics components можно добавить
через Inspector. Imported model creation использует один complete entity transaction
и сохраняет imported material inheritance.

Save/Load сохраняет стабильный mesh key. Совместимые одинаковые primitives используют
прежний batching; разные meshes образуют разные batches. Нет отдельного primitive
renderer. Tessellation/geometry parameters фиксированы в reproducible source generator.

## 9. Scene Browser и Recent Scenes

File → Open Scene показывает portable project/staged scenes, filename, относительный
key, modification date, поиск, New Scene и Recent Scenes. Индекс обновляется при
открытии dialog, а не каждый frame. Recents хранят до 12 уникальных успешно
открытых/сохранённых keys в editor preferences.

Double-click открывает чистую сцену; при dirty scene только выбирает путь, оставляя
Save current / Discard and Open / Cancel guard. Recent menu также проходит dialog.
Отсутствующая recent scene даёт ошибку открытия; запись не считается гарантией
существования файла. Scene paths проходят traversal/absolute/reparse checks до I/O.

## 10. Asset Import и New Folder

ПКМ по папке/пустому месту дерева → New Folder. Имя должно быть одним безопасным
component; запрещены `..`, absolute/ADS paths, control characters, reserved Windows
device names, trailing dots/spaces и reparse/symlink escape. Existing folder не заменяется.
Empty folders и packaged model images индексируются на Refresh.

Import Asset → Browse/source path → target package/name → optional dependency root →
Scan dependencies → Import. Поддержаны OBJ, glTF, GLB v2, PNG/JPEG, DDS в поддерживаемом
TextureLoader подмножестве и Radiance HDR. Тип/итоговый resource key/файлы/размер
показаны в preview. Existing package защищён; пользователь выбирает другое имя.

glTF/GLB scan копирует external buffers/images и сохраняет embedded data. OBJ scan
копирует MTL и texture maps. Если модель ссылается на `../textures`, пользователь
явно выбирает общий Dependency root; scan не читает зависимости за его пределами.
Relative directory structure сохраняется. URI percent escapes декодируются до validation;
network/absolute URIs не импортируются. Unsupported format/dependency получает ошибку.

Limits: 256 MiB/file, 512 MiB/package, 2048 files, portable paths ≤240 bytes;
glTF JSON ≤16 MiB; HDR ≤64 MiB/16 MP; image header dimension cap 64 MP.
Owned staging files проходят существующие mesh/texture decoders. Invalid meshes,
unsupported DDS и malformed images не публикуются. Staging caches удаляются, чтобы
не сохранять material keys временного каталога. Publication — один directory rename.
На ошибке удаляются собственные staging files и созданные пустые parent directories.

В source checkout import/folders пишутся в `engine/assets`, в standalone distribution —
в asset root. AssetPaths сохраняет приоритет staging и fallback к authoring assets;
новый импорт доступен ResourceManager сразу и переживает rebuild. Catalog обновляется,
asset выбирается и начинает существующий async load. Loading/Ready/Failed видны;
assignment/DND и Create Model Entity используют существующие ECS/resource paths.

## 11. Material focus

Double-click Material вызывает открытие существующей панели, выбор key, focus и
раскрытие window/dock tab. Layout не перестраивается и второго окна нет. Unsaved
материал защищён: при попытке смены текущая панель выходит вперёд с Save/Revert
объяснением; данные не теряются. Floating layout сохраняется, если выбран пользователем.

## 12. Numeric precision

Drag/Slider используют стандартный ImGui text input, NoRoundToFormat, подсказки о
Ctrl+click/double-click и реальные limits. Float input сохраняет typed precision;
invalid/nonfinite/out-of-range values возвращаются к предыдущему valid value с
ошибкой, вместо неявного clamp. Escape snapshots хранят только values.

Entity/scene controls используют прежние grouped Undo transactions. Shared Material
Editor сохраняет свой отдельный Save/Revert domain. Input display format не меняет
stored values. UI tests вводят 1.23456789 при display `%.3g`, отклоняют 99 для [0,10]
и проверяют точное восстановление Escape.

## 13. Sky / Sun Sync

SceneBackground хранит optional Sun Sync mode, explicit metadata flag/direction,
visible disc diameter/intensity. Старые v4 scenes и v1–3 получают Independent.

- Independent: прежние ECS directional и sky controls.
- Light Drives Sky: visual disc следует opposite outgoing +Z ray vector primary ECS
  directional при любой elevation. Цвет санитизируется; HDR yaw и IBL не меняются.
- Sky Drives Light: требуется явный finite nonzero sun direction в unrotated sky
  space. Направление нормализуется, учитывается выбранный sky/IBL yaw, а ECS Euler
  orientation задаёт противоположное направление лучей. Rotation primary light
  readonly в Inspector, пока управление принадлежит sky.

Primary выбирается по тем же enabled/primary/index правилам, что lighting gather;
lookup выполняется по текущему World, без cached Entity handles. Replacement,
scene reload и clone поддержаны. Settings/связанные directional snapshots участвуют
в Undo; Play/Stop сохраняет прежнюю изоляцию.

Disc заменяет background radiance в своей области, не добавляет второй direct light
и не изменяет precomputed IBL. Default Independent пропускает disc math uniform branch.
DX12 editor root constants расширены с 44 до 52 DWORD (с SRV table cost 54 <64).
Новых GPU buffers/uploads/fences для disc нет. Gamma/tone mapping pipeline сохранён.

Нет HDR brightest-pixel detector, полной атмосферы или произвольного pitch rotation
HDR panorama. Light Drives Sky — явно visual disc; карта с собственным солнцем может
показывать два диска. Metadata задаёт пользователь и должен пересмотреть её при смене HDR.

## 14. Save/Load/Undo и runtime

SceneSerializer v4 не заменён новой схемой. Sun/background parameters optional внутри
settings. Clipboard/recents/folder/import state не попадают в scene components.
Scene Save/Open/New сохраняют staged validation и history reset; guarded Open не
обходит dirty scene. Filesystem import/New Folder не являются Scene Undo actions.

World replacement, Play/Stop и clear сбрасывают capture, ImGuizmo и drafts. Render
Interpolation и physics algorithms не менялись. Shared Material Asset остаётся shared
ресурсом; runtime не сохраняет authored scene/материалы через новые действия.

## 15. Regression results

`EditorWorkflowRegression.cpp` добавляет CPU и DX12 tests. Прежние tests не удалены
и не ослаблены. Проверяются clipping/huge segments/rotation/frustum, copy/paste
mesh/light/empty, identities, Undo/Redo generations, duplicate cancel, primitive
normals/tangents/bounds/indices, serializer roundtrip, recents, scene enumeration,
dirty guard, folder safety, OBJ+MTL+texture, external glTF, GLB, embedded images,
unsupported/invalid assets, rollback, source-root resolution, exact numeric input,
Sun metadata/replacement/isolation.

Release build PASS (`build-final.log`), **27/27 CTest PASS за 85.73 s**
(`ctest-final.log`). Пять последовательных CPU workflow повторов PASS за 4.22 s
(`import-repeat.log`), дополнительный CPU+DX12 targeted run PASS за 5.45 s
(`workflow-retry.log`). В промежуточном полном прогоне выявлен
временный Windows `rename: Access is denied` после успешного GLB decoder validation.
Добавлены подробная ошибка и bounded retry атомарной публикации; исходный failure
log сохранён как `ctest-publication-failure.log`. Это исправление подтверждается
повторным полным прогоном и пятью последовательными CPU workflow runs.

## 16. DX12 readback

Реальные app/GPU tests читают viewport pixels: primitives, visible solar disc,
shadow ON/OFF, обычный/instanced paths (максимальное отличие ≤2 byte levels), HDR sky,
Play helpers suppression, resize и mesh reload/retirement. Debug counter проверяется
на 0 ERROR/CORRUPTION.

Программные ImGui/GLFW events проверяют реальные Hierarchy Ctrl+C/V/Delete, light
handle capture/изменение range/release вне image, ImGuizmo Alt+Move/Undo/Redo/Escape
и foreground Material Editor. Native polling учитывается через GLFW cursor position;
это автоматизированная integration проверка, не ручной обход пользователя.

## 17. Performance

Baseline — отдельно собранный исходный HEAD Editor 2.2; собственные assets/shaders и
неизменённые dependencies. Measurements — Release, DX12 debug layer, viewport 744×411,
100 warmup frames +1200 CSV rows/case. Instancing переключается прежним diagnostic
сценарием; первые 30 frames каждого блока исключаются. Physics parameters не менялись.
Сняты 100/500/1000 objects, 100/250/500 local lights, gizmos OFF/ON, browser с 1000
owned test assets, primitives. Это стоимость Edit workflow, не runtime FPS benchmark.

`performance-summary.json` содержит CPU frame/UI/Hierarchy/Inspector/Browser,
Gather/Prepare/Submit, Main/Shadow/GPU Frame p50/p95/p99 и draw calls для обеих
instancing configurations. `performance-tables.md` — сокращённые таблицы. Финальные
значения приведены ниже после последнего measured run.

Final p50, milliseconds, instancing ON (38 ON/OFF groups validated):

| Case | Phase | Instancing | CPU frame ms | Editor UI ms | Hierarchy | Inspector | Assets | Main GPU | GPU frame | Draws |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| 100 | before | ON | 4.367 | 0.188 | 0.040 | 0.073 | 0.002 | 0.063 | 0.143 | [1] |
| 1000 | before | ON | 12.867 | 0.350 | 0.139 | 0.084 | 0.003 | 0.102 | 0.210 | [1] |
| 500 | before | ON | 8.806 | 0.271 | 0.080 | 0.085 | 0.003 | 0.176 | 0.361 | [1] |
| browser-grid | before | ON | 7.759 | 0.300 | 0.073 | 0.078 | 0.056 | 0.138 | 0.288 | [1] |
| browser-list | before | ON | 8.986 | 0.317 | 0.076 | 0.083 | 0.054 | 0.124 | 0.262 | [1] |
| lights100 | before | ON | 5.169 | 0.585 | 0.052 | 0.076 | 0.003 | 0.207 | 0.324 | [1] |
| lights250 | before | ON | 6.522 | 0.968 | 0.081 | 0.088 | 0.003 | 0.258 | 0.369 | [1] |
| lights500-off | before | ON | 6.329 | 0.302 | 0.121 | 0.093 | 0.003 | 0.288 | 0.400 | [1] |
| lights500 | before | ON | 7.108 | 1.597 | 0.101 | 0.082 | 0.004 | 0.298 | 0.428 | [1] |
| 100 | after | ON | 3.885 | 0.171 | 0.036 | 0.067 | 0.002 | 0.062 | 0.141 | [1] |
| 1000 | after | ON | 13.207 | 0.343 | 0.130 | 0.083 | 0.003 | 0.110 | 0.223 | [1] |
| 500 | after | ON | 7.804 | 0.238 | 0.070 | 0.077 | 0.002 | 0.092 | 0.190 | [1] |
| browser-grid | after | ON | 8.310 | 0.318 | 0.079 | 0.081 | 0.060 | 0.092 | 0.198 | [1] |
| browser-list | after | ON | 8.201 | 0.299 | 0.070 | 0.079 | 0.057 | 0.120 | 0.247 | [1] |
| lights100 | after | ON | 5.899 | 0.626 | 0.059 | 0.086 | 0.003 | 0.250 | 0.410 | [1] |
| lights250 | after | ON | 6.191 | 0.903 | 0.067 | 0.080 | 0.003 | 0.275 | 0.395 | [1] |
| lights500-off | after | ON | 5.395 | 0.243 | 0.088 | 0.078 | 0.002 | 0.370 | 0.525 | [1] |
| lights500 | after | ON | 7.303 | 1.616 | 0.101 | 0.086 | 0.004 | 0.413 | 0.567 | [1] |
| primitives | after | ON | 3.294 | 0.141 | 0.016 | 0.061 | 0.002 | 0.108 | 0.188 | [5] |

Gather/Prepare/Submit и instancing ON/OFF, p50 ms:

| Objects | Instancing | CPU frame | Gather | Prepare | Submit | Draw calls |
|---|---|---:|---:|---:|---:|---:|
| 100 + floor | OFF | 6.790 | 0.739 | 0.017 | 1.796 | 101 |
| 100 + floor | ON | 3.885 | 0.740 | 0.017 | 0.044 | 1 |
| 500 + floor | OFF | 22.732 | 3.852 | 0.081 | 9.585 | 501 |
| 500 + floor | ON | 7.804 | 3.774 | 0.081 | 0.049 | 1 |
| 1000 + floor | OFF | 42.716 | 8.015 | 0.161 | 19.617 | 1001 |
| 1000 + floor | ON | 13.207 | 7.984 | 0.162 | 0.052 | 1 |

500 cubes + floor: ON=1 draw, OFF=501 draws. Пять различных primitives используют
5 совместимых mesh batches. Нового per-instance payload/PSO не добавлено.
500 lights: UI p50 1.597 →1.616 ms; gizmos ON/OFF in after =1.616/0.243 ms
(включая hierarchy/inspector, это не только стоимость clipping). Browser 1000 assets:
list 0.054→0.057 ms, grid 0.056→0.060 ms. 1000 objects CPU frame 12.867→13.207 ms.

Не заявляется ускорение или точный процент regression по одному прогону. Тот же
неизменённый baseline на 500 objects ранее показывал CPU 5.197 ms, а финально
8.806 ms; первоначальная summary сохранена в `performance-initial-baseline-summary.json`.
Это подтверждает существенный environment/clock drift. В текущем сравнении batching
сохранён, цена UI находится на сопоставимом уровне; performance следует повторить
на стабильном power/clock profile для строгого бюджета. GPU main/frame timings
приведены отдельно и не используются для объяснения CPU fluctuations.


Источник GPU time — DX12 timestamps (`gpuValid=1`); CPU time не приписывается pixel
shader. Clocks/power не фиксировались; GPU и CPU differences включают system noise.
Пять CPU workflow повторов дали: 12000 clipped segments 0.908–1.473 ms;
10000 cached catalog reads 0.0074–0.0081 ms; Scene enumeration +10000 cached reads
2.318–5.632 ms; Scene Save (14 entities) 3.705–5.167 ms; Scene Load 1.888–2.769 ms;
external glTF scan/decoder validation/publication (741 bytes) 26.700–29.116 ms.
Это tiny fixture measurements, не throughput обещание для больших моделей.

Filesystem scan и import являются explicit operations, не per-frame tasks. CPU tests
отдельно выводят clipping, cached reads, scene Save/Load и import timings.

## 18. Ручной checklist

Native walkthrough в среде не выполнялся. Пользователю проверить:

- Point/Spot huge range, near-plane/camera rotations, resize и DPI 125/150/200%.
- Yellow handle поверх mesh, drag за viewport, release, Escape и focus loss.
- Ctrl+C/V в Hierarchy/Viewport и обычный copy/paste в Name/search/числовых полях.
- Alt+Move mesh/light в Local/World со snap; Undo/Redo и Play/Stop во время interaction.
- Delete light/empty/mesh; blank click, Esc и `No entity selected`.
- File Open/Recent double-click с dirty guard и отсутствующей сценой.
- New Folder/subfolder, import внешнего OBJ/MTL/glTF package и existing-name error.
- Material double-click из другой dock tab; unsaved material protection.
- Typed numeric values/Enter/Escape и Sun Sync с известной HDR metadata.

## 19. Команды и краткая инструкция

Команды из PowerShell в корне repo; зависимости уже подготовлены в существующем build:

```powershell
$cmake='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$python='C:\Users\Тимофей\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
& $python tests/GenerateEditorPrimitives.py
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && chcp 65001 > nul && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out/build/shadows-dx12 --parallel 8'
$env:WHISP_DX12_DEBUG='1'
& $ctest --test-dir out/build/shadows-dx12 --output-on-failure
Push-Location out/build/shadows-dx12
.\EditorWorkflowRegression.exe
.\EditorWorkflowRegression.exe --dx12
.\WhispEngine.exe --primitive-demo --editor-gradient --scene-background-panel
Pop-Location
& ./tests/RunEditor23Diagnostics.ps1 -Phase after -Cases @('100','500','1000','lights100','lights250','lights500','lights500-off','browser-list','browser-grid','primitives')
& $python tests/SummarizeEditor23Diagnostics.py out/diagnostics/editor23
& ./tests/RunEditor23VisualCaptures.ps1 -Phase after -Cases @('editor','primitives')
```

Baseline reproduction:

```powershell
& $python tests/PrepareEditor23Baseline.py
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && chcp 65001 > nul && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S out/diagnostics/editor23/baseline-source -B out/diagnostics/editor23/baseline-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out/diagnostics/editor23/baseline-build --parallel 8'
Copy-Item out/diagnostics/editor23/baseline-build/EditorBaseline.exe out/diagnostics/editor23/baseline-runtime/EditorBaseline.exe
& ./tests/RunEditor23Diagnostics.ps1 -BuildDirectory out/diagnostics/editor23/baseline-runtime -Executable EditorBaseline.exe -Phase before
```

Пользователю: выберите entity в Hierarchy/Viewport → Ctrl+C/V; Alt+Move переносит
копию; Esc отменяет drag или снимает selection. Hierarchy → Create → 3D Object создаёт
примитив. Asset Browser → Import Asset: выберите файл, target package и общий
Dependency root при sibling textures, Scan, Import, затем Create Model Entity/assignment.
ПКМ дерева → New Folder. File → Open/Recent управляет scenes с dirty guard.
Scene Settings → Scene Background → Sun Sync: Independent, visual disc из primary
light либо вручную подтверждённое sky-space направление. Для ручного вращения
primary directional переключитесь в Independent.

## 20. Ограничения

- Нет multi-selection/graph clipboard, parenting, Lua, ScriptComponent или prefab runtime.
- Primitive tessellation фиксирована; OBJ sources занимают disk/cache, используется общий renderer.
- Import поддерживает только проверенные formats. OBJ MTL texture options/сложные имена
  mtllib требуют предварительного упрощения и получают явную ошибку. За Dependency
  root выйти нельзя; выберите общий source directory для `../textures`.
- Import отвергает collision в authoring и staged roots. При временном Windows
  Access denied/sharing error публикация повторяется максимум 10 раз по 20 ms;
  остальные ошибки сразу возвращаются, существующий destination не заменяется.
- Import не перезаписывает packages и выполняет scan/copy/decoder validation синхронно
  по явной операции; большие bounded assets могут задержать UI. Последующий ResourceManager load async.
- Sky metadata ручная; при смене HDR нужно обновить её или отключить sync. Disc не
  является атмосферой/IBL sun и не поворачивает HDR по pitch.
- Gizmo lines сохраняют прежний overlay/Xray occlusion contract, не добавлен новый
  GPU depth-aware gizmo renderer.
- Native/DPI walkthrough остаётся отдельным checklist; automated events не заменяют его.


## 21. Полный список изменений

- `.gitignore`
- `CMakeLists.txt`
- `WhispEngine.cpp`
- `engine/CMakeLists.txt`
- `engine/assets/materials/default_primitive.material.json`
- `engine/assets/models/primitives/cone.obj`
- `engine/assets/models/primitives/cube.obj`
- `engine/assets/models/primitives/cylinder.obj`
- `engine/assets/models/primitives/plane.obj`
- `engine/assets/models/primitives/sphere.obj`
- `engine/core/Application.cpp`
- `engine/core/AssetPaths.cpp`
- `engine/editor/AssetCatalog.cpp`
- `engine/editor/AssetImport.cpp`
- `engine/editor/AssetImport.h`
- `engine/editor/EditorAssets.cpp`
- `engine/editor/EditorImportUi.cpp`
- `engine/editor/EditorInspector.cpp`
- `engine/editor/EditorLayer.cpp`
- `engine/editor/EditorLayer.h`
- `engine/editor/EditorPreferences.cpp`
- `engine/editor/EditorPreferences.h`
- `engine/editor/EditorTransactions.cpp`
- `engine/editor/EditorUi.cpp`
- `engine/editor/EditorVisualSettings.cpp`
- `engine/editor/InputRouting.h`
- `engine/editor/LightGizmoMath.h`
- `engine/editor/LightGizmos.cpp`
- `engine/editor/PrecisionInputs.h`
- `engine/editor/SceneCatalog.h`
- `engine/editor/SunSynchronization.h`
- `engine/render/EditorVisuals.h`
- `engine/render/backends/dx12/Dx12EditorVisuals.cpp`
- `engine/scene/SceneSettings.cpp`
- `engine/shaders/dx12/editor_visuals.hlsl`
- `tests/EDITOR_2_3_REPORT.md`
- `tests/EditorWorkflowRegression.cpp`
- `tests/GenerateEditorPrimitives.py`
- `tests/PrepareEditor23Baseline.py`
- `tests/RunEditor23Diagnostics.ps1`
- `tests/RunEditor23VisualCaptures.ps1`
- `tests/SummarizeEditor23Diagnostics.py`
