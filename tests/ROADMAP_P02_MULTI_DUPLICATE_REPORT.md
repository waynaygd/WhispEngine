# P0.2 — Multi-Selection Alt + Drag Duplicate

Ветка: `project_system`. Этап ограничен существующими SelectionSet, ImGuizmo,
ComponentRegistry и scene transactions. Предыдущие незакоммиченные изменения
P0.1 сохранены; commit/push/merge/rebase/reset/clean не выполнялись.

## Причина и реализация

`EditorLayer::DrawGizmo` передавал multi-selection в `DrawGroupGizmo` до
single-selection обработчика Alt. Групповой обработчик только трансформировал
оригиналы. Кроме того, `BeginDuplicateDrag` захватывал только active Entity,
а отмена принудительно возвращала одиночное выделение.

Теперь `DrawGroupGizmo` вызывает общий `BeginDuplicateDrag` на первом реальном
изменении матрицы Move при Alt. Проверяются и порог движения мыши, и отличие
целевой матрицы от текущей: Alt+Click и неподвижный drag не создают копий.
Обычный Move, Group Rotate/Scale и правила snapping/Local/World используют
прежний путь.

## Snapshot, компоненты и ссылки

До создания сущностей копируется весь ordered SelectionSet с active и anchor;
через `ComponentRegistry::Capture` захватываются все компоненты каждого источника.
Для каждого источника заранее создаётся одна копия и новый SceneIdentity.
Таблицы original handle → clone handle и original SceneIdentity → clone
SceneIdentity позволяют сохранить порядок, active/anchor и внутренние ссылки.

Аудит зарегистрированных компонентов показал: единственное существующее
сериализуемое Entity Reference — tagged Lua Inspector property
`{"$entity":"persistent-id"}`. Только эти значения в `LuaScript.properties`
проходят `ValidEntityReference` и переназначаются по таблице идентификаторов.
Внешние ссылки, null и обычные строки, даже совпадающие с SceneIdentity,
сохраняются. Произвольная замена строк JSON не применяется. Остальные
зарегистрированные компоненты не содержат сериализуемых ссылок на Entity.

Компоненты восстанавливаются существующим `ComponentRegistry::Restore`:
авторские настройки Mesh/Material/Light/Collider/Lua и остальных компонентов
копируются, runtime Lua/physics/GPU handles не клонируются. Asset keys сохраняют
обычную модель владения ресурсами. При исключении весь созданный набор удаляется,
исходный selection и оригиналы остаются прежними. Single-selection использует
тот же путь; при неудаче создания копии исходная Entity не перемещается.

## Перенос, Undo/Redo и отмена

Существующий GroupSession сохраняет исходные Transform, pivot и текущую матрицу;
после создания копий меняется только список его целевых handles. Перенос остаётся
абсолютным относительно исходного snapshot, поэтому группа не прыгает и дельта
не накапливается по кадрам.

Одна `Duplicate and move` transaction хранит составной снимок отсутствующих
копий и исходного выделения. Завершение drag создаёт один Undo record. Undo
удаляет весь набор и возвращает исходный selection. Redo восстанавливает
сериализованные компоненты, итоговые Transform и стабильные идентификаторы,
а существующий RemapHistory обновляет handles новых поколений и selection.

Pending transaction и `m_DuplicateDrag` исключают повторное создание набора
внутри одного drag, в том числе после отпускания Alt. Escape/focus loss вызывают
существующую отмену транзакции: удаляются все копии, восстанавливается полный
исходный selection, сбрасывается GroupSession, Gizmo блокируется до отпускания
мыши. Внешнее уничтожение участника незавершённого duplicate drag также отменяет
оставшийся набор, не воскрешая уничтоженную копию. Смена сцены/проекта использует
существующий ResetHistory. Play и Pause сохраняют запрет authoring duplication.

## Проверки

Release build: PASS, `out/p02-release-build.log`.

Профильный CTest: **9/9 PASS**, 35.53 s:

- ProjectSystemRegression / ProjectSystemDx12Regression;
- ProjectSettingsRegression / ProjectSettingsDx12Regression;
- MultiSelectionRegression / MultiSelectionDx12Regression;
- ScalePlaneGizmoRegression;
- EditorWorkflowRegression / EditorWorkflowDx12Regression.

