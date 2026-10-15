# Editor 2.0 Foundation / ECS Inspector / Runtime Isolation

Дата: 14 октября 2026. Ветка: `job_system`. Backend: DX12, Release, MSVC 19.44,
Tracy 0.14.1. Vulkan не собирался и не изменялся по условиям этого этапа.

## 1. Аудит прежней архитектуры

Исходный HEAD: `5240060eee628158378d8b5460ecd579f08cb017`.
До изменений `git status` был чистым. В `out/diagnostics/editor2/` сохранены
`before-status.txt`, `before-head.txt`, `before-tree.zip`, `before.patch` и
SHA-256 manifest 278 исходных файлов. Исходный build и 18/18 прежних CTest прошли.

Application владел одним World и pipeline систем. Play переключал PhysicsSystem
в этом же World; Stop оставлял изменённые Transform и созданные/удалённые entities.
Entity содержал только index/generation. EditorLayer имел специализированные
Inspector, Light и Material UI, но общий lifecycle/history зависел от выбранного объекта.
Hierarchy показывала Tag-entities, а отсутствие selection автоматически выбирало первую.
Inspector строил контролы даже для свёрнутого окна; gizmo делал снимок на idle frame.
Assets повторно сканировались по таймеру. Схема v3 использовала EcsDemoEntityConfig,
не сохраняла полноценный состав компонентов и Ambient/IBL/Shadow настройки.

ResourceManager, DX12 adapter и RenderSystem уже поддерживали асинхронные assets,
PBR maps, IBL, directional shadows, Forward+, GPU instancing и frames in flight.
Эти реализации сохранены. ECS data и shader vertex/instance payload не объединялись.

## 2. Структура редактора

EditorLayer остаётся владельцем небольшого набора UI/history state. Реализация разделена
по обязанностям: `EditorUi.cpp` (layout, меню, Hierarchy, dialogs, Console),
`EditorInspector.cpp`, `EditorTransactions.cpp`, `EditorAssets.cpp`,
`EditorPreferences.*`, существующий `LightGizmos.cpp` (viewport overlays/picking).
Специализированный Material Asset Editor сохранён. Персональные Material overrides
показываются в общем Inspector без прежнего второго набора повторяющихся полей.

## 3. Docking и layout

Stable dockspace ID, toolbar под главным меню. Default: Hierarchy слева, Viewport
в центре, Inspector и Scene Settings в отдельных правых областях, Assets/Console/
Material/diagnostics в нижней tab area. Размеры имеют минимумы, содержимое прокручивается.
ImGui сохраняет docking/layout в `imgui.ini`; View → Reset Layout восстанавливает default.
View также открывает/закрывает панели и Navigation/snapping preferences.
`--editor2-demo` явно восстанавливает новый layout для демонстрации.

## 4. Hierarchy

Показывает все живые entities, включая пустые и light-only, с типовыми [Mesh]/[D]/[P]/[S]
метками. Поиск, list clipper, selection, double-click Focus, Create, Rename, Duplicate,
Delete, context menu. Duplicate копирует зарегистрированные значения и выдаёт новый
scene ID. Entity drag payload содержит полный handle с World identity.
Hierarchy плоская: parenting/reparenting и родительские transforms не имитируются.

## 5. Viewport input

`InputRouting.h`: приоритет UI/text/modal → gizmo → RMB camera → selection → Runtime.
Нативная camera учитывает этот контракт, действительную hovered image, native focus
и LMB. Losing focus освобождает cursor capture. UI modal/text и camera блокируют
игровые bindings; MenuState больше не читает Enter напрямую в обход Application.
W/E/R/F/Delete относятся к сфокусированному Viewport, Ctrl+S/Z/Y/D — editor actions.
Runtime input разрешается только при владении viewport keyboard. Camera не запускается
через Inspector, popup, light handle или transform drag.

## 6. Gizmos и selection

