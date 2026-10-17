# P0.3d — Hierarchy Editor Integration

**Verdict: INCOMPLETE — остановка после аудита блока A.**

Ветка `project_system`. Применено условие остановки из разделов 3 и 14
задания: выявлена несовместимость контрактов старых сцен и HierarchyData,
которую нельзя устранять простым переключением Editor World. Публичный
hierarchy workflow не активирован. Исходники и существующие тесты в этом этапе
не изменялись; добавлен только этот отчёт. P0.3a/b/c остаются текущим baseline.

## Проверенные границы

Изучены отчёты P0.3, P0.3a, P0.3b, P0.3c и P0.2, а также связанные пути
Application/ApplicationProject, SceneSerializer, World, HierarchyGraph,
TransformAccess, ComponentRegistry, EditorLayer, EditorUi, EditorSelection,
EditorGroupTransform, EditorTransactions, SelectionSet и GroupSession.

### 1. Режим World меняет физический смысл тех же компонентов

В RootOnly Collider halfExtents и offset имеют прежний мировой смысл;
Transform.scale не масштабирует размеры Collider. В HierarchyData
`ResolveHierarchyCollider` умножает halfExtents на абсолютный world scale,
а offset преобразует полной линейной частью world matrix. Это действующий
контракт P0.3c, а не недостающая UI-проверка.

Минимальный численный контрпример: root Entity с Transform.scale=(2,2,2),
статическим Rigidbody и BoxCollider.halfExtents=(0.5,0.5,0.5), offset=0.
При неизменных компонентах и Transform мировой полуразмер в RootOnly равен
0.5, а в HierarchyData — 1.0. Обе конфигурации допустимы для своего режима.
Проверка только graph/runtime compatibility не обнаружит изменение поведения.
Это вывод непосредственно из формул, отдельный исполняемый probe не добавлялся.

Для dynamic Rigidbody с таким же масштабом конверсия дополнительно
отклоняется: P0.3c требует unit scale и нулевой offset. Sphere с неравномерным
масштабом также получает новое ограничение. Collider без Rigidbody начинает
участвовать в hierarchy solver как immovable geometry.

Следовательно, перенос всех компонентов и stable IDs в новый World,
сохранение root TRS и успешная ValidateHierarchy сами по себе не означают
совместимость прежней сцены. Молчаливая правка размеров Collider или Transform
нарушила бы требование сохранения компонентов; ослабление P0.3c guards также
не является допустимым решением.

### 2. Schema v4 не различает два режима для плоской сцены

`SceneSerializer::CaptureWorld` записывает version/settings/entities;
DataMode в документе отсутствует. Relationship необязателен, поэтому
HierarchyData World, в котором пока только roots без Relationship, может
сохраниться в тот же JSON, что и RootOnly World с теми же компонентами.
`RestoreWorld` выбирает режим staged World по режиму принимающего World.

Автовыбор режима по наличию Relationship неоднозначен после New Scene,
до первого parenting и после удаления последней связи. Он не гарантирует
сохранение физического поведения при Save/Load. Всегда выбирать HierarchyData
меняет старые flat scenes; всегда выбирать RootOnly закрывает новые hierarchy
scenes. Нужен явный сохраняемый контракт режима и политика старых файлов.

### 3. Application объединяет authoring data и владельца систем

`Application::m_World` — value member с default RootOnly. Он одновременно
хранит Editor data и владеет PhysicsSystem/RenderSystem и GPU caches.
GetEditorWorld возвращает именно его. Фазы обновления запускаются через
системы m_World на текущем GetWorld; CloneData не клонирует системы.

DataMode фиксируется при создании World. ReplaceData сохраняет системы,
меняет identity и намеренно запрещает перенос между разными DataMode.
Снятие этого guard или замена const DataMode недопустимы.

OpenScene, OpenProject, CloseProject и проверка Startup Scene создают
RootOnly candidates. CommitProject активирует ProjectContext после очистки
ресурсов, а затем вызывает ReplaceData. Простая подмена candidate на
HierarchyData даст ошибку режима уже после разрушительных для текущей
сессии переходов. Требуется подготовленная публикация author World отдельно
от владельца систем и перенос связанных handles по stable IDs.

Настоящий SetEditorPlayMode публикует Runtime World и переключает Editor
history/selection до SpawnPlayer и LuaRuntime::Start. Для hierarchy необходимо
сначала подготовить и проверить candidate, затем атомарно публиковать runtime
session. Закрытый harness P0.3c не доказывает корректность этого Application пути.

## Почему реализация остановлена

Проблема не требует переписывать solver или renderer. Однако безопасное
включение требует согласованного изменения трёх контрактов: владение author
World в Application, сохранение режима сцены и явная совместимость старой
physics geometry. Это затрагивает New/Open/Project/Startup/Play/Stop до UI.
В рамках условия остановки эти изменения не маскируются частично работающим
деревом или обходом guards. Это техническая граница текущего этапа, а не
утверждение, что полноценная интеграция невозможна.

## Техническое разделение оставшейся работы