Новые проверки добавлены в существующий MultiSelectionRegression, поэтому число
CTest targets не увеличено. CPU: **2550 checks** вместе с прежними проверками;
native DX12: **395 checks** вместе с прежними проверками.

CPU покрывает наборы 1/2/16 Entity с Collider/Light/Lua, полный компонентный
snapshot оригиналов, относительные Transform и rotation/scale, выделение копий,
SceneIdentity uniqueness, self/internal/external references, неизменность обычных
строк, одну транзакцию, четыре цикла Undo/Redo, scene serializer round trip,
отмену нового набора, очистку при смене сцены и запрет Play/Pause.

Native ImGui/DX12 использует SyntheticEditorInput и настоящий Manipulate:
Alt+Click без копий; drag группы по X, отпускание Alt и продолжение движения;
ровно один набор и один Undo; клавиатурные Undo/Redo; повторный drag и Escape;
отдельные drag по X/Y/Z и YZ/ZX/XY с проверкой фактически захваченного handle,
общего перемещения и Undo. Прежние native сценарии normal Move со snapping,
Rotate/Scale, selection routing и single-selection Alt+Drag также проходят.

Итоговый полный CTest: **56/56 PASS**, 290.46 s
(`out/p02-ctest-full.log`, `out/p02-ctest-full-detail.log`). Команда включала
`--repeat until-pass:3`, но фактически **все 56 тестов прошли с первой попытки**:
в логе ровно 56 запусков и ноль неудачных попыток. DX12 validation errors: **0**,
проверены профильный и итоговый полный журналы, а также native assertions
`GetValidationErrorCount()==0`. Итоговый audit: `out/p02-validation-audit.json`.

Первый полный прогон: 55/56, единственный сбой — существующий
LuaGameplayControllerDx12Regression (`test window focused`). Две отдельные
перепроверки упёрлись в native desktop capture (`Edit RMB captures free-fly`).
После подтверждения пользователем отсутствия ввода на рабочем столе тот же
бинарный файл и неизменные assertions прошли: **1/1 PASS, 12.80 s**
(`out/p02-controller-quiet.log`). Первый полный лог сохранён отдельно в
`out/p02-ctest-first.log` и `out/p02-ctest-first-detail.log`. Второй полный прогон
также дал 55/56, тот же тест остановился на `Play without player retains RMB
free-fly` (`out/p02-ctest-second.log`, `out/p02-ctest-second-detail.log`).
Заключительный полный прогон прошёл без сбоев и повторных попыток.
Production-код или проверки ради этих сбоев не менялись.

`git diff --check`: PASS. Хеши пяти пользовательских Lua-файлов и EventBus
совпадают с исходными. Незатронутые P0.1 diff проверены по начальному снимку
`out/p02-initial-diff.patch`.

## Файлы этого этапа

- `engine/editor/EditorTransactions.cpp` — batch clone, remap, составная
  транзакция, восстановление selection при отмене;
- `engine/editor/EditorGroupTransform.cpp` — запуск дублирования из Move input;
- `engine/editor/EditorSelection.cpp` — отмена batch при инвалидированных handles;
- `engine/editor/EditorLayer.cpp` — безопасный выход single Gizmo при ошибке clone;
- `tests/MultiSelectionRegression.cpp` — CPU и native input regressions;
- `tests/ROADMAP_P02_MULTI_DUPLICATE_REPORT.md` — этот отчёт.

Project Settings, Browser, Application lifecycle, EventBus и пользовательские
скрипты/сцены/ассеты в P0.2 не редактировались. Остальные изменения в Git status
относятся к предыдущему этапу.

## Ограничения

Native проверки запускают настоящий DX12 renderer и ImGui с синтетическим
вводом в скрытом окне. Отдельная ручная визуальная проверка мышью не выполнялась.
Существующий LuaGameplayControllerDx12Regression использует видимые окна,
настоящий GLFW/Win32 focus и desktop input; его нестабильность при общем прогоне
зафиксирована выше, даже при успешной итоговой проверке.
Scene Save/Load проверен через CaptureWorld/RestoreWorld (тот же сериализатор),
файловые операции и смена проекта дополнительно покрыты существующими
ProjectSystem/EditorWorkflow regressions. Для будущих компонентов с новым типом
Entity Reference потребуется добавить его явное переназначение; родительская
иерархия и другие пункты Roadmap не реализовывались.
