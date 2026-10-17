# P0.3b — ECS Hierarchy Graph & Persistence

## Объём и безопасная граница

Ветка `project_system`. Изучены архитектурный отчёт P0.3 и фактический
Transform Contract P0.3a. Второй Transform service не вводился: расширены
существующие runtime TransformAccess/TransformMath. Physics solver, RenderPose,
порядок фаз Application и пользовательский Editor workflow не перестраивались.

Фактический аудит P0.3a: математика находится в `ecs/TransformMath.h`, value
reads и checked world setters — в `ecs/TransformAccess.h`; для root World
world matrix равна ComposeTRS. RenderInterpolation/LightGather читают world
snapshots, PhysicsQueries и solver BodyRef используют root adapter, Motion,
BoundsBounce, PlayerSpawn, Lua и основные Gizmo writers уже используют этот
контракт. Physics solver сохраняет mutable BorrowRootWorldTransform;
render snapshots остаются TRS RenderPose. Raw World GetComponent/ForEach,
component factories, CloneData и registry restore обходят setters, поэтому
cache invalidation по одним setter-вызовам была бы ненадёжна. В P0.3b эти
обходы учтены свежей валидацией графа и staged публикацией, без второго кэша.

`World::DataMode` фиксируется при создании World:

- `RootOnly` — прежний режим, default для Application, Editor, Project и Legacy.
- `HierarchyData` — внутренний режим данных для graph/persistence/transactions.

Relationship нельзя добавить в RootOnly даже через native AddComponent.
Компонент не добавляется через Inspector. Обычная загрузка hierarchy-сцены,
включая legacy LoadEntityConfigs, завершается ошибкой до публикации staged
World. ReplaceData не переносит данные между разными режимами.
System pipeline, прямые входы Physics/Render, Lua Start и PlayerSpawn отвергают
HierarchyData. Lua Start проверяет границу до Stop текущего runtime.
Таким образом, новая hierarchy не становится частично работающей игровой
функцией. UI parenting и Play hierarchy относятся к следующим этапам.

## Relationships и graph validation

Единственная авторитетная связь — optional RelationshipComponent:
`parent: optional<SceneIdentity string>`, `order: uint64`. Отсутствующий parent
означает root; отсутствие компонента также означает root с порядком 0.
Допустимый order — целое от 0 до INT64_MAX. При совпадении order siblings
сортируются по SceneIdentity; корни используют те же правила.
Handles/index/generation в JSON не сохраняются.

HierarchyGraph — короткоживущий immutable value snapshot, создаваемый заново
после изменений. Он содержит ID/Entity indexes, derived children, roots,
preorder и полные world matrices. Независимо редактируемого children storage и
долгоживущего кэша без invalidation нет. Построение O(N + сортировка siblings),
поиск в готовом snapshot — hash lookup. Convenience queries строят новый граф;
для серии запросов следует использовать один snapshot.

Проверяются World identity/generation, owner thread, уникальные непустые ID
длиной до 128 без NUL, parent existence, self-parent, Transform участников,
order, cycles и конечность композиции. Обход итеративный; недостигнутые узлы
означают цикл. Work budget — 1 000 000 живых Entity, рекурсивного depth limit
нет; тест проходит цепочку глубиной 4096. Raw ECS запись по-прежнему возможна
для внутреннего staging, поэтому каждый checked вход заново валидирует граф.
Snapshot нельзя повторно использовать после мутации World.

## World / Local Transform и checked mutations

В HierarchyData TransformComponent хранит authored local TRS.
WorldMatrix = ParentWorldMatrix × ComposeTRS(local), для root — ComposeTRS(local).
Полная double affine matrix сохраняет shear и negative scale.
World position читается из translation; world rotation/scale/Transform
возвращаются только при успешной checked decomposition. Shear не теряется
путём безусловной конверсии в TRS.

Используются прежние P0.3a проверки finite, inverse residual, orthogonality и
reconstruction. Участники hierarchy должны иметь обратимые world matrices;
singular/near-singular relationships отклоняются. Unrelated flat root без
Relationship сохраняет старую возможность нулевого scale.
RootOnly fast path и числовая семантика authored Euler/scale не изменены.
BorrowRootWorldTransform не допускается для HierarchyData.

