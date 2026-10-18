# P0.3d-B1 — Hierarchy Tree & Selection

Дата: 2026-10-18. Ветка `project_system`.
**Verdict: READY для B1.** Полный CTest и ограничения — ниже.
Область: только B1, без B2/C/D и публичной hierarchy activation.

## Аудит и граница UI

Изучены P03D_EDITOR_HIERARCHY_REPORT, A2 ownership report, A3 activation report,
P02_MULTI_DUPLICATE_REPORT и EditorLayer/EditorUi/EditorSelection/SelectionSet,
transactions/runtime history, ApplicationWorld publication, HierarchyGraph,
Relationship, SceneIdentity и RootOnly input/Inspector/Gizmo pathways.

Старый DrawSceneHierarchy одновременно содержит flat rows, Focus/Rename/Delete,
Duplicate и drag payload. Его использование для H открыло бы неподготовленные
операции. Поэтому прежний RootOnly panel `Scene Hierarchy` сохранён без изменения,
а controlled H branch EditorLayer::Render вызывает только новый production
DrawHierarchyTree (`Hierarchy Tree`) и возвращается до legacy Editor controls.
Это реальная панель EditorLayer в настоящем Application/ImGui, не test-only UI.

Публичные New/Open/Project/Startup guards A3 не менялись. Новых menu/CLI/settings
для создания или открытия H нет. Tests активируют H существующими приватными
production API A3 через ApplicationTestAccess.

## Построение дерева

EditorHierarchyTree.cpp получает один валидный HierarchyGraph snapshot на
открытый UI pass. Helper HierarchyTree.cpp строит из graph.preorder плоские
presentation rows: Entity handle, индекс snapshot, depth, наличие children.
Родители и порядок остаются исключительно в ECS Relationship и HierarchyGraph;
новый parent/children storage не введён.

Сортировка — существующий P0.3b order, затем SceneIdentity, для roots и siblings.
Нет зависимости от ECS indices или порядка JSON. Depth/visibility вычисляются
итеративно, parent уже обработан в preorder. После graph build helper делает
линейные проходы; нет рекурсии и graph rebuild для каждой строки.

ImGuiListClipper работает по flattened visible preorder с фиксированной высотой
строки. TreeNodeEx использует NoTreePushOnOpen: widgets не образуют глубокий
рекурсивный stack. Для очень глубоких цепочек визуальный отступ ограничен 45%
ширины rows area; полный depth остаётся в модели, подписи `[depth N]` и tooltip.
Root/Parent/Leaf подписаны отдельно; есть стрелка children, selection highlight,
`[Active]` и footer active name, включая `(hidden)` при collapse/filter.

Invalid graph не отображается как старое дерево: rows очищаются и показывается
diagnostic. Нормальные Application publication paths отклоняют такой candidate
ещё до активного UI.

## Expansion и lifetime

HierarchyTreeState содержит только World identity, collapsed stable IDs и filter.
По умолчанию все parents раскрыты. Arrow click переключает expansion и не
меняет selection. Collapse скрывает descendants только визуально. Selection и
active/anchor не обрезаются по видимым строкам. Re-expand возвращает highlight.

Row IDs зависят от World identity и SceneIdentity; search widget ID также
привязан к World, чтобы активный ImGui text buffer не переносился в новую сцену.
Нет TreeNode state в ECS/scene JSON. ImGui window не сохраняется через ini.

World publication ResetWorldState очищает rows, visible handles, presentation и
saved presentation. Project reset делает это сразу, до следующего UI pass.
При обычном UI pass selection прунятся через World/generation guards; collapsed
IDs удалённых/переставших быть parents Entity удаляются при построении view.
Даже свёрнутое целиком окно очищает row handles и пруняет selection.

EnterRuntime/LeaveRuntime сохраняют/возвращают только новый presentation state
рядом с существующим Editor history context. Сам selection remap/restore A2
не менялся: source handles отображаются через CloneMap, runtime selection
независим, Stop возвращает author selection/active/anchor/history. Runtime
expansion/filter стартуют отдельно. Scene/project publication уничтожает оба
presentation contexts, не переносит одинаковые stable IDs между проектами.

## Search и selection

Поиск — substring по отображаемому Tag name (fallback SceneIdentity). В Windows
UTF-8 преобразуется в UTF-16 и применяется invariant Unicode lowercase через
существующий системный LCMapStringEx; проверены ASCII mixed case и Cyrillic.
Новых зависимостей нет. На non-Windows fallback ограничен ASCII case folding;
этот этап проверен на Windows/DX12. Unicode normalization/fuzzy search не добавлены.

