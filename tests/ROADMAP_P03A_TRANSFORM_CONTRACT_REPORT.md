# P0.3a — Transform Contract Foundation

## Объём и архитектура

Реализован компактный header-only runtime контракт в ECS, без зависимости от
Editor. Все Entity остаются roots. TransformComponent сохраняет исходные
position/Euler rotation/scale и остаётся единственным источником истины.
Дополнительного world/local кэша, parent relationships, synchronization phase
или изменения порядка Lua/Physics/frame updates нет.

`engine/ecs/TransformMath.h`:

- `Matrix4` — column-major 4×4 double matrix; сам тип не гарантирует TRS.
- `ComposeTRS` сохраняет прежний float compose path Rz*Ry*Rx;
  `TryComposeTRS` проверяет конечность входа и результата.
- `IdentityTransformMatrix`, `MultiplyTransformMatrices`, `InverseAffine`.
- `DecomposeTRS` — checked conversion с сохранением preferred negative signs.
- `Finite` для Vec3, TransformComponent и Matrix4.
- Прежние RenderPose/quaternion/compose/slerp перенесены из RenderInterpolation
  без изменения формул. Интерполяция остаётся отдельным потребителем.

`engine/ecs/TransformAccess.h`:

- `ReadWorldTransform`, `ReadWorldPosition`, `ReadWorldRotation`,
  `ReadWorldScale`, `ReadWorldMatrix`.
- `TrySetWorldTransform`, отдельные checked world position/rotation/scale
  setters и `TrySetWorldMatrix`.
- Чтения возвращают value snapshots, не mutable pointer и не ссылку на новый
  кэш. Отсутствующие компоненты и stale/foreign handles дают empty optional.
  TRS reads отражают хранимые значения; checked matrix acquisition дополнительно
  отвергает nonfinite compose. Некорректный Transform, записанный напрямую в
  ECS в обход API, не исправляется молча.
- Setters проверяют owner thread, lifetime/World/component и конечность целого
  кандидата, затем выполняют одну запись. Ошибка не создаёт компонент и не
  оставляет частично записанный Transform. Ошибки возвращаются bool/string;
  нарушение owner-thread — существующий logic_error World.
- `BorrowRootWorldTransform` — явно названный временный in-place adapter для
  root-only solver. Это сознательно сохранённый mutable доступ, не универсальный
  способ редактирования из Lua/UI и не готовый hierarchy solver contract.

## Математика и невалидные входы

Полная affine matrix, invertible matrix и представимый TRS различаются:
матрица с shear может успешно проходить InverseAffine, но отклоняется
DecomposeTRS и TrySetWorldMatrix. Perspective/non-affine матрицы отклоняются.
NaN/Infinity, singular и near-singular matrix conversion/inverse дают отказ.

InverseAffine использует pivoted Gauss–Jordan для affine linear 3×3,
проверяет абсолютный pivot 1e-6 и относительный 1e-12 от нормы linear part,
конечность результата и обе inverse products с residual <=1e-7.
DecomposeTRS сохраняет прежние допуски editor decomposition: проверку
orthogonality, reconstruction и singular scale; scale range до 10000,
matrix entries до 1e8. Assertions и допуски старых тестов не менялись.

Явный конечный TRS с нулевым scale **по-прежнему допустим**: это часть
существующей scene/Inspector/Lua семантики. Такой Transform можно читать,
сохранять и Compose, но нельзя invert/decompose как невырожденную матрицу.
Числовые значения root Transform не проходят лишнюю decomposition при обычной
записи; Euler radians, negative signs и authored scale сохраняются.

Editor `TransformMath.h` оставлен тонким compatibility wrapper к runtime math.
Render-facing affine hierarchy и новая семантика rotation/scale при shear не
реализованы: это P0.3c.

## Переведённые consumers

| Потребитель | Изменение |
|---|---|
| ComponentRegistry | Transform property get/set идут через world contract; прежние имена, диапазоны и JSON fields сохранены. |
| LuaRuntime | Private Transform accessor возвращает world value; все шесть существующих Lua get/set сохраняют сигнатуры. Rotation/scale сохраняют Registry validation; position сохраняет прежнюю finite-only input validation. Ошибки проходят прежние protected calls. |
| Prefab spawn | После прежней staged validation начальная world position записывается checked setter. Формат prefab не менялся. |
| MotionSystem | Рассчитывает world candidate прежними формулами, затем checked commit. |
| BoundsBounceSystem | Прежние bounds/sign правила; candidate Transform и velocity применяются после успешной проверки Transform. |
| PhysicsSystem | Raycast/radius query читают world value; BodyRef/trigger shape acquisition используют BorrowRootWorldTransform. Solver integration, contacts, triggers, broadphase и job order не менялись. |
| RenderInterpolation | Capture/ResetEntity получают world value; Record, Begin/EndTick, comparison, lerp/slerp и teleport/fallback поведение сохранены. Resolve остаётся low-level physical-pose adapter. |
| RenderSystem | Mesh/shadow submission и collider debug получают world pose через контракт. Матрицы и render submission алгоритмы прежние. |
| LightGather | World value передаётся прежнему interpolation/light direction пути. |
| PlayerSpawn | Marker world read и checked initial player pose. Размеры player, marker cleanup и lifecycle прежние. |
| Application | Camera follow и FocusEntity используют world snapshot. |
| Single/group Gizmo | World reads и checked writes; прежние pivot/snapping/space rules, P0.2 duplication и transactions. |
| EditorScalePlanes / Selection / Transactions | Plane scale write, invalidated group rollback и quarter-turn read используют контракт; capture/restore history остаются существующими. |
| LightGizmos | Model placement world position использует checked write. |
| SunSynchronization | World rotation write/read через контракт; daylight formulas/order сохранены. |

