# WhispEngine — Roadmap Resume Checkpoint

Дата: 2026-10-17. Рабочая папка:
`C:\Users\Тимофей\source\repos\WhispEngine`.
Текущая ветка проверена: **`project_system`**.

## Состояние и граница продолжения

- **P0.3a — Transform Contract Foundation: завершён.**
- **P0.3b — ECS Hierarchy Graph & Persistence: завершён.**
- **P0.3c — Runtime Hierarchy Integration: завершён.**
- **P0.3d — Hierarchy Editor Integration: INCOMPLETE, остановлен после аудита блока A.**
- Следующий этап — **только P0.3d-A: подготовка безопасного открытия и сохранения HierarchyData**.

Это документ для возобновления, а не разрешение продолжать реализацию сейчас.
**Не переходить к Tree UI, Gizmo, Reparent или P0.4 до завершения P0.3d-A.**
Не включать частичный публичный hierarchy workflow.

Прочитан [отчёт P0.3d](C:/Users/Тимофей/source/repos/WhispEngine/tests/ROADMAP_P03D_EDITOR_HIERARCHY_REPORT.md).
В нём выполнен аудит; production-код и тесты на этапе P0.3d не менялись.
При возобновлении также учитывать отчёты
[P0.3a](C:/Users/Тимофей/source/repos/WhispEngine/tests/ROADMAP_P03A_TRANSFORM_CONTRACT_REPORT.md),
[P0.3b](C:/Users/Тимофей/source/repos/WhispEngine/tests/ROADMAP_P03B_ECS_HIERARCHY_REPORT.md),
[P0.3c](C:/Users/Тимофей/source/repos/WhispEngine/tests/ROADMAP_P03C_RUNTIME_HIERARCHY_REPORT.md),
[исходный аудит P0.3](C:/Users/Тимофей/source/repos/WhispEngine/tests/ROADMAP_P03_HIERARCHY_REPORT.md)
и [P0.2](C:/Users/Тимофей/source/repos/WhispEngine/tests/ROADMAP_P02_MULTI_DUPLICATE_REPORT.md).

## Проверка: публичный Hierarchy Editor отключён

При создании checkpoint проверен код, без запуска приложения:

- В `Application.h` GetEditorWorld возвращает m_World; m_World создаётся
  как обычный ecs::World, то есть RootOnly.
- В `World.h` default DataMode — RootOnly, DataMode неизменяемый;
  добавление RelationshipComponent в RootOnly запрещено.
- В `World.cpp` ReplaceData по-прежнему отвергает разные DataMode.
- OpenScene/OpenProject и Startup Scene validation создают RootOnly candidates.
- Scene Hierarchy в `EditorUi.cpp` остаётся плоским списком с подписью
  `entities (flat hierarchy)` и drag source WHISP_ENTITY; обработчиков
  parenting/drop-to-root в этом UI нет.

Внутренний HierarchyData graph/runtime harness существует, но не означает
наличие публичного Editor workflow. Проверка статическая; новое дерево
вручную не проверялось, поскольку оно не реализовано.

## Три критические проблемы блока A

1. **Несовместимая Collider semantics.** В RootOnly halfExtents/offset имеют
   прежний мировой смысл. В HierarchyData размеры масштабируются world scale,
   offset преобразуется world matrix. Статический root Box с scale=(2,2,2)
   и halfExtents=(0.5,0.5,0.5) имеет разные мировые размеры в двух режимах,
   хотя graph/runtime validation может пройти в обоих. Dynamic collider root
   в HierarchyData дополнительно требует unit scale и zero offset. Простое
   копирование компонентов не доказывает сохранение прежней физики.
2. **Нет persisted World DataMode.** Schema v4 записывает
   version/settings/entities без режима. Плоские RootOnly и HierarchyData
   сцены без Relationship неразличимы по этому признаку. Режим нельзя надёжно
   выводить только из наличия связей; Save/Load может изменить физическую семантику.
3. **Application World ownership.** m_World объединяет authoring data и
   владельца Physics/Render systems и GPU caches. CloneData не копирует системы,
   ReplaceData сохраняет их, но запрещает смену режима. Scene/Project publication
   и настоящий Play сейчас опираются на этот контракт. Нужна минимальная
   согласованная граница владения и атомарная публикация подготовленных данных.

