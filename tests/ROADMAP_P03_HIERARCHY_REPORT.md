# P0.3 — Parent–Child Hierarchy: архитектурный аудит

**Verdict: INCOMPLETE.** Выполнен аудит; hierarchy не реализована и не объявляется
готовой. Сработало условие остановки из разделов 2 и 13 задания: надёжная
реализация требует согласованной смены крупных World/Transform/Physics
контрактов. Частичная Editor-only hierarchy не добавлялась.

Ветка: `project_system`. При первом чтении изменения P0.1/P0.2 были staged;
при финальной проверке они уже находятся в текущем HEAD `79f1dbf` (`altdrag`),
tracked working/staged diff пуст. Агент не выполнял commit/push/reset/clean/
merge/rebase и не изменял существующие исходники.

## 1. Проверенные контракты

| Подсистема | Текущее поведение и точка интеграции |
|---|---|
| `engine/ecs/components/TransformComponent.h` | Единственный position/Euler rotation/scale. Отдельных Local/World Transform и Transform System нет. |
| `engine/ecs/World.h`, `World.cpp` | Компоненты доступны через mutable pointers/references и ForEach. Изменения полей не проходят через setters или invalidation. DestroyEntity удаляет только одну Entity; поколения и World identity защищают handles. |
| `engine/ecs/systems/SystemPipeline.h`, `Application.cpp` | Есть frame/fixed phases, но нет фазы вычисления hierarchy. Lua Update, Lua FixedUpdate, solver, frame systems и editor mutations изменяют Transform в разное время. |
| `engine/ecs/ComponentRegistry.cpp` | Transform properties напрямую обращаются к полям компонента. Capture/Restore работают с каждой Entity отдельно; межсущностной валидации графа нет. |
| `engine/scene/SceneSerializer.cpp` | Schema v4, стабильные `entity.id`, staged World и ReplaceData при успешной загрузке. Загрузка пока однопроходная. LoadEntityConfigs преобразует v4 в плоские legacy configs. |
| `engine/editor/EditorUi.cpp` | Scene Hierarchy — плоские Selectable + ImGuiListClipper. Есть drag source WHISP_ENTITY, но нет parent/drop-to-root операции. Shift range использует плоский visual order. |
| `engine/editor/EditorLayer.cpp`, `EditorGroupTransform.cpp`, `GroupTransform.h` | Gizmo трактует Transform как мировой. Групповой baseline/pivot захватывает и изменяет каждый выбранный Transform. При будущих Parent+Child это даёт двойную delta. |
| `engine/editor/EditorSelection.cpp`, `SelectionSet.h` | Ordered selection с active/anchor и проверкой World/generation. DeleteSelection захватывает и удаляет только выбранные Entity. |
| `engine/editor/EditorTransactions.cpp` | Составные snapshots и RemapHistory уже есть, но RestoreSnapshot восстанавливает компоненты последовательно, не строя и не валидируя весь граф заранее. |
| P0.2 `BeginDuplicateDrag` | Копирует только выбранный набор, создаёт ID map, явно remap-ит tagged Lua references. Обхода поддерева и remap parent relationship сейчас нет. |
| `engine/ecs/systems/RenderSystem.cpp/.h` | RenderSnapshot хранит RenderPose, то есть TRS. Mesh matrices, shadow bounds и collider debug строятся из этого pose. Полная affine world matrix не передаётся. |
| `engine/ecs/RenderInterpolation.h` | Интерполирует position/scale/quaternion из мирового физического Transform. Record keyed by Entity, но ancestor dependencies неизвестны. |
| `engine/ecs/systems/PhysicsSystem.cpp` | BodyRef содержит mutable TransformComponent*. Broadphase, solver, triggers, raycast и radius queries читают мировые position/rotation; solver пишет их напрямую. Collider halfExtents/offset не являются автоматически локальной формой под parent matrix. |
| `engine/scripting/LuaRuntime.cpp` | World.get/set_position/rotation/scale обращаются к тому же мировому Transform. Destroy помечает одну Entity, вызывает её lifecycle callbacks, затем применяет MutationQueue. |
| `engine/ecs/MutationQueue.h` | Очередь проверяет World identity и вызывает одиночный DestroyEntity на phase boundary; subtree closure отсутствует. |
| `engine/ecs/PlayerSpawn.h`, `Application.cpp` | PlayerStart задаёт мировую feet position/yaw; runtime markers удаляются после spawn. Camera follow получает pose из RenderInterpolation. |
| `engine/ecs/LightGather.h` | Light position/direction берутся из мирового RenderPose. Масштаб не должен произвольно переворачивать outgoing rays. |
| `engine/ecs/systems/MotionSystem.cpp`, `BoundsBounceSystem.cpp` | Прямые мировые записи в Transform без уведомления общего Transform service. |

