# P0.3d-B2 — Hierarchy Reparent & Unparent

Дата: 2026-10-18. Ветка `project_system`.
**Verdict: INCOMPLETE — полный baseline не зелёный (68/69).**
Реализация B2 и профильные/native проверки прошли; единственный полный CTest
отказал в существующем RootOnly native controller keyboard-input сценарии.
Публичная hierarchy activation остаётся закрытой. C/D не начинались.

## A — Transaction audit

Прочитаны отчёты B1/A3/A2/P03b/P03c, EditorHierarchyTree/HierarchyTree,
EditorSelection/SelectionSet, EditorTransactions/EditorGroupTransform,
HierarchyGraph/SetParent/ClearParent, ComponentRegistry Restore/RestoreBatch,
ApplicationWorld, SceneSerializer и World storage/lifecycle.

Существующий RestoreSnapshot восстанавливает Entity последовательно через
ComponentRegistry::Restore. Undo сначала вынимает запись, потом создаёт inverse,
восстанавливает компоненты и помещает запись в другой stack. Этот путь нельзя
применять к H batch: он не обеспечивает атомарную проверку итогового графа.
RestoreBatch staging безопасен для persistence, но ReplaceData меняет identity;
Application publication очищает selection/history. Для одного drop это неверно.

Выбрано узкое расширение существующего snapshot/UndoRecord, а не второй Undo
framework. Value snapshot хранит только touched local Transform и optional
Relationship (включая отсутствие), stable ID и исходный Entity/World/generation.
История и selection подготавливаются до авторитетных записей.

HierarchyEdit строит временный CloneData, применяет весь batch и проверяет
итоговый graph/runtime и scene constraints. Relationship storage выделяется
заранее; его swap и присваивания существующим Transform выполняются без
аллокаций/callbacks на owner-thread вне iteration/lifecycle. World, slots,
SceneIdentity, остальные storage и адреса Transform сохраняются. Relationship
references являются краткоживущими и после graph edit должны запрашиваться заново.

Входной SHA-256 snapshot: `out/p03db2-start-hashes.json`; исходный status:
`out/p03db2-start-status.txt`. Все предыдущие dirty изменения сохраняются.
Блок A baseline: 3/3 PASS, 1.31 s; `out/p03db2-a-{ctest,detail}.log`.
Первая попытка запуска не нашла ctest без VS environment; тесты не запускались.
После инициализации VS выполнен один узкий baseline выше.

## B/C — Graph preparation и existing Undo model

HierarchyEdit::PrepareReparent принимает explicit source set, проверяет handles
и target. Итеративный preorder помечает selected ancestors: в move попадают
только selected effective roots, в deterministic graph order. Explicit selection
не переписывается; descendants не получают повторную delta. Target внутри
selected subtree запрещён до preparation. Для unselected drag source UI фиксирует
только этот handle, независимо от остальных выбранных Entity.

На disposable candidate используются существующие checked SetParent/ClearParent.
Они вычисляют inverse(parent world) * old world, checked DecomposeTRS и append
order. Для Unparent local = old world. Shear/singular local не округляется и не
аппроксимируется. Дополнительно сравниваются полные world matrices всех Entity
до/после подготовки (relative tolerance 2e-5); descendants и unrelated nodes
включены. Допустимый affine world shear сохраняется, если новый local — TRS.
No-op исключается до записи, не меняет order, revisions или history. При order
exhaustion — отказ, sibling renumbering не реализован.

Apply независимо строит финальный candidate из snapshot records, проверяет
identity/generation/stable ID каждого touched Entity, итоговые graph/runtime
constraints и settings. Runtime policy — существующий RequireRuntimeWorld
(P03c), без ослабления Collider/Character/PlayerStart guards. Editor validation
также сохраняет A3 запрет legacy mesh auto-fit. Exact A3 sun synchronization
проверяется на дополнительной disposable копии: unsupported ancestry и
orientation/runtime constraints вызывают отказ, author Transform, light
properties и SceneSettings не корректируются скрыто.

В EntitySnapshot добавлен optional hierarchy value snapshot; используются те же
UndoRecord, stacks, revision/saved revision и HistoryContext. Графовый путь не
вызывает последовательный legacy RestoreSnapshot. До Apply готовятся inverse,
selection copy, label/record и capacity destination stack. При отказе исходная
запись остаётся на месте, World и оба history stacks сохраняются. При успехе
nonthrowing moves завершают одну transaction. Undo/Redo повторно проходят тот же
checked candidate путь и восстанавливают точные authored values/order/absence.