Move/Rotate/Scale, Local/World, configurable snapping: position/world units,
rotation/degrees, scale increments. Focus использует cached mesh bounds, включая
центр смещённой импортированной геометрии; для lights учитывает range.
Light icons/range/cone handles сохранены. Click по mesh использует world ray,
обратный model transform, AABB и ближайшее triangle intersection; light icon получает
приоритет над геометрией. Геометрия и poses берутся из ResourceManager/interpolation.
Grid и выбранный bounding box рисуются только ImGui overlay. Line projection клипует
near/far и допускает сегменты, пересекающие границы viewport. Это не scene draw calls.
Highlight — overlay bounding box; outline/stencil pass не добавлялся.

## 7. Metadata registry

`ComponentRegistry` хранит стабильный FNV-1a 64-bit ID по schema key, label/tooltip,
Has/Add/Remove/Reset, typed property getters/setters и JSON callbacks. Offset arithmetic,
unchecked memory casts и сохранение component pointers между frames отсутствуют.
Safe `Get/Set` проверяют World identity, generation, alive, component, readonly/type/range.
Регистрация extensions выполняется на main thread при bootstrap до загрузки сцен/UI.
Builtins registry расширяем; duplicate ID/key отвергается. Test-only RegressionExtension
подтверждает регистрацию произвольного typed component и его JSON/property hooks.

Зарегистрированы 12 типов: Tag, SceneIdentity, Transform, MeshRenderer, Material,
Rigidbody, Collider, Velocity, BoundsBounce, DirectionalLight, PointLight, SpotLight.
Transform автоматически добавляется для зависимых render/light/physics компонентов;
его удаление блокируется при наличии зависимостей. Допускается один light type/entity.
NaN/Inf и несовместимые PropertyValue отклоняются; численные диапазоны и lights валидируются.

## 8. Inspector

Группы компонентов, Add popup, Reset/Remove context menu, typed bool/enum/int/float/
Vec3/color/string/asset/list controls, tooltips, readonly ID/sleep state, nullable
Material overrides с inherit. Light цвета отображаются через существующие linear↔sRGB
UI helpers; tint сохраняет существующую семантику. Показаны ResourceManager Loading/
Failed статусы. Draft существует только для активного поля; изменение не сохраняет
ссылку на компонент. Component JSON snapshot создаётся на границах операции.
Ошибки property validation показываются в Inspector и Console без повторного log spam.

## 9. Scene Settings

Один Scene Settings UI: Ambient, legacy directional fallback, Environment/IBL asset,
intensity/rotation/exposure/tone mapping/diffuse/specular, Shadow quality/coverage/bias/
filtering/strength. HDR drag-and-drop поддерживается. Primary directional выбирается
через combo либо scoped entity payload, меняя ECS primary flags.
В JSON primary сохраняется как logical scene ID; canonical выбор по ECS flags/order
проверяется при чтении. Отдельной competing primary настройки не введено.
Lighting OFF, Forward/Tiled switch, shadow debug и editor overlays — диагностический
домен; они не становятся authored scene settings. Global PhysicsSystem параметры
не добавлены в scene schema: прежняя сериализация не считала их настройками сцены.

## 10. Play → Runtime World → Stop

Play завершает текущую transaction и вызывает `World::CloneData`, создавая новый World
со своими slots/component values, identity и Editor→Runtime entity map. Systems не
копируются: Application-owned pipeline обновляет active World. Tracked entity vectors
и selection переводятся через map; authoring Undo/dirty state сохраняются отдельно.
Lighting/IBL/Shadows runtime settings имеют восстановление исходных значений на Stop.
ResourceManager и renderer GPU caches переиспользуются; meshes/textures не клонируются.

Edit Mode не выполняет GameUpdate/fixed simulation. Pause останавливает simulation
и сбрасывает accumulator, оставляя Runtime World и rendering живыми. Resume продолжает
эти же данные. На каждый Play создаётся свежий существующий GameState lifecycle.
Stop вызывает GameState OnExit пока Runtime ещё жив, очищает physics caches, queued
commands, interpolation и collision pairs, уничтожает Runtime World, затем возвращает
Editor selection/history/settings. Никакого переноса runtime Transform обратно нет.

Системы физики, Job System, fixed timestep и interpolation algorithm не переписывались.
Минимальные common interfaces позволяют существующему pipeline работать с target World.

## 11. Entity references