SetParent / ClearParent реализуют KeepWorld: проверка handles/цикла, inverse
нового parent, checked local TRS decomposition, append sibling order, затем
валидация всего предполагаемого графа с candidate local и relationship.
Только после успешной подготовки выполняются авторитетные записи.
Непредставимый local shear, singular parent, exhaustion order или некорректный
граф оставляют World без изменений. World setters также проверяют candidate
граф, включая потомков, до записи. Position setter сохраняет affine linear
part, если соответствующий local TRS остаётся представимым.

## Serialization и rollback

Schema остаётся v4, optional `components.Relationship` имеет строго форму
`{"parent": string|null, "order": integer}`. JSON floats, bool, negative и
слишком большие order, неизвестные поля и повреждённый parent отклоняются.
Transform Capture/Restore сохраняет именно local TRS, независимо от world-space
property API. Значения старых плоских сцен сохраняются как прежде.

RestoreWorld работает в World того же режима: сначала создаёт все Entity с
ID, затем восстанавливает компоненты, валидирует весь graph и только потом
публикует через ReplaceData и обновляет SceneSettings. Порядок Entity в JSON
не влияет на parent resolution. Для HierarchyData выбор primary directional
при равных предпочтениях также стабилизирован по ID; RootOnly выбор сохранён.
Load failure сохраняет активные компоненты, slots, World identity и settings.
Save предварительно проверяет граф; disk roundtrip входит в regression.

## Cloning и подготовка Undo / Duplicate

CloneData сохраняет DataMode, валидирует исходный граф, копирует components и
даёт прежний Entity map для нового World. Parent IDs остаются стабильными и
разрешаются внутри копии. ReplaceData валидирует staged graph до move,
сохраняет IDs и меняет World identity: старые handles становятся недействительны.

ComponentRegistry::RestoreBatch — атомарный upsert набора `{id, components}`:
клонирует World, сначала создаёт все отсутствующие Entity, затем восстанавливает
компоненты, проверяет полный graph и публикует результат. При успехе меняется
World identity, поэтому будущему Undo caller нужно заново разрешить stable IDs.
Это подготовка инфраструктуры, не изменение пользовательского Undo UI.
Обычный per-entity Restore остаётся низкоуровневой частью staging и сам по себе
не является атомарной graph transaction.

PrepareHierarchyClone возвращает unique descendant closure и collision-free
original ID → cloned ID map. RemapRelationship меняет только parent из map;
внешний parent остаётся внешним. Tagged Lua references не переписываются
по совпадению строк. Существующий flat Alt+Drag workflow не подключён к новому
пути и проверяется прежними MultiSelection regressions. Полная subtree Gizmo
duplication и remap остальных typed references — интеграционная задача P0.3d.

## Lifecycle: реализованное и отложенное

Для HierarchyData World::DestroyEntity на phase boundary собирает unique
descendant closure и удаляет reverse preorder — дети раньше родителей.
Перед удалениями резервируется free-index storage. Остальные Entity не
затрагиваются; dangling parent references не остаётся. MutationQueue использует
тот же вход после завершения iteration. Повторные parent+child requests,
generation reuse и команды старого World после ReplaceData безопасны.

HierarchyData не имеет запущенных Lua/Physics instances. Поэтому каскад не
имитирует callbacks отсутствующего runtime. RootOnly destroy и существующий
Lua World.destroy сохраняют прежний порядок lifecycle: callbacks до удаления
компонентов и deferred mutation. Это проверено реальным Lua on_destroy, который
пишет в другую Entity перед удалением self.

PlayerSpawn отвергает HierarchyData до любой очистки PlayerStart: игровые дети
marker не могут быть случайно удалены. Полная runtime cascade с Lua descendant
callbacks, Editor Delete/Undo, Application Play lifecycle и parent-aware
PlayerStart policy отложена до согласованных контрактов P0.3c/d.
Публичные пользовательские пути parenting закрыты. Это разрешённая заданием
безопасная граница блока D; частичный игровой cascade не включён.

## Проверка

