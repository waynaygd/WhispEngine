# P0.3d-C1 — Hierarchy Transform Inspector

Ветка `project_system`, 2026-10-18. Финальные результаты и verdict — ниже.
Область — controlled HierarchyData C1, без C2/Gizmo/публичной activation.

## Аудит и архитектура

Прочитаны B2/B2.1/B1/P03c/A3 reports и связанные EditorLayer/HierarchyTree,
RootOnly Inspector, SelectionSet/EditorSelection, EditorTransactions,
EditorHierarchyTransactions/HierarchyEdit, HierarchyGraph/TransformAccess/Math,
World/TransformComponent, SceneSettings/SunSynchronization/runtime geometry.
Последний baseline B2.1: full 69/69, DX12 0; исторические native input issues
остаются техническим долгом, их bounded diagnostics сохранены.

RootOnly Transform registry properties читают/пишут World space, старый drawer
включает component mutations и последовательные transactions. Поэтому он не
подключается к H. Переиспользованы формат precision, Degrees/Radians, ECS
математика, selection и существующий B2 history. RootOnly Inspector.cpp и
EditorUi.cpp не менялись. Новый `Hierarchy Transform` — отдельная production
панель EditorLayer в прежней guarded H branch после Hierarchy Tree.

## Local editing и World display

Показаны имя, SceneIdentity и Parent name/Scene Root. Редактируются только Local
Position/Rotation/Scale XYZ. Rotation отображается в degrees и переводится в
исходные authored Euler radians только при фактическом изменении значения.
Convention Rz*Ry*Rx не менялась. Лейблы находятся над тремя полями, полный
доступный width отдаётся DragFloat3; длинные metadata/messages переносятся.

HierarchyTransformView содержит только values. Matrix берётся из свежего
HierarchyGraph; World Position — translation [12..14] полной affine matrix,
без TRS decomposition. World rotation/scale показываются только после
существующего checked DecomposeTRS с authored sign preference. Shear/singular/
неподдерживаемый TRS range дают понятное сообщение об отсутствии lossless World
TRS. Приближённого decomposition, записи World TRS в Local или нового cache нет.
World fields всегда read-only.

## Atomic validation и Sun/Physics

EditHierarchyLocal требует одну выбранную H Entity в idle Edit, finite Local
values в persisted numeric range +/-1e6. No-op сравнивает все девять authored
значений, не создаёт history и не меняет dirty state.

Перед записью подготовлены value snapshot/selection, UndoRecord и stack capacity.
Затем используется существующий HierarchyEdit::Apply: temporary CloneData,
полный итоговый graph, RequireRuntimeWorld, SceneSettings/persistence validation,
и только после успеха nonthrowing graph-storage/Transform commit. Relationship,
stable ID, slots, World identity и адреса Transform сохраняются. Проверяется
весь World, включая весь affected subtree, и finite world matrices в renderer
range +/-1e8. Collider не нормализуется/не мигрируется.

P03c policy сохранена: collider shear/singular geometry, dynamic ancestor,
child dynamic body/Character, dynamic root unit-scale/zero-offset, Sphere
uniform absolute scale, physics range и PlayerStart topology проверяются
прежним runtime validator. Для недопустимой topology, которую Local TRS сам по
себе создать не может, tests также проверяют отказ при injected invalid source.

A3 sun ancestry/orientation validation выполняется на disposable probe. C1
дополнительно отвергает SkyDrivesLight edit, если probe потребовал бы изменить
authored sun Transform: такая коррекция иначе появилась бы вне Local Undo
record на следующем кадре. Поэтому конфликтующая rotation Parent/driven sun
отклоняется, совместимая translation разрешена. SceneSettings, light properties
и остальные Entity не записываются. Это консервативная C1 policy; B2 KeepWorld
validation и SunSynchronization не изменены.

## Transactions и lifecycle

UI — deferred value draft. Drag/typing не пишет ECS и не создаёт history каждый
кадр. Release/Enter или завершение поля по уходу focus применяет один checked
candidate и одну existing Undo transaction. Невалидный draft отклоняется и
сбрасывается; Escape/сворачивание панели отменяет draft без ECS/history writes.
До завершения поля World display показывает текущий авторитетный World, а hint
явно сообщает о pending Local edit. Live preview в C1 не реализован.