Entity теперь `{index, generation, world}` (16 bytes), equality/hash включают identity.
IsAlive/GetComponent/HasComponent отклоняют чужие handles даже при одинаковом index/gen.
Runtime handle после Stop не может попасть в Editor entity. Scene load меняет identity
через ReplaceData и инвалидирует старые references. Clear сохраняет безопасные generations.
SceneIdentity — стабильный serialized logical ID; он не равен runtime handle.
Duplicate создаёт новый logical ID, clone сохраняет его для того же authored actor.
Future entity-valued components потребуют явного reference remap policy по CloneMap;
нынешние компоненты не содержат таких component-to-component handles.

## 12. Safe structural mutations

World проверяет owner thread и запрещает Create/Destroy/Add/Remove/Clear внутри ForEach.
Scalar updates в iteration допускаются. Clone/queue Apply требуют owner thread; ошибочный worker Apply не извлекает commands. Self ReplaceData отвергается. MutationQueue принимает value-owned typed
Add/Remove/Destroy и команды initializer из producer threads. Apply выполняется на main
thread после завершения fixed batch/jobs и перед frame gather; чужие/stale world commands
отбрасываются. Stop/New/Open очищают queue. UI authoring операции выполняются до gather,
вне ECS iteration. Future bindings должны использовать Registry + queue и не захватывать
component pointers в callbacks. Перенос flush на отдельную fixed phase возможен при
согласовании порядка future script/physics lifecycle, без изменения timestep.

## 13. Undo/Redo

Снимок содержит component JSON выбранной entity, World identity и alive state.
Scene Settings transaction дополнительно содержит связанные Directional actors.
Drag: Begin один раз → промежуточные scalar changes → Commit один раз. No-op не создаёт
record. Escape/focus loss вызывает Cancel с восстановлением начального snapshot.
Create/Delete/Duplicate/Rename/Add/Remove/Reset/asset assignment также транзакционны.
Undo deletion создаёт новую generation и remap оставшейся истории; старые handles остаются
невалидными. Redo invalidation, bounded 128 records, revision/savepoint dirty tracking.
Play имеет отдельную runtime историю, которая уничтожается на Stop; editor история возвращается.
Material asset disk Save/Revert остаются отдельным доменом, scene Undo их не подменяет.

## 14. Scene serialization

Тот же SceneSerializer, новая schema v4: entities[id, components] + settings.
Сохраняются пустые entities, точный component set, metadata values, Material references/
overrides/slots, lights, Ambient/IBL/Shadows/primary. v1–v3 продолжают читаться через
существующий EcsDemoEntityConfig adapter с default scene settings; v4 adapter для старых
startup consumers сохранён. Unknown component требует зарегистрированного descriptor
и вызывает понятную ошибку, а не тихую потерю данных.

Load полностью парсит/валидирует staged World до изменения текущей сцены. Invalid JSON,
IDs, numbers, paths или component dependencies оставляют World и mode неизменными.
Application Save/Save As/Open/New используют portable `scenes/*.json` keys; absolute,
drive, parent traversal и backslash keys отвергаются. Missing относительный asset
сохраняется как ссылка и получает обычный async fallback. Atomic write использует unique
temporary sibling, flush/close и Windows MoveFileExW WRITE_THROUGH; ошибка не повреждает
предыдущий файл. Save As требует явного overwrite, Save работает с текущим ключом.
Save в Play запрещён; New/Open имеют явный dirty-discard dialog. New/load инвалидируют history.

## 15. Preferences

`editor-preferences.json` schema v1 хранит panel visibility, grid/light gizmos/X-ray,
operation/space/snapping, camera navigation. `imgui.ini` хранит docking геометрию.
Они отделены от scene JSON и сохраняются атомарно на shutdown/по UI command.
Default preferences не создаются headless CPU fixtures без initialized editor window.
Procedural Create/Restart/Clear сбрасывают несовместимую entity history и отмечают
изменение active scene; в Play это runtime dirty domain. New создаёт чистую Untitled scene.

## 16. Asset Browser

Project directory selection, search/type filter, Mesh/Texture/Material/Shader/HDR,
typed drag payload, double-click/context assignment. Payload — relative ResourceManager
key, без копирования GPU assets. File scan выполняется при инициализации либо Refresh,
а не каждый frame. Asset lists кешируются; visible rows используют clipper.
Large geometry/textures/environment загружаются существующим async механизмом renderer.
Material Editor не переключается на другой asset при unsaved edits без Save/Revert;
его mutable asset editing заблокировано в Play. Editor не переписывает ResourceManager.