## Оставшийся direct access — явные границы миграции

Контракт **не полностью централизован**. Инвентаризация связанных путей:
`out/p03a-transform-access-inventory.txt`. Само наличие Transform в ForEach
может быть membership filter, а не чтение координат.

| Файл/путь | Почему оставлен direct access и следующая точка миграции |
|---|---|
| World.h / World.cpp generic storage, CloneData | Публичный mutable ECS storage не переписывается. Default/component construction и value clone остаются storage операциями. P0.3b должен ввести отдельный граф/validation, а P0.3c — consumer adapters; нельзя считать raw writes автоматически отслеживаемыми. |
| PhysicsSystem BodyRef, Rotate/integration/projection, trigger geometry | Solver пишет в borrowed root storage внутри substeps/jobs. Механический checked setter внутри каждого impulse изменил бы execution/ownership contract. В P0.3c нужен world physical-pose adapter с explicit root/body guards и checked commit boundaries; текущий Borrow adapter не должен обслуживать parented bodies. |
| RenderInterpolation::Resolve(Entity, physical) | Принимает физический TRS, в том числе из существующих regression fixtures. Его temporal history не заменена новым кэшем. В P0.3c определить ancestor-aware evaluated physical pose до этой границы. |
| ComponentRegistry descriptor add/reset/deserialize; SceneSerializer.cpp и legacy ConfigLoader representation | Это авторское хранение/загрузка, не gameplay world move. JSON schema v4 и raw TRS сохранены. До P0.3b необходимо отделить future local serialization от world property get/set; сейчас они совпадают только потому, что все roots. |
| Application.cpp CreateEcsEntity/bootstrap/projectile/debug scene factories, stress/diagnostic scene setup; PhysicsStressScene.h | Первичное создание root-компонентов из проверенных configs/fixtures. Не добавлялись общие callbacks storage. Перед поддержкой non-root factories определить authoring local versus world placement explicitly. |
| Application.cpp ECS diagnostic logging, collider mesh autofit и Play initial camera pose | Diagnostic/root construction/autofit читают legacy root values; не добавлялся новый physics sizing policy. В P0.3c мигрировать autofit вместе с world collider geometry, а initial camera read — вместе с PlayerStart hierarchy policy. Follow camera уже переведена. |
| EditorLevelWorkflow.cpp bulk object creation | Начальное authored root placement в существующей составной creation transaction. В P0.3b/d staged graph creation должен совместно валидировать relations и authored transforms; здесь не реализовано subtree placement. |
| LightGizmos.cpp occluders/picking/selection bounds/PlayerStart visual handles; EditorEntityIcons.cpp; EditorFogGizmos.cpp | Старые root geometry helpers остаются TRS-based. Переход на evaluated world affine geometry относится к P0.3c вместе с bounds/raycast, а не к механической подмене матрицы в одном UI месте. Placement writer переведён. |
| EditorUi.cpp Local/World scale enablement; EditorLayer.cpp Scene Settings sun diagnostic; EditorVisualSettings.cpp sky preview | UI orientation/preview policy по прежним root TRS. До hierarchy migration нужно определить world rotation при shear и source semantics sky/light; переименование getter не решает эту политику. PrimarySun/LightComponent ForEach без чтения TRS — только фильтр membership. |
| Чистые math helpers PlayerLookDirection/PlayerBoxPoint, GroupTransform и scale geometry | Получают переданный value, не имеют доступа к World. Их вызывающая сторона должна передать evaluated pose; root formulas не менялись. |

В частности, на P0.3b нельзя просто включить ParentComponent и считать этих
consumers hierarchy-ready. Нужны дальнейшие P0.3b/c/d из архитектурного аудита;
этот этап их не реализует.

## Совместимость и сохранность дерева