Draft хранит проверяемый Entity handle, stable ID, исходный Local/Relationship
snapshot, edited value и widget ID, без component pointers. При изменении
selection/World/baseline/relationship он не может записаться в новую Entity.
Numeric widget IDs зависят от World identity и SceneIdentity. History operations
инвалидируют активный numeric input; ResetHistory/publication/Project/Play/Stop
сбрасывают draft. Обычная смена selection завершает старое поле при потере focus
либо отменяет уже не относящийся к selection draft, не изменяя новую Entity.

Нового Undo framework нет. EntitySnapshot расширен Local-policy flag, хранит
те же HierarchyEditState records; Undo/Redo используют существующие stacks,
revisions, saved revision и HistoryContext. Local snapshots дополнительно
проходят C1 validation (включая текущие sun settings). Отказ Undo сохраняет
record и весь World. B2 records сохраняют прежний validator/KeepWorld semantics.
Mixed Transform → Reparent → Transform и repeated Undo/Redo восстанавливают
точные authored values, ordering, optional Relationship и selection.

При multi-selection показывается active Entity read-only; Parent+Child explicit
selection остаётся прежней. В Play/Pause поля disabled, service отказывает;
Stop возвращает author selection/history без сохранения незавершённого draft.

## Граница UI

Доступны B1 Tree/selection, B2 reparent/root drop, C1 Local Inspector и общая H
Undo/Redo. Старые Inspector component edits, Gizmo, Delete/Duplicate/Alt+Drag,
asset mutation, generic legacy shortcuts/context menus не включены.
Публичные New/Open H guards A3 сохранены; tests используют существующий private
controlled production pathway. A1 schema v4, A2 ownership, A3 lifecycle,
HierarchyEdit и пользовательские assets/Lua/scenes/EventBus не менялись.

## Проверки по блокам

| Блок | Результат / журналы out/ |
| --- | --- |
| A — audit baseline | 3/3 PASS, 1.43 s; p03dc1-a-{ctest,detail}.log |
| B — checked service/history | Release target build PASS; 3/3 PASS, 1.62 s; C1 68 CPU, B2 176, Transform 718; p03dc1-b-build.log, p03dc1-b-{ctest,detail}.log |
| C — реальный CPU UI | build PASS; 3/3 PASS, 1.59 s; p03dc1-c-build.log, p03dc1-c-{ctest,detail}.log |
| D — native UI/lifecycle | build PASS; 6/6 PASS, 20.57 s; C1 123 CPU/376 DX12; B1/B2 unchanged; p03dc1-d-build.log, p03dc1-d-{ctest,detail}.log |

Новые CPU tests проверяют root/child/grandchild composition, negative/nonuniform
scale, valid affine shear и checked display, numeric/runtime/sun refusals,
exact undo, mixed history и v4 roundtrip. Native tests используют настоящий
Application/EditorLayer/ImGui с queued mouse/key/text events. B1 selection,
DragFloat3 Ctrl-click input и continuous drag, one transaction, Escape/no-op/
invalid field, multi-selection readonly, selection switch, B2 real drag/drop,
numeric refresh, mixed Undo, Play/Pause/Stop, pending draft publication/project
switch и production SaveSceneAs/Open проверяются через реальные handlers.

GPU readback проверяет изменение более 20 geometry pixels **вне** Tree/Inspector
windows после Parent Local edit (x>=800). Это не сравнение текста полей или
только счётчика draws. Проверяются actual draws и DX12 validation.

Исходный snapshot: `out/p03dc1-start-hashes.json`,
`out/p03dc1-start-status.txt` (843 файла). Все прежние dirty изменения сохранены.

## Изменённые файлы C1

Существующие файлы:

- `CMakeLists.txt` — CPU/DX12 test targets.
- `engine/CMakeLists.txt` — два production source файла C1.
- `engine/editor/EditorLayer.h` — draft state, Local transaction API и history policy flag.
- `engine/editor/EditorLayer.cpp` — вызов Inspector в controlled H branch.
- `engine/editor/EditorTransactions.cpp` — отмена draft при runtime transitions.
- `engine/editor/EditorHierarchyTransactions.cpp` — checked Local commit и mixed history validation.
- `engine/editor/EditorHierarchyTree.cpp` — общие подписи Undo/Redo.

Новые файлы:

- `engine/editor/HierarchyTransformInspector.h`.
- `engine/editor/HierarchyTransformInspector.cpp`.
- `engine/editor/EditorHierarchyTransformInspector.cpp`.
- `tests/EditorHierarchyTransformInspectorRegression.cpp`.
- `tests/ROADMAP_P03D_C1_TRANSFORM_INSPECTOR_REPORT.md`.