Снимать DataMode guards, менять const DataMode, молча переписывать Collider
или flatten-ить hierarchy запрещено. Перестройка renderer/solver не является
предлагаемым решением.

## Точный план следующего этапа: только P0.3d-A

1. Сверить ветку, dirty status и этот checkpoint с фактическим кодом. Читать
   только связанные World/Application/ApplicationProject/SceneSerializer и
   соответствующие проверки. Сохранить все прежние изменения.
2. Зафиксировать явный persisted режим сцены в совместимом расширении v4:
   новые сцены — HierarchyData; старые документы без признака не должны молча
   менять RootOnly physics semantics. Отдельно определить безопасную политику
   прежних внутренних v4 документов с Relationship без признака режима;
   не загружать их в RootOnly и не отбрасывать связи. Проверить unknown/invalid
   режимы и roundtrip HierarchyData без единого Relationship. Конкретное поле
   JSON ещё не реализовано и не утверждено кодом.
3. Минимально отделить заменяемый author World от владельца систем Application.
   Сохранить фиксированный DataMode каждого World, действующие GPU/system
   lifetimes, изоляцию Editor/Runtime и запрет межрежимного ReplaceData.
   Подготовить candidate и все потенциально отказные проверки до смены
   ProjectContext, очистки ресурсов или публикации author World.
4. Подготовить явный staged RootOnly → HierarchyData upgrade: сохранить все
   stable IDs, компоненты и root world TRS; проверить graph, runtime constraints
   и сохранение прежней physics geometry. Не совместимые с прежним поведением
   сцены оставлять неизменными с конкретной диагностикой. Не нормализовать
   размеры/offset/Transform скрыто. Не публиковать частично подготовленный World.
5. При успешной публикации remap-ить selection и прочие editor handles по
   SceneIdentity; после identity change не использовать старые Entity handles.
   Проверить New/Open/Save/Save As, Startup Scene validation, Project switching,
   Close Project и Legacy Workspace. Ошибка должна сохранять текущую сессию.
6. Подготовить настоящий Application Play к выбранному author World:
   clone/validation/SpawnPlayer preparation до runtime publication и Lua start.
   Проверить Play/Pause/Resume/Stop, повторный Play и отсутствие изменений author
   World. Закрытый P0.3c harness не заменяет этот интеграционный критерий.
7. После будущих изменений выполнить Release и профильные проверки persistence,
   mode compatibility, rollback, Project/Legacy и Application lifecycle,
   затем полный CTest и DX12 validation. Assertions не ослаблять. До завершения
   A пользовательские Tree/Gizmo/Reparent controls остаются отключёнными.
8. Обновить отчёт блока A с результатами и оставшимися рисками; остановиться
   на этой границе. Следующие UI/interaction блоки не начинать автоматически.

Сохранить ограничения P0.3c: hierarchy interpolation выключена;
dynamic Rigidbody/Character только root; sheared Collider и Collider под
dynamic ancestor запрещены; PlayerStart — root без детей; dynamic collider
root — unit scale и zero offset.

## Последние результаты проверок и источники

Ни сборка, ни CTest в ходе создания checkpoint повторно не запускались.
Ниже результаты предыдущего этапа, а не доказательство готовности P0.3d.
Наличие логов и итоговые строки profile/full проверены чтением файлов.

| Проверка | Последний результат | Источник в рабочей папке |
|---|---|---|
| Release build | PASS | [build log](C:/Users/Тимофей/source/repos/WhispEngine/out/p03d-audit-release-build.log), отчёт P0.3d |
| Profile CTest | 12/12 PASS, 26.01 s | [profile log](C:/Users/Тимофей/source/repos/WhispEngine/out/p03d-audit-profile-ctest.log), [detail](C:/Users/Тимофей/source/repos/WhispEngine/out/p03d-audit-profile-ctest-detail.log) |
| Full CTest | 60/60 PASS, 241.38 s, один запуск | [full log](C:/Users/Тимофей/source/repos/WhispEngine/out/p03d-audit-full-ctest.log), [detail](C:/Users/Тимофей/source/repos/WhispEngine/out/p03d-audit-full-ctest-detail.log) |
| Runtime Hierarchy | CPU 296, DX12 16 checks PASS | profile detail, отчёт P0.3d |
| ECS Hierarchy / Transform | 261 / 718 checks PASS | profile detail, отчёт P0.3d |
| MultiSelection | CPU 2550, DX12 395 checks PASS | profile detail, отчёт P0.3d |
| DX12 | errors 0 по assertions RuntimeHierarchy/MultiSelection; ERROR/CORRUPTION/native assertion markers 0 в логах | profile/full detail, отчёт P0.3d |
| Сохранность baseline после P0.3d | 820/820 файлов, SHA256 без изменений | [исходные хеши](C:/Users/Тимофей/source/repos/WhispEngine/out/p03d-audit-start-hashes.json), отчёт P0.3d |
| git diff --check | PASS после создания checkpoint | текущая проверка этого этапа; существующее CRLF warning Application.cpp не является whitespace error |