Scene schema v4, TransformComponent layout/JSON, asset formats, Project System,
Project Settings, Legacy Mode и ResourceManager не менялись. Ни одной
автоматической миграции сцен нет. P0.2 и Undo/Redo используют прежние snapshots.

В начале уже существовали незакоммиченные изменения PhysicsQueries, EventBus,
BoundsBounceSystem, PhysicsSystem, EditorInspector, EntityReference и LuaRuntime,
а также отчёт P0.3. Они не сбрасывались. Начальный diff/status сохранены в
`out/p03a-initial-diff.patch` и `out/p03a-initial-status.txt`; перед изменением
пересекающихся consumers сохранены копии в `out/p03a-baseline-files/`.
Во время работы PhysicsQueries.h дополнительно отформатирован вне изменений
агента; текущая версия сохранена. Её текст совпадает с HEAD без whitespace
(`out/p03a-external-format-verification.json`). Исходные diffs EventBus и
EditorInspector остались прежними.

Первый Release build обнаружил уже существовавшую отдельную строку `''` в
EntityReference.h. Удалена только эта лишняя синтаксическая строка; форматирование
и логика пользовательской правки сохранены. EventBus, пользовательские Lua,
сцены и модели не редактировались. Git mutation commands не выполнялись.

## Верификация

Release build: PASS (`out/p03a-release-build.log` и первоначальный полный
compile log `out/p03a-release-build-initial.log`).

Новый `TransformContractRegression`: **718 checks PASS**. Проверяет независимый числовой
matrix oracle, signed Compose/Decompose, affine inverse с shear, rejection
singular/near-singular/perspective/NaN/Infinity, failure atomicity, stale/foreign
handles и owner thread. Интеграционные проверки: Lua legacy getters/setters и
protected errors, fixed updates, disk schema v4 Save/Load, physics + raycast
world consistency, render interpolation/matrix position, Motion/Bounds,
group Gizmo Undo/Redo и Play/Stop isolation.

Профильный CTest: **18/18 PASS**, 105.83 s (`out/p03a-profile.log`,
`out/p03a-profile-detail.log`): TransformContract, Physics, CharacterMovement,
RenderInterpolation, LuaGameplay API/Controller, MultiSelection, ScalePlane,
EditorWorkflow, ProjectSystem и ProjectSettings, включая native DX12 варианты.

Полный CTest: **57/57 PASS**, 294.24 s (`out/p03a-ctest-full.log`,
`out/p03a-ctest-full-detail.log`). Все тесты запущены один раз, без retry-флагов
и повторного запуска для получения PASS. DX12 validation error matches: **0**
в профильном и полном журналах; native assertions GetValidationErrorCount
также прошли. `git diff --check`: **PASS**.

Существующий LuaGameplayControllerDx12Regression прошёл и профильный, и полный
прогоны. Известная desktop focus/input нестабильность предыдущих этапов этим
не объявляется устранённой: сам тест и assertions не менялись.

Хеши пользовательских Lua и исходного для P0.3a EventBus совпадают, 6/6.
Итоговая машинная сводка: `out/p03a-validation-audit.json`. Копии пересекавшихся
исходных файлов показывают узкие изменения поверх прежнего форматирования:
LuaRuntime 5/3 строк, PhysicsSystem 5/4, BoundsBounceSystem 6/1 (added/removed).
Полный исходный пользовательский diff не сбрасывался.

## Файлы P0.3a

Новые: `engine/ecs/TransformMath.h`, `engine/ecs/TransformAccess.h`,
`tests/TransformContractRegression.cpp`, этот отчёт.

Изменены:

- `CMakeLists.txt` — новый regression target;
- `engine/core/Application.cpp` — world camera/focus reads;
- `engine/ecs/ComponentRegistry.cpp`, `LightGather.h`, `PlayerSpawn.h`,
  `RenderInterpolation.h`;
- `engine/ecs/systems/MotionSystem.cpp`, `BoundsBounceSystem.cpp`,
  `PhysicsSystem.cpp`, `RenderSystem.cpp`;
- `engine/editor/TransformMath.h`, `GroupTransform.h`, `EditorGroupTransform.cpp`,
  `EditorLayer.cpp`, `EditorScalePlanes.cpp`, `EditorSelection.cpp`,
  `EditorTransactions.cpp`, `LightGizmos.cpp`, `SunSynchronization.h`;
- `engine/scripting/LuaRuntime.cpp`, `Prefab.cpp`, `EntityReference.h`.

Остальные присутствовавшие в status изменения относятся к исходному дереву.
Ручная визуальная проверка не выполнялась. Parent relationships, tree UI,
Reparent/Unparent, cascade deletion и subtree duplication отсутствуют.

**Итоговый verdict: READY для P0.3a.** Это готовый root-only foundation с
задокументированными adapter boundaries, а не полностью централизованная
hierarchy-ready система. К P0.3b самостоятельно не переходили.