## 17. Console / Diagnostics

Logger предоставляет thread-safe bounded 1024 messages с sequence/revision. Console
имеет severity/search/Clear; snapshot копируется при изменении revision. Видны resource,
shader, load, validation errors. Diagnostics показывают CPU/GPU frame, UI/Hierarchy/
Inspector, clone, scene/instanced draw counts, gather, lights/culling и resource state.
Editor overlays не смешиваются с scene draw statistics.

## 18. Script-ready extension points

Никакой scripting technology, ScriptComponent или фиктивный ScriptRuntime не добавлен.
Будущий component регистрирует stable schema key, typed value getters/setters, Add/Remove/
Reset и serialize/deserialize hooks; Inspector и v4 serializer используют этот registry.
Safe bindings должны принимать scoped Entity, проверять World/Generation и применять
structural commands на main-thread phase. Ошибки направляются в Logger/Console.

Runtime instances нельзя value-copy из Editor. Перед лабораторной №2 потребуется
конкретный clone policy: serialized Script asset/properties копируются, mutable VM/instances
создаются заново. OnCreate/OnStart относятся к Runtime; OnUpdate/OnFixedUpdate — только
simulation phases. OnDestroy нужно вызвать до освобождения World, пока handles валидны;
Stop уже имеет явную фазу завершения существующего GameState перед runtime reset.
Будущий script system может использовать эту границу, затем вычищать callbacks/queue.
Non-copyable storage сейчас явно отвергает CloneData вместо частичного aliasing.

Без ТЗ лабораторной не выбраны язык/VM, lifetime ownership, entity reference serialization,
reload/migration policy, exception isolation, exact script-vs-physics update order и resource
permissions. Script hot reload не реализован; asset hot reload остаётся существующим.

## 19. Automated regressions

Итог: **20/20 CTest PASS, итоговый прогон 58.82 s**, Release build PASS (`build-final-guards.log`); DX12 integration assertion подтверждает **0 ERROR/CORRUPTION**. Логи `ctest-final.log` и `build-final.log`; последующие UX checks 4/4 PASS (`ctest-final-ux.log`). Итоговые owner-thread guards подтверждены полным 20/20 прогоном в `ctest-final-guards.log` / `build-final-guards.log`.

`Editor2Regression.cpp`: registry coverage/extension/duplicate ID, typed reads/writes,
foreign handle rejection, readonly/range/NaN checks, dependencies, iterator mutation
guard, worker queue, clone map/value isolation, Create/Duplicate/Rename/Delete/remapped
Undo/Redo, Reset/assignment Undo, 100-change drag coalescing/cancel/savepoint, Scene Settings
Undo, preferences, Console, atomic v4 reopen equivalence, invalid transactional load,
overwrite/path rejection, New/history invalidation, input ownership and clipped grid math.

CRITICAL CPU regression реально обновляет PhysicsSystem в Runtime, меняет Transform/light,
создаёт Spot entity, удаляет authored entity и меняет IBL exposure. После Stop все authored
serializable components/settings совпадают с исходным документом; selection и references
проверяются отдельно. Есть 20 Play/Pause/Resume/Stop циклов, не только пустые переключения.

PhysicsRegression cross-world event comparison адаптирован к новому Entity contract:
проверяется принадлежность event текущему World, а deterministic sequence сравнивается
по local index/generation в независимых Worlds. Tick/state/collision assertions сохранены.
Прежние 18 suites не удалены и не ослаблены.

## 20. DX12 integration

`Editor2Regression --dx12` создаёт настоящие Application/window/adapter/UI. Проверяет
GPU viewport readback с непустыми цветами, 501 instances / batching 500 кубов, mesh reload
с shared handle/version и render после него, missing asset failure/fallback, owned physics
simulation в Runtime, Delete/Undo generation, Pause с продолжающимся rendering, Stop с
точным authoring document comparison, Save As/overwrite, 3 GPU Play/reload/resize цикла,
invalidated references, New пустого World, уничтожение сцены после command recording
до EndFrame/submit и shutdown. DX12 Debug Layer ERROR/CORRUPTION count должен быть 0.
Существующие DX12 suites отдельно подтверждают PBR/IBL/shadows/Forward+/instancing и GPU lifetime.