Ни entity creation/destruction, ни ReplaceData/RestoreBatch, ни Application
publication для drop не используются. World identity/slots/generations и stable
IDs сохраняются, Lua references и прочие компоненты не записываются. Ранее
удержанные Relationship pointers не поддерживаются после graph edit; Editor
хранит value graph snapshots, H Inspector/Group/Gizmo закрыты. Все Transform
addresses сохраняются. Generic NotifyEditorMutation не вызывается: он будит
Rigidbody и меняет sleeping fields; KeepWorld этого не требует. Авторская H
physics simulation не исполняется, Play готовит независимый runtime.

## D — Реальный ImGui wiring и lifetime

B1 production TreeNode rows стали настоящими BeginDragDropSource/Target.
Отдельная выделенная кнопка Scene Root — root target. Payload содержит только
проверяемый Entity с World/generation (без указателей); frozen drag source set
привязан к World identity. Resolve/validation выполняются снова при delivery.
Hover делает только дешёвую проверку handles/selected ancestry, без CloneData
или runtime validation. Highlight — preliminary admission; tooltip сообщает,
что final constraints проверяются на drop. Refusal reason доступен в footer
(hover), включая ID из graph/runtime/sun diagnostics.

Click selection фиксируется при отпускании на исходной строке, source activation
её отменяет. Поэтому выбранный Parent+Child остаётся explicit selection; начало
drag unselected row не заменяет selection. Arrow clicks остаются selection-free.
Esc очищает payload и блокирует повторный source до отпускания мыши. Left/Right
во время drag сворачивают/раскрывают source; Ctrl+F активирует реальный search
InputText. ImGui SetKeyboardFocusHere намеренно игнорирует DnD, поэтому для
этого явного запроса используется существующий ActivateItemByID. Собственного
text input механизма нет. Collapse/search не меняют frozen payload и ECS.

Scene/project publication и Enter/LeaveRuntime очищают pending source/click;
на следующем H UI pass устаревший ImGui payload очищается. Старая доставка не
может примениться к новому World даже при одинаковых stable IDs. Play/Pause
отключают source/targets/graph Undo/Redo; service также проверяет Edit session.
Stop использует прежний HistoryContext и возвращает author selection/history.

В H ветке добавлены только graph Undo/Redo buttons и Ctrl+Z/Y (когда panel
focused, не text input и не drag). Inspector, Gizmo, component/asset mutations,
Delete, Duplicate, Alt+Drag, context menus и legacy shortcuts остаются закрытыми.
RootOnly EditorUi не менялся; публичные New/Open H workflows не включены.

## Проверки по блокам

| Блок | Результат / журналы в out/ |
| --- | --- |
| A — audit baseline | 3/3 PASS, 1.31 s; p03db2-a-{ctest,detail}.log |
| B — checked service | Release PASS; 3/3 PASS, 1.03 s; 52 new CPU checks; p03db2-b-build.log, p03db2-b-{ctest,detail}.log |
| C — existing Undo integration | Release PASS; 3/3 PASS, 1.53 s; 75 new CPU checks; p03db2-c-build.log, p03db2-c-{ctest,detail}.log |
| D — первая UI проверка | 2/4 PASS, 9.50 s; new CPU/native fixture expected hidden child; p03db2-d-{ctest,detail}.log |
| D — диагностика | CPU failure with exact visible IDs/undo count; p03db2-d-diagnose-{ctest,detail}.log |
| D — interactions | 2/4 PASS, 8.91 s; ImGui ignores focus request during drag; p03db2-d-interaction-{ctest,detail}.log |
| D — final search/collapse wiring | 4/4 PASS, 10.94 s; new 152 CPU / 261 DX12, old Tree 74 / 308; p03db2-d-search-{ctest,detail}.log |

Новые UI tests используют production EditorLayer/Application и SyntheticEditorInput
(реальные queued ImGui mouse/key/text events), не заменяют native drop прямой
Relationship записью. Fixture setup — отдельные owned Worlds/Project directories.
CPU helper checks не заменяют native Application Initialize/Run/DX12.

Первый D failure объяснён быстрыми повторными кликами по label: штатный ImGui
double-click сворачивал source и скрывал child target; в диагностике видны
`target,other,a,d` и undo=3. Теперь collapse/expand во время drag проверяются
явными Left/Right events, без предположения, что source обязательно раскрыт.
Отдельный search failure выявил документированное ограничение ImGui
SetKeyboardFocusHere во время DnD. Исправлен явный production Ctrl+F handler
через ActivateItemByID. Assertions не ослаблены; первоначальные журналы сохранены.
Повторов until-pass, sleeps, skip и изменений camera/input production нет.