Совпадения вычисляются один раз, затем reverse preorder помечает всех ancestors.
При активном filter collapsed state временно игнорируется только для подходящих
ветвей; сохраняются совпавшие строки и ancestor chain. Совпадение parent само по
себе не включает всех его несовпавших descendants. Arrow при search не портит
постоянный collapsed set. После очистки filter восстанавливается прежний expansion.
Поиск не меняет selection, Relationship или Transform.

Используются существующий SelectionSet и настоящий SelectHierarchyEntity:

- Click заменяет selection одним Entity, задаёт active/anchor.
- Ctrl-click добавляет или убирает Entity по прежним SelectionSet правилам.
- Shift-click использует диапазон flattened visible preorder после search/collapse.
- Ctrl+Shift добавляет диапазон и сохраняет anchor.
- Если anchor скрыт, действующее правило Range начинает диапазон с clicked row;
  не используется ECS iteration/index fallback.
- Parent и Child одновременно остаются explicit selection. Effective roots не
  вычисляются, Transform delta не применяется.

«Visible» здесь означает логические строки после collapse/filter. Pixel clipping
при scrolling не меняет range order. В H handler есть отдельная selection-only
ветка, обходящая старые EndGroupTransform/FinishTransaction mutation pathways.
PruneSelection H также не запускает rollback/group transform handlers.

## Разрешено и закрыто

Разрешены только просмотр дерева, expansion, search, row selection и безопасная
очистка selection кликом по пустому rows area. Не строятся старые Inspector,
Gizmo/overlays, toolbar, menus, asset mutation controls, context actions,
drag payload, Transform shortcuts или generic HandleEditorShortcuts.
Delete/Duplicate/Alt+Drag/Undo/Redo/Component edits/Reparent не включены для H UI.
Это не объявляет старые mutation APIs готовыми для прямого вызова на H.

RootOnly использует прежние DrawSceneHierarchy, Inspector/Gizmo, Search,
selection/transactions, Create/Delete, Alt+Drag, Undo/Redo и Project Browser.
Старый EditorUi.cpp не менялся. Application, A1 serializer, A2 ownership,
A3 activation/runtime, HierarchyGraph, physics и camera/input code не менялись.

## Тесты и файлы

Новый EditorHierarchyTreeRegression зарегистрирован как CPU и DX12 CTest.
CPU проверяет graph-to-view, порядок при перестановке storage/JSON, collapse,
Unicode search, цепочку 12 000 nodes, production EditorLayer/ImGui и реальные
mouse/key/text events. Native использует Application Initialize/Run и
SyntheticEditorInput (ImGui events, не прямое присваивание SelectionSet).

Native проверяет те же interactions, H safety shortcuts/drag, DX12 scene draws,
Play/Stop remap и presentation restore, runtime subtree retirement и slot reuse,
collapsed-window pruning, invalid graph refusal, scene switch при активном text
input, project switch с одинаковыми IDs, empty H, реальную clipping-проверку
2 000 deep nodes, Browser и возврат в Legacy flat UI.

Изменённые существовавшие файлы:

- CMakeLists.txt, engine/CMakeLists.txt;
- engine/editor/EditorLayer.h, EditorLayer.cpp;
- engine/editor/EditorSelection.cpp, EditorTransactions.cpp, EditorProject.cpp.

Новые файлы:

- engine/editor/HierarchyTree.h, HierarchyTree.cpp, EditorHierarchyTree.cpp;
- tests/EditorHierarchyTreeRegression.cpp;
- этот отчёт.

Снимок до этапа: out/p03db1-start-status.txt и out/p03db1-start-hashes.json
(832 существовавших файла). Все прошлые dirty изменения сохраняются.
Никаких commit/push/merge/rebase/reset/clean.

## Проверки