## 21. Ручной UX status

Настоящий mouse/keyboard walkthrough не выполнялся: доступный automation surface
поддерживает браузеры, native apps control отключён. Это ограничение среды.
Реальные DX12 кадры сохранены и визуально проверены, а CPU/GPU tests выполняют state/API
сценарии. Они не выдаются за ручное перетаскивание gizmo, click picking, asset DND,
интерактивный docking или keyboard navigation. Эти UX действия следует пройти пользователю
в `--editor2-demo`; screenshots показывают geometry/material/light/UI result, не факт input testing.

## 22. Производительность

Первичные unchanged baseline CSV сохранены как `before-*.csv`. Для сопоставимых UI
таймеров `PrepareEditor2Baseline.py --ref <initial HEAD>` материализует отдельный source
snapshot в `out/diagnostics/editor2/baseline-source`, не checkout/branch/reset. Собраны
исходные Engine/Application/World с теми же unchanged dependency libraries; только chrono
UI/panel timers и CSV fields добавлены. CPU fixture использует empty-path v3 JSON capture
без записи файла. Rendering/physics baseline behavior не заменялся.

Парные результаты: `before-ui-*.csv` и `after-*.csv`. Обе версии используют одну сохранённую
INI с открытым Inspector и выбранной первой entity. `--fixed-editor-layout` сохраняет
старую floating геометрию и высоту canvas; **все samples имеют viewport 744×411**.
Default новый dock layout проверялся отдельно. Diagnostic harness восстанавливает исходные INI/preferences в finally; benchmark panel visibility не становится пользовательской настройкой. Неиспользованные пробные collapsed/unselected
datasets оставлены как диагностические артефакты и не участвуют в таблице.

100 warmup frames, 1200 samples для cubes/lights, 1800 для materials; варианты собираются
в блоках по 300. Cubes count включает дополнительно floor; lights — 500 local lights и
100 mesh объектов плюс floor; materials — 50 копий OBJ и glTF (200 submesh draws или 4 batches).
CPU frame percentiles — linear quantile interpolation, остальные поля — arithmetic mean.
GPU данные используются только при gpuValid=1 (в этих парных datasets все valid).
Физические параметры и shader/backend sources для улучшения метрик не изменялись.