Аудит ограничен этими связанными путями и их Transform helpers. Project
Settings/Browser/System, EventBus, пользовательские Lua/сцены/модели не менялись.

## 2. Почему нельзя безопасно ограничиться Parent-компонентом

### Local/World — смена контракта, а не дополнительное поле

Если переопределить существующий Transform как local, Rendering, Physics,
Raycast, PlayerStart, Lights и Lua начнут принимать local за world. Если оставить
его world и добавить отдельный local без миграции writers, появятся две
независимые правды: изменение parent и прямое изменение child в том же кадре
не дают однозначного порядка пересчёта. Наличие mutable pointers исключает
надёжную invalidation через один новый setter.

Один дополнительный проход «перед рендером» недостаточен: raycast может
выполняться внутри Lua callback сразу после set_position, а физика изменяет
Transform внутри каждого substep. Нужны явные чтения/записи мирового pose и
определённые phase boundaries. Solver переписывать не требуется, но его входы и
выходы должны получить согласованный adapter и ясные ограничения parenting.

### Shear выходит за пределы текущего RenderPose

Parent scale (2,1,1) и Child rotation Z=45° дают world matrix с неортогональными
столбцами: normalized dot = -0.6. Это не TRS. Текущий DecomposeTransform
правильно отклоняет такую матрицу; RenderPose не способен её сохранить.

Проверен отдельный C++ diagnostic с существующими Transform helpers:

1. `S(2,1,1) * Rz(45°)` — shear, DecomposeTransform возвращает отказ.
2. Keep World для identity child под parent `Rz(45°) * S(2,1,1)` требует local
   `S(0.5,1,1) * Rz(-45°)` — также отказ.
3. С uniform scale композиция представима в TRS и успешно раскладывается.

Логи: `out/p03-transform-audit-build.log`, `out/p03-transform-audit.log`;
diagnostic source: `out/p03-transform-audit.cpp` (временный build artifact).
Это проверка текущего контракта, а не hierarchy regression или Release build.

Для реализации нужен affine world-matrix путь в существующих render snapshots
и bounds/picking либо более строгая, явно проверяемая политика отказа всех
создающих world shear операций. Отклонить только Keep World недостаточно:
shear возникает и при последующем обычном изменении Transform в уже существующем
дереве. Проверки должны охватывать editor, Lua и runtime, а не только DnD.

### Lifetime затрагивает Lua до фактического удаления

Добавление subtree cascade только внутри World::DestroyEntity небезопасно:
LuaRuntime до MutationQueue::Apply вызывает on_stop/on_destroy только для
явно помеченной Entity. Потомки исчезнут до корректного вызова их callbacks
при ещё существующих компонентах. Аналогично SpawnPlayer удаляет PlayerStart
markers — слепая cascade могла бы удалить их дочерние объекты в Runtime World.
Эти случаи требуют общей политики удаления и отдельного тестирования.

## 3. Предлагаемая архитектура — ещё не реализована

### Relationships и Local/World Transform

- Одна авторитетная зарегистрированная ECS relationship-запись: optional
  parent SceneIdentity + persistent sibling order. Children — производный
  индекс, не второй независимо изменяемый список.
- Resolver scoped to World identity; ID lookup/cache перестраивается после
  ReplaceData/CloneData. SceneIdentity в runtime сохраняется, ECS handles
  чужого World не копируются как готовые parent pointers.
- TransformComponent становится авторским local TRS после миграции readers
  и writers. Для roots его числовые значения остаются прежними.