Обязательный профиль: **28/28 PASS, 117.11 s**,
`out/p03db2-final-profile-{ctest,detail}.log`. Включает Reparent/Tree,
Application Activation/Ownership, SceneWorldMode, Runtime/ECS Hierarchy,
Transform Contract, MultiSelection, Project Core/System/Settings, Editor2,
Editor Workflow/LevelWorkflow CPU/DX12 и LuaGameplayController CPU/DX12.
В профиле Reparent — 168 CPU / 261 DX12; остальные production sources после
этого профиля не менялись.

Дополнены только новые tests: nested Parent+Child+Grandchild selection/effective
roots, Character root policy и настоящий Application SaveSceneAs/OpenScene
на owned fixture после native drop. Финальная Release: PASS,
`out/p03db2-verified-release-build.log`. Финальные Reparent tests:
**2/2 PASS, 5.29 s; 176 CPU / 294 DX12 checks**,
`out/p03db2-coverage-{ctest,detail}.log`. После этого исходники не менялись.

## Изменённые файлы относительно начала B2

Существовавшие (семь):

- `CMakeLists.txt`, `engine/CMakeLists.txt` — service/Editor source и CPU/DX12 CTest.
- `engine/ecs/World.h` — только narrow friendship для HierarchyEdit storage commit.
- `engine/editor/EditorLayer.h` — snapshot extension и transient drag state.
- `engine/editor/EditorTransactions.cpp` — H dispatch и runtime drag invalidation.
- `engine/editor/EditorProject.cpp` — transient drag reset.
- `engine/editor/EditorHierarchyTree.cpp` — actual source/targets/root/graph controls.

Новые (пять):

- `engine/ecs/HierarchyEdit.h`, `HierarchyEdit.cpp`.
- `engine/editor/EditorHierarchyTransactions.cpp`.
- `tests/EditorHierarchyReparentRegression.cpp`.
- Этот отчёт.

Application/A1 persistence/A2 ownership, HierarchyGraph/P03c physics guards,
SunSynchronization, старые test sources/assertions и пользовательские файлы
не редактировались в B2. Состояние начальной ветки и все предыдущие dirty
изменения сохранены; семь перечисленных файлов дополнены поверх baseline.

## Остаточные ограничения

- Область реализации — только controlled H B2, не завершение P0.3d или публичный
  hierarchy workflow. C/D, Tree sibling reorder, Inspector/Gizmo, subtree
  Delete/Duplicate/Prefab и другие Roadmap задачи не начинались.
- Подготовка drop/Undo/Redo использует transient full CloneData; при sun driving
  есть дополнительная probe copy. Это стоимость delivery/history, не hover.
  Длительный performance soak больших authoring scenes не проводился.
- Внешние native consumers обязаны запрашивать Relationship references заново
  после graph commit; World/Entity/Transform addresses не меняются. Graph API
  не является механизмом одновременной mutation/read с произвольных threads.
- Автоматические native tests используют настоящий Application/ImGui/DX12 и
  queued mouse/key/text input, но длительный ручной UX/DPI/multi-monitor soak
  не проводился. Исходный Controller native focus test выполняется стандартно.
- Историческая intermittent RMB issue остаётся техническим долгом; успешные
  прогоны B2 не доказывают окончательное устранение её root cause. Camera/input
  production code и bounded native trace не менялись.

## Единственный полный CTest и native input diagnosis

**68/69 PASS, 344.80 s, exit 8**, без repeat/retry/skip.
Источники: `out/p03db2-full-ctest.log`, `out/p03db2-full-detail.log`.
Полный block отказавшего native test, включая весь имеющийся trace:
`out/p03db2-native-input-failure.log`. После этого никаких tests/builds или
исправлений input не выполнялось.

Единственный failed test: `LuaGameplayControllerDx12Regression` (13.62 s).
Точный failing assertion:

`keyboard alternative moves and turns without capture`

Источник: `tests/LuaGameplayControllerRegression.cpp:270`. Это conjunction:
camera yaw > previous yaw, desiredDirection length > .9, cursor == NORMAL.
Непосредственно перед ним test после focus recovery/text-edit capture release
меняет script на KeyboardTurnController, отправляет native E/W down и выполняет
непрерывные 20 кадров. Какие именно члены conjunction отказали, существующий
журнал не записал. Значения failing-phase focus/camera/viewport/ownership также
не записаны. **Root cause и первый blocker именно keyboard phase не доказаны.**

Это не отказ assertion `Play without player retains RMB free-fly`: он и
continuous-trace completeness check в этом запуске прошли. BufferedRmbTrace
живёт до конца функции и печатается при любом последующем exception; поэтому
появление RMB dump не означает, что причиной был RMB.

Сохранённый bounded trace: **20 continuous frames, 23 records, overflow=0,
truncated=0**. Три pre-state records и все 20 hold samples сохранены, без
добавленных events, sleeps, per-frame I/O или изменения исходного Run(20).