| Сцена / режим | CPU p50/p95/p99 до → после, ms | UI до → после | Hierarchy до → после | Inspector до → после | Gather до → после | GPU frame до → после | Scene draws |
|---|---|---|---|---|---|---|---|
| entities-100, instancing=0 | 2.469/4.693/5.442 → 2.603/5.526/6.678 | 0.306 → 0.386 | 0.025 → 0.034 | 0.043 → 0.056 | 0.542 → 0.564 | 0.084 → 0.101 | 101.000 → 101.000 |
| entities-100, instancing=1 | 2.626/4.556/5.419 → 3.523/5.025/5.803 | 0.278 → 0.392 | 0.025 → 0.033 | 0.040 → 0.055 | 0.551 → 0.587 | 0.088 → 0.119 | 1.000 → 1.000 |
| entities-500, instancing=0 | 8.473/10.359/11.686 → 8.164/9.860/10.951 | 0.858 → 0.536 | 0.063 → 0.067 | 0.047 → 0.065 | 3.020 → 3.142 | 0.155 → 0.161 | 501.000 → 501.000 |
| entities-500, instancing=1 | 8.050/9.515/10.989 → 7.355/9.811/12.714 | 0.950 → 0.541 | 0.070 → 0.067 | 0.053 → 0.065 | 3.520 → 3.277 | 0.178 → 0.244 | 1.000 → 1.000 |
| entities-1000, instancing=0 | 14.519/16.904/22.647 → 13.243/14.987/17.033 | 1.689 → 0.656 | 0.108 → 0.105 | 0.051 → 0.066 | 7.043 → 6.411 | 0.224 → 0.214 | 1001.000 → 1001.000 |
| entities-1000, instancing=1 | 13.381/15.976/20.757 → 12.148/14.618/16.782 | 1.771 → 0.688 | 0.112 → 0.114 | 0.055 → 0.068 | 7.546 → 6.869 | 0.297 → 0.367 | 1.000 → 1.000 |
| lights, instancing=1,tiled=0 | 3.717/6.155/6.866 → 4.411/6.987/7.638 | 0.245 → 0.510 | 0.069 → 0.082 | 0.046 → 0.064 | 0.617 → 0.665 | 1.064 → 1.168 | 1.000 → 1.000 |
| lights, instancing=1,tiled=1 | 4.402/7.005/8.145 → 4.834/5.946/6.879 | 0.255 → 0.615 | 0.071 → 0.104 | 0.049 → 0.076 | 0.702 → 0.832 | 0.190 → 0.212 | 1.000 → 1.000 |
| materials, instancing=0,shadows=0,pcf=0 | 2.109/3.011/3.673 → 2.549/3.226/3.780 | 0.198 → 0.476 | 0.021 → 0.034 | 0.024 → 0.030 | 0.542 → 0.551 | 0.065 → 0.077 | 200.000 → 200.000 |
| materials, instancing=1,shadows=0,pcf=0 | 2.643/4.844/5.600 → 4.543/5.784/6.934 | 0.267 → 0.711 | 0.030 → 0.055 | 0.032 → 0.048 | 0.765 → 0.941 | 0.066 → 0.082 | 4.000 → 4.000 |
| materials, instancing=0,shadows=1,pcf=0 | 4.661/6.205/9.916 → 5.564/6.810/7.786 | 0.370 → 0.874 | 0.040 → 0.075 | 0.041 → 0.061 | 1.101 → 1.158 | 0.113 → 0.136 | 200.000 → 200.000 |
| materials, instancing=1,shadows=1,pcf=0 | 4.405/5.979/8.983 → 5.270/6.671/8.312 | 0.401 → 0.877 | 0.040 → 0.073 | 0.043 → 0.062 | 1.100 → 1.189 | 0.127 → 0.152 | 4.000 → 4.000 |
| materials, instancing=0,shadows=1,pcf=1 | 4.893/6.947/8.298 → 5.674/7.272/7.662 | 0.399 → 0.848 | 0.041 → 0.069 | 0.041 → 0.055 | 1.094 → 1.135 | 0.176 → 0.192 | 200.000 → 200.000 |
| materials, instancing=1,shadows=1,pcf=1 | 4.110/5.670/6.445 → 5.357/7.364/8.314 | 0.306 → 0.857 | 0.031 → 0.069 | 0.038 → 0.056 | 0.935 → 1.152 | 0.188 → 0.188 | 4.000 → 4.000 |

| Entities | v3 → v4 serialization CPU | Старый переключатель Play → настоящий clone | Stop до → после | Gesture: 100 старых Undo records → 1 новая transaction |
|---|---|---|---|---|
| 100 | 0.589 → 1.287 | 0.015 → 0.080 | 0.002 → 0.024 | 0.272 → 0.068 |
| 500 | 2.567 → 5.243 | 0.023 → 0.264 | 0.003 → 0.083 | 0.277 → 0.058 |
| 1000 | 4.701 → 10.066 | 0.023 → 0.565 | 0.003 → 0.224 | 0.262 → 0.063 |


На 1000 кубах UI mean instancing ON изменился с 1.771 до 0.688 ms; batching остался 1 draw.
Для materials-heavy сцены новая функциональность увеличила UI (PCF/instancing ON:
0.306 → 0.857 ms). Это наблюдаемая стоимость дополнительных panels/metadata/overlays,
а не заявление об универсальном ускорении. CPU tails зависят от scheduling/частоты CPU.
GPU frame включает UI/output/shadow/main; изменения CPU не приписываются pixel shaders.
RenderPrepare/Submit, main GPU, instanced draws и все percentiles также сохранены в
`performance-summary.json`, не скрыты за агрегированной таблицей.