Release build — PASS. Отдельные блоки CPU regression — PASS: graph 34,
transform 111, persistence 93, lifecycle 23 (всего 261).
Первый профильный CTest — 21/21 PASS, 109.70 s.
Первый полный CTest — 56/58, 325.66 s: Editor2 CPU/DX12 остановились на
точном устаревшем count 17 встроенных компонентов. После регистрации
Relationship их 18. Ожидание обновлено до точного 18, добавлена отдельная
проверка стабильного ID и закрытых Inspector flags/properties Relationship;
проверка не заменялась нестрогим сравнением и не удалялась.
Финальная incremental Release build — PASS. Расширенный профильный CTest,
включающий оба Editor2-теста, — 23/23 PASS, 117.52 s; DX12 error markers — 0.
Финальный полный CTest — **58/58 PASS**, 283.88 s. Включает существующие ECS,
Transform (718 проверок), scene serialization/transactions, Lua lifecycle,
deferred mutation, flat MultiSelection, Project System/Settings и CPU/DX12
Editor regressions. Hierarchy в полном прогоне — 261 checks PASS.
DX12 ERROR/CORRUPTION/validation-failure markers в финальных profile/full logs —
**0**, включая штатные assertions GPU regressions.
`git diff --check` — PASS. SHA256 пяти пользовательских Lua и EventBus совпадают
с началом этапа. Изменения пользовательских assets/сцен отсутствуют.

Журналы находятся в `out/`: `p03b-release-build.log`, `p03b-final-build.log`,
`p03b-{graph,transform,persistence,lifecycle}.log`,
`p03b-final-profile-ctest.log`, `p03b-final-profile-detail.log`,
`p03b-final-full-ctest.log`, `p03b-final-full-detail.log`.
Первоначальные 56/58 и диагностика сохранены отдельно в
`p03b-full-initial-ctest.log` / `p03b-full-initial-detail.log`.

Отдельный EcsHierarchyRegression содержит блоки `--graph`, `--transform`,
`--persistence`, `--lifecycle`. Проверяются реальные Worlds, component capture,
staged restore, disk Save/Load, clone/replace, batch transaction и Lua runtime.
Rejected mutations сравнивают весь captured World вместе с identity,
slots/handles; scene rejection дополнительно сравнивает settings.
Существующие assertions и допуски старых тестов не ослаблялись.

## Изменённые файлы этапа

- `engine/ecs/components/RelationshipComponent.h` — авторитетная связь.
- `engine/ecs/Hierarchy.h`, `Hierarchy.cpp` — graph, queries, world evaluation,
  checked parenting, closure, clone preparation.
- `engine/ecs/World.h`, `World.cpp` — DataMode gates, clone/replace, data cascade.
- `engine/ecs/TransformAccess.h` — расширение существующего API для data graph.
- `engine/ecs/ComponentRegistry.h`, `ComponentRegistry.cpp` — local persistence,
  Relationship descriptor, atomic batch restore.
- `engine/scene/SceneSerializer.cpp` — two-pass staging и graph validation.
- `engine/ecs/PlayerSpawn.h`, `engine/scripting/LuaRuntime.cpp`,
  `engine/ecs/systems/PhysicsSystem.cpp`, `RenderSystem.cpp` — runtime gates.
- `CMakeLists.txt`, `engine/CMakeLists.txt` — исходник graph и CPU regression.
- `tests/EcsHierarchyRegression.cpp`, этот отчёт.
- `tests/Editor2Regression.cpp` — точный новый component count и descriptor gate.

До этапа в working tree уже находились незакоммиченные изменения P0.3a и
другие пользовательские правки. Они не относятся целиком к diff P0.3b и
сохранены; Application, EventBus и пользовательские Lua/сцены/assets на этом
этапе не редактировались. Git history/index не изменялись агентом.

## Вердикт

**READY для P0.3b в разрешённой заданием границе безопасного data foundation.**
Обязательные graph/persistence/transaction и data lifecycle проверки проходят,
root-only регрессии сохранены, небезопасных пользовательских путей активации
неполной hierarchy нет. Hierarchy gameplay/render/physics/UI остаются закрытыми
до P0.3c/d. Ручная проверка нового hierarchy UI неприменима: UI не добавлялся;
интерактивный пользовательский сеанс вручную не проводился. Выполнены реальные
автоматические DX12 UI/render/runtime regressions. Commit/push/reset/clean/
merge/rebase не выполнялись.