Прежние dirty файлы не включены в этот перечень как изменения C1. Их исходный
список сохранён в `out/p03dc1-start-status.txt`; итоговый —
`out/p03dc1-final-status.txt`. Сравнение SHA256 843 исходных файлов:
missing 0, unexpected modifications 0; изменены ровно семь перечисленных
существующих файлов. Все 90 файлов assets/EventBus совпадают с исходным snapshot.
Application, существующие regression sources, HierarchyEdit, serializer и
остальные прежние изменения сохранены byte-for-byte относительно старта C1.
Источник: `out/p03dc1-final-integrity.log`.

## Ограничения и остаточные риски

- C1 работает только в controlled HierarchyData pathway; публичная activation
  остаётся закрытой A3 guards. C2 не начат.
- Редактируется одна Entity; multi-selection только read-only. World поля
  read-only. Непредставимый shear не преобразуется в приблизительный TRS.
- Deferred commit не даёт live preview во время незавершённого drag/typing.
  Авторитетный World и geometry обновляются после завершения поля.
- Консервативный Sun guard может отказать в ancestor rotation при Sky Drives
  Light. Он предотвращает скрытое изменение sun вне Local undo record.
- Проверены реальные ImGui/native inputs, Application transitions и GPU pixels.
  Отдельная ручная проверка DPI, длительный soak и пользовательские проекты
  в этот этап не входили.
- Исторические intermittent RMB/keyboard native input issues остаются
  техническим долгом. Успешные прогоны не доказывают устранение их root cause;
  существующие bounded traces и assertions не изменены.

## Финальная верификация — READY

После последних source edits выполнен all-target Release build: PASS
(`out/p03dc1-verified-release-build.log`; предыдущий полный build также PASS,
`out/p03dc1-final-release-build.log`). После этого исходники не менялись.

| Проверка | Итог | Источник |
| --- | --- | --- |
| Финальный C1 CPU/DX12 | 2/2 PASS, 6.73 s; 142 CPU / 419 DX12 checks | `out/p03dc1-final-local-ctest.log`, `out/p03dc1-final-local-detail.log` |
| Обязательный широкий профиль | 31/31 PASS, 135.32 s | `out/p03dc1-profile-ctest.log`, `out/p03dc1-profile-detail.log` |
| Единственный полный CTest Release | 71/71 PASS, 0 failed, 338.68 s | `out/p03dc1-full-ctest.log`, `out/p03dc1-full-detail.log` |
| DX12 error/corruption markers | 0 в финальном C1, профиле и полном CTest; native validation assertions PASS | `out/p03dc1-validation-check.log`, detailed logs |
| `git diff --check` | PASS, exit 0 | `out/p03dc1-diff-check.log` |
| Сохранность baseline | 843 проверены; missing 0; unexpected modified 0 | `out/p03dc1-final-integrity.log` |

В profile и full PASS: C1 CPU/DX12, B2 Reparent CPU/DX12, B1 Tree CPU/DX12,
Application Activation/Ownership CPU/DX12, SceneWorldMode, ECS Hierarchy,
Runtime Hierarchy CPU/DX12, Transform Contract, Project Core/System/Settings,
Editor2/LevelWorkflow/Workflow, MultiSelection CPU/DX12, ScalePlaneGizmo,
LuaGameplayController CPU/DX12. Полный suite дополнительно сохраняет остальные
RootOnly/asset/render/runtime regressions. Число тестов выросло 69 → 71 только
за счёт двух новых C1 targets.

В полном suite: C1 142/419 checks; B2 176/294; B1 74/308;
SceneWorldMode 406; Transform Contract 718; ECS Hierarchy 261;
Runtime Hierarchy 296 CPU / 16 DX12. LuaGameplayControllerDx12Regression
прошёл в profile (19.84 s) и единственном full (20.77 s). Исторические input
failures в этом этапе не воспроизвелись; нового failure trace нет, прежняя
bounded диагностика оставлена без изменений. Автоматических повторов,
skip, ослабления assertions и установки camera state ради PASS не было.
Native C1 camera pose используется только как детерминированная render fixture.

Вердикт **READY для завершения P0.3d-C1**: безопасный authored Local editing,
lossless World display, atomic validation, общий B2+C1 Undo/Redo, настоящие
UI/native/render tests и прежние regressions подтверждены текущим baseline.
Это не утверждение об устранении исторической input instability и не разрешение
публичной H activation. P0.3d-C2 не начат. Commit/push/merge/rebase/reset/clean
не выполнялись.