| Блок | Результат и источники в out/ |
| --- | --- |
| A — audit baseline | 3/3 PASS, 1.63 s; p03db1-a-{ctest,detail}.log |
| B — tree/flattening | Release PASS, 3/3 PASS, 1.70 s; p03db1-b-build.log, p03db1-b-{ctest,detail}.log |
| C — search/selection | Release PASS, 3/3 PASS, 1.53 s; p03db1-c-build.log, p03db1-c-{ctest,detail}.log; CPU 74 checks |
| D — первый native profile | 5/6 PASS, 11.33 s; p03db1-d-{ctest,detail}.log; новый native остановился на fixture asset readiness |
| Исправленный native fixture | 1/1 PASS, 3.95 s; p03db1-d-assets-{ctest,detail}.log; native 293 checks |
| Обязательный профиль | 22/22 PASS, 59.19 s; p03db1-final-profile-{ctest,detail}.log |
| Collapsed-window safety | 3/3 PASS, 7.98 s; p03db1-collapsed-{ctest,detail}.log |
| Финальные lifetime guards | 3/3 PASS, 7.76 s; p03db1-lifetime-{ctest,detail}.log; tree 74 CPU / 308 DX12 checks, A3 native 71 |

Финальная Release build: PASS, out/p03db1-verified-release-build.log.
Первый native fixture ожидал только mesh, пока material ещё загружался async.
Исправлен setup: предварительная загрузка mesh/material/shader и существующий
DrainProjectLoads. Sleep/retry-until-pass не добавлялись; drawing assertion
сохранён. Неуспешный журнал не скрыт, старые assertions не ослаблялись.

После финальных lifetime guards исходники больше не менялись.
**Один полный CTest: 67/67 PASS, 257.06 s**, exit 0, без repeat/retry/skip.
Источники: out/p03db1-full-ctest.log и out/p03db1-full-detail.log.

| Обязательные regression tests в полном прогоне | Результат |
| --- | --- |
| Editor Hierarchy Tree CPU / DX12 | 74 / 308 checks PASS |
| Editor2 CPU / DX12 | PASS |
| Editor Workflow CPU / DX12 | PASS |
| MultiSelection CPU / DX12 | 2550 / 395 checks PASS |
| Application Hierarchy Activation CPU / DX12 | 104 / 71 checks PASS |
| Application Ownership CPU / DX12 | 101 / 20 checks PASS |
| SceneWorldMode | 406 checks PASS |
| ECS Hierarchy | 261 checks PASS |
| Runtime Hierarchy CPU / DX12 | 296 / 16 checks PASS |
| Transform Contract | 718 checks PASS |
| Project Core / System CPU / System DX12 | PASS |
| Project Settings CPU / DX12 | 75 / 64 checks PASS |
| LuaGameplayController DX12 | 226 checks PASS |
| Остальные Physics/Lua/Rendering/Editor regressions | PASS |

**DX12 validation: 0 errors.** GetValidationErrorCount()==0 assertions прошли
в новом native Tree test, A3, ownership и остальных native regression tests.
Поиск D3D12 ERROR/CORRUPTION, severity и DX12 validation-failure markers в
финальном profile/lifetime/full detail: 0. Ожидаемые asset-loader warnings и
Lua negative-test errors не приравниваются к GPU validation errors.

**git diff --check: PASS**, отдельная whitespace-проверка новых файлов — PASS.
Сравнение 832 входных SHA-256 показывает изменения только семи существовавших
файлов из списка выше; добавлено пять новых файлов B1. Все 90 protected
assets/EventBus сохранены. Старые test sources/assertions и отчёты A1/A2/A3/P0.2
совпадают со snapshot. Index/history не изменялись. Источники:
out/p03db1-final-integrity.log и out/p03db1-final-status.txt.

## Решение и ограничения

**READY только для P0.3d-B1.** Реальные controlled H Tree UI/selection проверены,
RootOnly regressions зелёные, неподготовленные UI mutation paths закрыты.
Публичное создание/открытие H сцен, B2/C/D не включались. Это не завершение P0.3d.

Ограничения: expansion/filter не сохраняются между публикациями или перезапуском;
очень глубокие отступы ограничены шириной панели с явным depth indicator;
non-Windows Unicode case folding не реализован сверх ASCII fallback. RootOnly
search сохранил прежнюю реализацию. Ручной длительный UI soak и исчерпывающая
проверка DPI/monitor layouts не проводились; проверен автоматический настоящий
Application/ImGui/DX12 path с input events и validation.

RMB failure в B1 не воспроизвёлся: native controller прошёл в профиле и единственном
полном CTest с исходным assertion и continuous bounded trace completeness check.
Историческая intermittent issue A1/A2 остаётся техническим долгом без доказанного
root cause. Изменений input/camera и утверждения об окончательном исправлении нет.