- World matrix — производная affine matrix из полной parent chain. Вычисление
  итеративное, с cycle detection, детерминированным обходом и ограничением
  трудозатрат/количества узлов; не рекурсивный стек на произвольной глубине.
- Единый runtime Transform service предоставляет world reads и checked world
  writes; editor math helpers не становятся зависимостью ECS/Lua.
- Потребители, которым нужен TRS, обязаны получать проверяемый результат
  decomposition, а не молчаливую аппроксимацию shear.

### Reparent / Unparent

Предварительно проверить child/parent World, lifetime, self/cycle, наличие
Transform, physics policy и invertibility. Захватить прежнюю world matrix;
вычислить `newLocal = inverse(newParentWorld) * oldChildWorld`; проверить
lossless TRS с согласованной политикой negative scale. Для Unparent
`newLocal = oldChildWorld`. Только после всех проверок атомарно применить
relationship/local TRS/order. Отказ не меняет ни дерево, ни Transform/selection.

Keep World transaction захватывает relationship, local TRS, sibling order и
selection. Если потребуется менять order соседей, они входят в ту же
транзакцию. Redo восстанавливает stable IDs, а не создаёт новые scene IDs.

### Serialization и World cloning

Сохранить schema v4: optional зарегистрированный hierarchy component со
stable parent ID/order. Отсутствие компонента означает root и прежний Transform.
В staged World сначала восстановить Entity/components/IDs, затем проверить и
разрешить relationships независимо от порядка JSON. Missing parent, cycle,
foreign handle, non-finite/singular transforms отклонять до ReplaceData.

Registry Restore сам по себе не гарантирует валидность графа; общий batch
validator нужен также Undo/Redo и duplication. CaptureWorld сейчас валидирует
каждую Entity во временной одиночной Entity — этот путь должен учитывать
межсущностные проверки отдельно. Legacy LoadEntityConfigs нельзя оставлять
молча теряющим связи: его hierarchical вход должен быть явно обработан либо
отклонён в вызывающем compatibility path.

CloneData сейчас побайтно по смыслу копирует значения T, не remap-ит встроенные
Entity handles. Stable parent IDs плюс производный World-scoped resolver
позволяют сохранить изоляцию без общего переписывания component storage.

### Политика удаления Parent

Предлагается явное удаление всего поддерева: заранее собрать уникальный closure,
вызвать Lua lifecycle для всего closure до удаления компонентов, затем удалить
детей раньше родителей на phase boundary. Editor capture/Undo должны хранить
весь closure одной операцией и восстанавливать Entity/компоненты до relationships.
Непосредственный World destroy, Application destroy, Lua deferred destroy и
Runtime PlayerStart cleanup должны следовать проверенной общей политике.
Удаление runtime marker требует отдельно выбранного действия для surviving
children; нельзя автоматически каскадировать его как обычное пользовательское
удаление. Политика здесь предложена, в текущий World.destroy не внесена.

### Rendering / Physics / Lua

- Renderer, shadow bounds, selection picking и debug geometry получают одну
  и ту же evaluated world matrix. RenderInterpolation должен учитывать
  parent motion; world TRS cache не должен незаметно стирать shear.
- Dynamic Rigidbody/Character для первого runtime MVP разумно ограничить
  roots; visual children динамического root могут наследовать его world pose.
  Static collider parenting требует согласованного world shape adapter,
  включая offset/halfExtents и неподдерживаемые affine случаи. Одного запрета
  dynamic children недостаточно для корректности static collider/raycast.
- Physics queries и solver должны использовать ту же мировую геометрию, что
  Viewport. Новый physics solver не предлагается.
- Существующие Lua get/set_position/rotation/scale сохраняют world semantics.
  Непредставимая операция возвращает понятную ошибку без частичной записи;
  Local API, если потребуется, добавляется отдельно. Смешанный affine world
  transform требует явно определённой семантики get_rotation/get_scale.
- Camera, PlayerStart и lights используют evaluated world pose с отдельно
  проверенными правилами rotation/scale; существующие root сцены сохраняют
  прежнее поведение.

### Editor / Gizmo / P0.2