Сериализация сравнивает прежний неполный v3 DTO с v4 metadata/settings и полной валидацией;
это разный объём/контракт данных. v4 1000 entities capture mean 10.066 ms выполняется при
Save/load/test, не при idle UI или каждом mouse movement. CPU fixture выполняет 25 циклов
на каждый размер, Stop действительно уничтожает runtime. `cloneMs` baseline — время
переключателя без клонирования; после — полноценное копирование, включая editor bookkeeping.
Gesture baseline создаёт 100 отдельных records, после — 1 transaction и 100 validated updates.

Реальные Tracy captures: `before-editor.tracy` / `after-editor.tracy`, zones CSV рядом.
Captured EditorLayer mean: 0.938 → 0.595 ms; RenderGather: 3.251 → 3.346 ms.
Capture длительностью 8 s содержит соответственно 882/903 UI zones и не используется как
подмена парных unprofiled percentiles. Source locations позволяют проверить реальные phases.

## 23. Изменённые файлы

- `CMakeLists.txt`
- `WhispEngine.cpp`
- `engine/CMakeLists.txt`
- `engine/core/Application.cpp`
- `engine/core/Application.h`
- `engine/core/Logger.cpp`
- `engine/core/Logger.h`
- `engine/core/RenderDiagnosticSample.h`
- `engine/ecs/ComponentRegistry.cpp`
- `engine/ecs/ComponentRegistry.h`
- `engine/ecs/Entity.h`
- `engine/ecs/MutationQueue.h`
- `engine/ecs/World.cpp`
- `engine/ecs/World.h`
- `engine/ecs/components/SceneIdentityComponent.h`
- `engine/editor/EditorAssets.cpp`
- `engine/editor/EditorInspector.cpp`
- `engine/editor/EditorLayer.cpp`
- `engine/editor/EditorLayer.h`
- `engine/editor/EditorPreferences.cpp`
- `engine/editor/EditorPreferences.h`
- `engine/editor/EditorTransactions.cpp`
- `engine/editor/EditorUi.cpp`
- `engine/editor/EditorWidgets.h`
- `engine/editor/InputRouting.h`
- `engine/editor/LightGizmoMath.h`
- `engine/editor/LightGizmos.cpp`
- `engine/game/StateMachine.cpp`
- `engine/game/StateMachine.h`
- `engine/game/states/MenuState.cpp`
- `engine/scene/SceneSerializer.cpp`
- `engine/scene/SceneSerializer.h`
- `engine/scene/SceneSettings.cpp`
- `engine/scene/SceneSettings.h`
- `tests/EDITOR_2_REPORT.md`
- `tests/Editor2Regression.cpp`
- `tests/PhysicsRegression.cpp`
- `tests/PrepareEditor2Baseline.py`
- `tests/RunEditor2Diagnostics.ps1`
- `tests/SummarizeEditor2Diagnostics.py`

Preservation audit: `preservation-audit.json`. Прежние reports, shader sources, DX12
backend, Vulkan, ResourceManager/loaders, Job System/enkiTS, WhispPhysics algorithms,
fixed clock и RenderInterpolation implementation совпадают с исходным SHA-256 manifest.
Entity/World/Application/common diagnostics interfaces расширены для isolation и measurement.
Без commit/push/merge/PR, main не изменялся.

## 24. Воспроизведение

PowerShell из корня repo, VS2022 Developer Command Prompt environment, Ninja; build
остаётся DX12-only. В этой машине используется VS bundled cmake/ctest.

```powershell
$cmake = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctest = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
$python = 'C:/Users/Тимофей/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $cmake -S . -B out/build/shadows-dx12 -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_DX12=ON -DENABLE_VULKAN=OFF -DENABLE_TRACY=ON -DWHISP_BUILD_PHYSICS_TESTS=ON
& $cmake --build out/build/shadows-dx12 --parallel 8
$env:WHISP_DX12_DEBUG='1'
& $ctest --test-dir out/build/shadows-dx12 --output-on-failure
Push-Location out/build/shadows-dx12
./Editor2Regression.exe
./Editor2Regression.exe --dx12
./WhispEngine.exe --editor2-demo --frames 180 --capture-frame ../../diagnostics/editor2/editor2-demo.ppm
./WhispEngine.exe --editor2-demo  # обычный интерактивный запуск
Pop-Location
```