Логи в игнорируемом out/ локальные: их необходимо сохранить для воспроизводимого
контекста, они не заменяют отчёты в tests/ и не гарантированы новым checkout.

## Все незакоммиченные изменения, которые необходимо сохранить

На входе checkpoint: **33 modified tracked + 14 untracked**. Ни один из этих
файлов нельзя перезаписывать или удалять при возобновлении. EventBus уже был
изменён до checkpoint; его существующее содержимое также сохраняется.
Не считать весь dirty diff изменениями P0.3d.

Ниже точный входной `git status --short`; пути в этом выводе относительно
указанной выше рабочей папки:

```text
 M CMakeLists.txt
 M engine/CMakeLists.txt
 M engine/core/Application.cpp
 M engine/ecs/ComponentRegistry.cpp
 M engine/ecs/ComponentRegistry.h
 M engine/ecs/LightGather.h
 M engine/ecs/PhysicsQueries.h
 M engine/ecs/PlayerSpawn.h
 M engine/ecs/RenderInterpolation.h
 M engine/ecs/World.cpp
 M engine/ecs/World.h
 M engine/ecs/events/EventBus.h
 M engine/ecs/systems/BoundsBounceSystem.cpp
 M engine/ecs/systems/MotionSystem.cpp
 M engine/ecs/systems/PhysicsSystem.cpp
 M engine/ecs/systems/RenderSystem.cpp
 M engine/ecs/systems/RenderSystem.h
 M engine/editor/EditorGroupTransform.cpp
 M engine/editor/EditorInspector.cpp
 M engine/editor/EditorLayer.cpp
 M engine/editor/EditorScalePlanes.cpp
 M engine/editor/EditorSelection.cpp
 M engine/editor/EditorTransactions.cpp
 M engine/editor/GroupTransform.h
 M engine/editor/LightGizmos.cpp
 M engine/editor/SunSynchronization.h
 M engine/editor/TransformMath.h
 M engine/scene/SceneSerializer.cpp
 M engine/scripting/EntityReference.h
 M engine/scripting/GameplayServices.h
 M engine/scripting/LuaRuntime.cpp
 M engine/scripting/Prefab.cpp
 M tests/Editor2Regression.cpp
?? engine/ecs/Hierarchy.cpp
?? engine/ecs/Hierarchy.h
?? engine/ecs/HierarchyPhysics.h
?? engine/ecs/TransformAccess.h
?? engine/ecs/TransformMath.h
?? engine/ecs/components/RelationshipComponent.h
?? tests/EcsHierarchyRegression.cpp
?? tests/ROADMAP_P03A_TRANSFORM_CONTRACT_REPORT.md
?? tests/ROADMAP_P03B_ECS_HIERARCHY_REPORT.md
?? tests/ROADMAP_P03C_RUNTIME_HIERARCHY_REPORT.md
?? tests/ROADMAP_P03D_EDITOR_HIERARCHY_REPORT.md
?? tests/ROADMAP_P03_HIERARCHY_REPORT.md
?? tests/RuntimeHierarchyRegression.cpp
?? tests/TransformContractRegression.cpp
```

Единственное добавление текущего Safety Checkpoint:
`tests/ROADMAP_RESUME_CHECKPOINT.md`; итоговый status должен иметь
**33 modified + 15 untracked**. Сохранять и этот документ.

Production-код, тесты, пользовательские Lua/сцены/модели, EventBus и
конфигурация этим checkpoint не изменяются. Git index/history не изменяются.
Commit, push, merge, rebase, reset и clean не выполнялись и не разрешены
данным checkpoint.