| Фаза / frames | Наблюдение |
| --- | --- |
| edit-ready 706 / play-before-press 710 | focused=1, foregroundOwn=1, focusLost=0; camera=0, RMB up; firstBlock=input-owner до нажатия — ожидаемый pre-state |
| sent-down 710 | nativeRmb=1, nativeW=1, captureOwn=1, firstBlock=eligible |
| hold-before-ui 711–730 | play=1, paused=0, players=0, starts=0; focused=1, focusLost=0, foregroundOwn=1, foregroundPid=selfPid=32604; captureOwn=1; nativeRmb=1, nativeLmb=0, nativeW=1; physicalRmb=physicalW=0; syntheticRmb=0; camera=1; все 20 firstBlock=eligible |
| 711 → 712 | viewport hovered/capture 1→0, hoveredWindow Viewport→none, inputOwner 4→6; camera остаётся 1, eligible; это ещё успешный RMB interval |
| 712–730 | bounds 12,210,729,368; native cursor 640,360, synthetic cursor disabled sentinel; fullscreen=0, active=0, gizmo=0,0, cursor=212995 (DISABLED); camera остаётся active |
| keyboard failure | detailed observer уже остановлен после RMB phase; сравнение последнего успешного/первого неуспешного keyboard frame недоступно |

В обязательном профиле тот же standard native controller прошёл **226 checks**.
Успешный profile не печатает этот failure-only buffer, поэтому покадровая
разница profile/full в отказавшей keyboard phase отсутствует. Нельзя объявлять
её ни доказанным B2 дефектом, ни доказанным историческим RMB root cause.
RootOnly control flow и controller test source в B2 не менялись.

Промежуточная commentary отметка о прохождении Controller в full была неверной:
сводный результат ещё не был проверен. Исправлена после чтения полного итога;
авторитетный результат — FAILED выше.

Все новые B2 tests прошли и в full: **176 CPU / 294 DX12 checks**.

| Обязательные regressions в full | Результат |
| --- | --- |
| Editor Hierarchy Reparent CPU / DX12 | 176 / 294 PASS |
| Editor Hierarchy Tree CPU / DX12 | 74 / 308 PASS |
| Application Hierarchy Activation CPU / DX12 | 104 / 71 PASS |
| Application Ownership CPU / DX12 | 101 / 20 PASS |
| Runtime Hierarchy CPU / DX12 | 296 / 16 PASS |
| ECS Hierarchy / Transform Contract / SceneWorldMode | 261 / 718 / 406 PASS |
| MultiSelection CPU / DX12 | 2550 / 395 PASS |
| Project Core/System/Settings CPU/DX12 | PASS; Settings 75 / 64 |
| Editor2 / Workflow / LevelWorkflow CPU/DX12 | PASS |
| LuaGameplayController CPU / DX12 | CPU 62 PASS / DX12 FAILED as above |
| Остальные tests | PASS |

## DX12, целостность и решение

**DX12 validation error markers: 0** по final profile/coverage/full detail;
GetValidationErrorCount()==0 assertions завершившихся Reparent/Tree/Activation/
Ownership и других GPU tests прошли. Нет `DX12 validation failed at shutdown`,
D3D12 ERROR/CORRUPTION или severity error markers. Отказ Controller — input
assertion; его полный test не завершился, это не GPU PASS вместо failed test.
Источник: `out/p03db2-validation-check.log` плюс raw detail logs.

**git diff --check: PASS**; отдельная whitespace проверка новых файлов — PASS.
Сверены все **837** входных SHA-256: missing=0, изменены только семь заявленных
файлов, добавлены пять заявленных, unexpected changes=0. Все **90** assets и
EventBus совпадают с началом. Предыдущие dirty production/test/report файлы
сохранены. Источники: `out/p03db2-final-integrity.log`,
`out/p03db2-final-status.txt`, начальные hashes/status. Предупреждение Git о
CRLF Application.cpp относится к прежнему dirty baseline; его SHA-256 не менялся.
Commit/push/merge/rebase/reset/clean не выполнялись.

**INCOMPLETE для финального baseline/READY gate.** Atomic graph edits, existing
Undo integration, native source/targets/root, cancellation/lifetime, KeepWorld,
physics/sun validation и RootOnly regression coverage проверены. Однако READY
требует зелёных старых regressions; 68/69 этого не выполняет.

Никаких неподтверждённых input fixes и повторного full CTest. Минимальный
следующий диагностический шаг — отдельная задача по failing keyboard phase с
bounded frame-state trace вокруг её исходного Run(20), чтобы различить yaw,
desiredDirection и cursor/focus/ownership, без изменения camera activation или
assertions. B2 не объявляется READY; переход к C/D не рекомендуется до решения
baseline gate. Остальные Roadmap задачи не начинались.