First configure на чистой машине требует existing downloaded dependencies или доступ
к FetchContent. Текущая проверенная конфигурация использует FETCHCONTENT_SOURCE_DIR для
уже скачанных glfw/assimp/stb/enkiTS/tracy в `out/build/x64-release/_deps`.
Для инкрементальной сборки точная использованная команда:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && chcp 65001 > nul && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out/build/shadows-dx12 --parallel 8'
```

Baseline и paired measurements после обычного build:

```powershell
# before-imgui.ini сохранён до первого Editor 2 UI запуска; не подменять пользовательский layout.
& $python tests/PrepareEditor2Baseline.py --ref 5240060eee628158378d8b5460ecd579f08cb017
& $cmake -S out/diagnostics/editor2/baseline-source -B out/diagnostics/editor2/baseline-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
& $cmake --build out/diagnostics/editor2/baseline-build --parallel 8
./tests/RunEditor2Diagnostics.ps1 -Baseline
./tests/RunEditor2Diagnostics.ps1
& $python tests/SummarizeEditor2Diagnostics.py
```

Для повторной проверки каждой сцены отдельно, из build cwd:

```powershell
./WhispEngine.exe --render-benchmark 100 ../../diagnostics/editor2/after-entities-100.csv --fixed-editor-layout
./WhispEngine.exe --render-benchmark 500 ../../diagnostics/editor2/after-entities-500.csv --fixed-editor-layout
./WhispEngine.exe --render-benchmark 1000 ../../diagnostics/editor2/after-entities-1000.csv --fixed-editor-layout
./WhispEngine.exe --light-benchmark 500 ../../diagnostics/editor2/after-lights.csv --fixed-editor-layout
./WhispEngine.exe --material-benchmark 50 ../../diagnostics/editor2/after-materials.csv --fixed-editor-layout
./Editor2Regression.exe --benchmark ../../diagnostics/editor2/after-transitions.csv
```

Tracy: из build cwd запустить capture в отдельном hidden process, затем app с `--wait-tracy`:

```powershell
Start-Process ../../diagnostics/tracy-tools/tracy-capture.exe -WindowStyle Hidden -ArgumentList '-f','-s','8','-o','../../diagnostics/editor2/after-editor.tracy' -WorkingDirectory (Get-Location).Path
./WhispEngine.exe --render-benchmark 500 ../../diagnostics/editor2/after-tracy.csv --fixed-editor-layout --wait-tracy
../../diagnostics/tracy-tools/tracy-csvexport.exe ../../diagnostics/editor2/after-editor.tracy > ../../diagnostics/editor2/after-tracy-zones.csv
# Для before: baseline-build/EditorBaseline.exe, без --fixed-editor-layout, separate output names.
```

Логи: `baseline-build.log`, `baseline-ctest.log`, `build-final.log`, `ctest-final.log`,
`build-final-guards.log`, `ctest-final-guards.log`,
`before-ui-*.log`, `after-*.log`, Tracy captures и raw CSV в `out/diagnostics/editor2`.

## 25. Ограничения

Полноценный native interactive UX walkthrough остаётся непроверенным из-за среды;
доступные automated/API/math/render checks описаны без преувеличения.
Flat hierarchy, AABB overlay highlight, ASCII case-fold поиска; parenting/stencil outline
не добавлялись. Asset directories/filtered rows собираются из cached keys в UI; filesystem
не сканируется per-frame. Legacy scenes без новых settings получают defaults.
Внутренние component-pointer lifetimes ограничены текущим вызовом; future systems должны
соблюдать phases и не держать такие pointers после World replacement.
Generic callback registration — C++ extension point, не публичная sandbox/VM API.
Будущие non-value runtime components и entity references нуждаются в явном clone/remap/destruction
policy. Event subscriptions будущих скриптов также потребуется освобождать на Stop.
Vulkan не изменён и не запускался; текущий этап и проверки относятся к DX12.

## 26. Лабораторная №2

Следующий этап следует начинать после отдельного ТЗ: определить script data/instance
разделение, register serialized properties, World-scoped bindings, runtime lifecycle
перед reset, error reporting и phase-safe commands. Затем согласовать clone policy,
hot-reload миграцию, ordering OnFixedUpdate/physics и permissions.
Эта задача заканчивается на Editor 2.0 Foundation; scripting runtime автоматически не начинается.