UI tree строится из evaluated relationship index. Search/filter должен давать
понятный visible order с необходимыми ancestors; Shift range использует именно
его. Existing ImGui payload остаётся, добавляются DnD target на строке/root,
cycle/error feedback и реальные native input regression tests.

Групповой Gizmo преобразует только effective selected roots: выбранный потомок
выбранного ancestor не получает повторную world delta. Полный selection и
active/anchor сохраняются независимо от этого фильтра.

P0.2 сохраняет одну transaction и once-per-drag gate. Предлагаемая duplication
policy — уникальный union поддеревьев выбранных roots, без повторного clone для
явно выбранного Child. Сначала выделяется весь clone set и ID map; потом
remap-ятся parent IDs и typed Lua references. Ссылки вне closure остаются
внешними. Single selection Parent дублирует поддерево по той же политике.
Исходное explicit selection remap-ится на соответствующие копии; потомки,
добавленные только closure, не обязаны становиться selected.

## 4. Предлагаемое разделение P0.3

| Этап | Конкретный результат | Условие приёмки |
|---|---|---|
| P0.3a — Transform contract | Общие runtime matrix/inverse/checked TRS helpers; world read/write API; миграция прямых readers/writers с root-only семантикой. Relationships ещё не включены. | Старые scenes/Lua API/root physics/render interpolation без изменений; Release + существующие regression tests. |
| P0.3b — ECS graph и persistence | Authoritative parent/order, iterative evaluation, checked Keep World, staged schema v4 loading, World cloning и lifetime closure. | CPU graph/depth/cycle/shear/serialization/lifecycle tests; invalid load не меняет World; Undo batch validate. |
| P0.3c — Runtime consumers | Affine render/bounds/picking; согласованные physics/query adapters и guards; world-space Lua/camera/lights/PlayerStart; безопасные phase boundaries. | Parent/Child в Play/Stop, world rendering/physics/raycast consistency, Lua get/set, marker deletion и interpolation regressions. |
| P0.3d — Editor integration | Tree/DnD, Reparent/Unparent transactions, selected-root Gizmo, P0.2 subtree closure/remap. | Native Hierarchy DnD, Undo/Delete/P0.2 tests, профильные проверки и полный CTest/DX12 validation. |

Зависимости: a → b → c → d. Не показывать пользователю hierarchy editing,
пока runtime consumers не обеспечивают один и тот же world-space результат.
Этапы не требуют Prefab Editor, нового solver или нового Scene Graph Framework.

## 5. Верификация и сохранность

- Standalone C++ Transform contract diagnostic: PASS (три сценария выше).
- P0.3 hierarchy regressions/native DnD: не созданы и не запускались, поскольку
  реализация остановлена после аудита согласно заданию.
- Release build, профильный и полный CTest P0.3: не запускались; изменений
  production-кода или существующих тестов нет. Предыдущий результат P0.2
  **56/56 PASS и DX12 errors=0** — baseline, а не новая верификация P0.3.
- Нестабильность существующего LuaGameplayControllerDx12Regression из P0.2
  остаётся известной. Финальный PASS предыдущего этапа не доказывает её
  устранения; тест и assertions не менялись и повторными прогонами не маскировались.
- `git diff --check`: PASS; tracked working/staged diff пуст.
- Хеши пяти пользовательских Lua-файлов и EventBus совпадают с сохранённым
  baseline предыдущих этапов (6/6). Итоговые проверки:
  `out/p03-audit-verification.json`, `out/p03-protected-verification.json`,
  `out/p03-final-status.txt`. Единственное новое изменение репозитория — отчёт.

## 6. Изменённые файлы и ограничения

Единственный новый репозиторный файл этого этапа:
`tests/ROADMAP_P03_HIERARCHY_REPORT.md`. Диагностика и audit logs находятся
только в ignored `out/`. Production-код, существующие тесты, staged changes,
пользовательские assets и EventBus не изменены.

Функции Parent/Child, Keep World, tree UI, hierarchy Save/Load/Undo/Physics/Lua
в этом этапе отсутствуют. Предлагаемые решения выше являются проектом следующих
этапов, а не описанием реализованного поведения. Итог: **INCOMPLETE — audit only**.