1. **Закончить блок A отдельно.** Отделить владельца систем от заменяемого
   author World минимальным изменением Application API. Ввести явное
   сохранение режима в v4 с обратной совместимостью: старые документы без
   признака сохраняют RootOnly семантику, новые сцены имеют HierarchyData.
   Подготовить checked upgrade: копия всех компонентов/IDs, graph/runtime
   validation и проверка сохранения прежней physics geometry. Несовместимые
   сцены оставлять в прежнем режиме с конкретной диагностикой; не исправлять
   пользовательские компоненты молча. Проверить атомарный Open/Project switch
   и реальный Application Play до публичного включения.
2. **Блок B.** Построить flattened preorder из HierarchyGraph, editor-only
   expansion, ancestor-preserving search и range selection по видимым строкам.
   Multi-reparent/unparent выполнять на candidate для selected roots, с
   KeepWorld и runtime validation, затем публиковать одну transaction.
3. **Блок C.** Ввести Local Inspector и checked World diagnostics. Для single
   и GroupSession использовать effective roots, сохраняя explicit selection;
   проверять весь transform candidate до записи, включая snapping и shear.
4. **Блок D.** Перевести snapshots на closure и атомарный batch restore.
   Использовать PrepareHierarchyClone, parent/Lua ID remapping, отмену всего
   clone closure, один Undo и восстановление selection/history handles после
   смены World identity. Затем native UI/input и полный Application lifecycle.

Это разделение относится только к P0.3d; перехода к P0.4 нет.

## Фактическое состояние пользовательских операций

| Область | Состояние после этого этапа |
|---|---|
| Activation / old flat scenes | Прежний RootOnly Editor; hierarchy не активирована, конверсия не выполняется. |
| New / Open / Save / Save As / Startup / Project / Legacy | Существующий workflow сохранён. Обычный Editor по-прежнему не открывает Relationship scenes. |
| Tree / Collapse / Search / DND | Новое дерево, reparent и root drop-zone не реализованы. |
| Inspector / Gizmo / selected roots | Существующие root-only controls; parent-aware workflow не заявляется. |
| Alt+Drag / Delete / Undo / Redo | Существующий P0.2 root-only workflow. Subtree snapshots и атомарный hierarchy restore ещё не интегрированы. |
| Application Play | Существующий root-only путь. Реальный Application hierarchy Play не проверен и не объявлен готовым. |
| Persistence | Внутренние HierarchyData graph roundtrip из P0.3b/c сохранены; публичный Editor roundtrip не реализован. |

Ограничения P0.3c не изменены: hierarchy interpolation отключена; dynamic
Rigidbody/Character только root; sheared Collider и Collider под dynamic
ancestor запрещены; PlayerStart должен быть root без детей; dynamic collider
root требует unit scale и zero offset.

## Верификация

Результаты ниже относятся к неизменённому baseline, **не к готовности P0.3d**.
Новые Editor Hierarchy UI tests не добавлялись: соответствующий workflow не
включён и его нельзя объявлять проверенным через внутренние helpers.

- Release build: PASS (`out/p03d-audit-release-build.log`).
- Профильные тесты: **12/12 PASS**, 26.01 s
  (`out/p03d-audit-profile-ctest.log`, подробности в файле с суффиксом `-detail`).
  ECS Hierarchy, Runtime Hierarchy CPU/DX12, Transform Contract,
  MultiSelection CPU/DX12, ScalePlaneGizmo, Project Core/System/Settings
  CPU/DX12. Runtime CPU: 296 checks, Runtime DX12: 16, ECS Hierarchy: 261,
  Transform Contract: 718, MultiSelection CPU: 2550, DX12: 395.
- Полный CTest: **60/60 PASS**, 241.38 s, один запуск, без повторов
  (`out/p03d-audit-full-ctest.log`, `out/p03d-audit-full-ctest-detail.log`).
  Включает Lua, Physics, Rendering и существующие native Editor input tests.
- DX12 validation: RuntimeHierarchy/MultiSelection DX12 assertions на нулевой
  GetValidationErrorCount прошли. Поиск по profile/full detail logs:
  D3D12 ERROR/CORRUPTION и native assertion markers — **0**.
- Финальная SHA256-проверка после полного CTest: все **820/820** исходных
  tracked/ранее untracked файлов совпадают со снимком до этапа.
- `git diff --check`: PASS (Git отдельно предупреждает о существующем CRLF
  в Application.cpp; файл этим этапом не изменялся).

## Изменённые файлы / Git

Единственный новый файл P0.3d:
`tests/ROADMAP_P03D_EDITOR_HIERARCHY_REPORT.md`.
Диагностические логи и снимок SHA256 baseline находятся в игнорируемом `out/`.
Dirty status до этапа сохранён в `out/p03d-audit-start-status.txt`, хеши tracked
и ранее untracked файлов — в `out/p03d-audit-start-hashes.json`.
Финальный полный status сохранён в `out/p03d-audit-final-status.txt`:
он содержит прежние 33 modified и 13 untracked файлов плюс этот новый отчёт;
содержимое всех прежних файлов сохранено, что подтверждено SHA256.

Исходные незакоммиченные изменения P0.3a/b/c, включая ранее изменённый
EventBus, не относятся к правкам этого этапа. Пользовательские Lua, сцены,
модели и EventBus не редактировались. Git index/history не изменялись;
commit/push/merge/rebase/reset/clean не выполнялись.

Ручная проверка нового дерева невозможна: пользовательская hierarchy остаётся
закрыта. Автоматические DX12 baseline tests не заменяют её.
