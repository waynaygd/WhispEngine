# Multi-Selection & Group Transform System

Ветка `scripting`. Изменения оставлены в рабочем дереве; коммиты и операции с удалённым репозиторием не выполнялись.

## Управление

- ЛКМ по Entity в Viewport или Hierarchy заменяет выделение.
- Ctrl+ЛКМ добавляет/убирает Entity. Добавленная Entity становится Active; при удалении Active выбирается последняя оставшаяся.
- Shift+ЛКМ в Hierarchy выделяет диапазон от anchor в текущем отображаемом, отфильтрованном порядке. Ctrl+Shift расширяет набор диапазоном.
- Shift+ЛКМ в Viewport добавляет объект. Клики с Ctrl/Shift имеют приоритет над началом drag gizmo.
- ЛКМ по пустому пространству снимает выделение; Escape в Edit снимает его либо сначала отменяет текущую транзакцию.
- Ctrl+A в сфокусированной Hierarchy выбирает все строки текущего фильтра. Текстовые поля, Asset Browser, popup/modal и другие окна сохраняют свои сочетания.
- W/E/R или Toolbar выбирают Move/Rotate/Scale. Local/World и Snap используют существующие настройки редактора.
- Все выбранные строки подсвечиваются, Active помечена `[Active]`. В Viewport показаны границы выбранных Mesh и previews выбранных Light/PlayerStart.
- Delete удаляет весь набор одной транзакцией; Ctrl+Z/Ctrl+Y отменяют/повторяют всё действие.
- Inspector показывает число выбранных Entity и явно указывает, что его поля изменяют только Active Entity.

## Selection Set и жизненный цикл

`SelectionSet` хранит упорядоченный `vector<Entity>`, Active и anchor для Shift-range. Сравнивается полный ECS handle: index, generation и World identity. Старый `GetSelectedEntity()` возвращает Active, поэтому одиночные инструменты используют тот же источник состояния.

`PruneSelection()` удаляет уничтоженные/чужие handles перед UI и операциями. Если во время группового drag исчезает участник, выжившие возвращаются к исходным Transform; удалённый извне объект не воскресает, незавершённая транзакция отбрасывается.

При Play весь набор, Active и anchor переводятся через существующий CloneMap. После spawn удалённый runtime PlayerStart сразу исключается из выделения. Stop восстанавливает полный Editor selection вместе с Editor history. Новая/загруженная сцена очищает selection/session через существующий ResetHistory. Сцены и prefab не получают нового формата сериализации выделения.

## Group Transform

Общий pivot — среднее арифметическое позиций. World reference имеет единичное вращение; Local reference использует ориентацию Active Entity. ECS остаётся плоской: parenting не добавлялся.

В начале drag фиксируются исходные Transform всех участников и исходная матрица pivot. Каждый update вычисляет:

```text
delta = currentGizmoMatrix * inverse(initialPivotMatrix)
newObjectMatrix = delta * originalObjectMatrix
```

Move даёт одинаковое мировое смещение. Rotate меняет позиции вокруг pivot и собственные ориентации через композицию матриц, без сложения Euler-углов. Scale меняет и размеры, и смещения от pivot. Состояние объектов предыдущего кадра не используется как база следующего update.

Результаты сначала полностью рассчитываются и проверяются существующим TRS decomposition, затем записываются всей группе и передаются в `NotifyEditorMutation`. NaN/Inf, shear, singular/нулевой scale и выход за ограничения отклоняют update целиком, сохраняя последнее корректное состояние. Диапазон абсолютного scale: 0.001–10000; допустимая абсолютная позиция: до 1e6. Имеющиеся отрицательные знаки scale сохраняются. Вращение Sun в режиме SkyDrivesLight также отклоняется для всей группы.

Используется существующий ImGuizmo с осевыми Move/Rotate/Scale и центральным uniform Scale. Групповой gizmo обходит только одиночные дополнительные scale-plane handles и light range/cone handles, чтобы они не перехватывали общий drag. Одиночные операции сохранены.

## Undo/Redo

Расширен существующий `EntitySnapshot.related`, отдельной истории нет. Group Transform и Delete имеют корневой snapshot со всеми участниками и полным selection metadata. Транзакция начинается при первом реальном валидном изменении и завершается при отпускании; нажатие без изменения не добавляет Undo. Escape/потеря фокуса отменяют незавершённую транзакцию штатным путём.

При Undo удаления создаются новые ECS handles. Замены remap-ятся в обеих историях, related snapshots, selection, Active и anchor, включая историю более ранних трансформаций. Redo удаления очищает уничтоженные handles. Undo обычного изменения Active property и scene settings также сохраняет состав выделения.

## Изменённые файлы

| Файлы | Назначение |
| --- | --- |
| `engine/editor/SelectionSet.h` | Ordered selection, Active, anchor, range, prune/remap |
| `engine/editor/GroupTransform.h` | Абсолютная математика и атомарная TRS validation |
| `engine/editor/EditorSelection.cpp` | Общие операции выбора, pruning, групповое удаление |
| `engine/editor/EditorGroupTransform.cpp` | Session, применение группы, общий ImGuizmo |
| `engine/editor/EditorLayer.h`, `EditorLayer.cpp` | Selection state/API, shortcut routing, dispatch gizmo |
| `engine/editor/EditorTransactions.cpp` | Group snapshots, selection restoration, generation remap, Play/Stop |
| `engine/editor/EditorUi.cpp` | Hierarchy Ctrl/Shift, подсветка, Active, меню, Help, group World scale |
| `engine/editor/EditorInspector.cpp` | Явный Active-only Inspector |
| `engine/editor/LightGizmos.cpp` | Общий picking, outlines/previews, приоритет группового gizmo |
| `engine/editor/EditorScalePlanes.cpp` | Использование Active из общего selection state |
| `engine/editor/EditorAssets.cpp`, `EditorLevelWorkflow.cpp` | Совместимость одиночных asset/import операций с Active |
| `engine/core/Application.cpp` | Prune runtime selection после Play/Stop/spawn |
| `engine/CMakeLists.txt`, `CMakeLists.txt` | Сборка новых модулей и CPU/DX12 test targets |
| `tests/MultiSelectionRegression.cpp` | Новые CPU и native ImGui/DX12 сценарии |
| `tests/MULTI_SELECTION_REPORT.md` | Этот отчёт и инструкция |

## Проверки

Release собран через установленный MSVC 2022 и Ninja (`cmake --build out/build/x64-release -j 4`), включая WhispEngine и regression targets.

Новые тесты:

- `MultiSelectionRegression`: Ctrl toggle, Active fallback, Shift filtered-order range, foreign/stale handles; многократное абсолютное Move без drift; World/Local rotation; uniform и совместимый non-uniform scale, отрицательные scale; атомарное отклонение shear/NaN/zero; cancel; Undo/Redo и repeated Delete с generation remap; Active property Undo; Play/Stop и внешний destroy во время drag.
- `MultiSelectionDx12Regression`: три отрисованных Cube, picking и Ctrl/Shift через события реального ImGui; общий Move X со Snap, Rotate Z и центральный Scale через pointer drag; одно Undo на каждый drag и восстановление Transform; реальные строки Hierarchy, Shift-range, Ctrl toggle, безопасный Ctrl+A в search, групповое Delete/Undo/Redo; DX12 validation errors = 0. Для стабильных координат проверены обычный layout и F11 Viewport.

Профильный прогон выполняется с fixture layout; исходные `imgui.ini` и `editor-preferences.json` сохраняются и восстанавливаются побайтно. Lua FPS native test использует собственное HWND/GLFW-окно и требует разрешённого перемещения системного курсора.

Расширенный профиль: **22/22 PASS**, 163.84 секунды, ошибок нет.

| Suite | Результат |
| --- | --- |
| MultiSelectionRegression / MultiSelectionDx12Regression | PASS / PASS; 346 / 176 checks (включая проверки выполнения кадров) |
| PhysicsRegression / RenderInterpolationRegression | PASS / PASS |
| CharacterMovementRegression / CharacterMovementDx12Regression | PASS / PASS |
| LuaScriptingRegression | PASS |
| LuaGameplayControllerRegression / LuaGameplayControllerDx12Regression | PASS / PASS; FPS, keyboard alternative, Edit/Play capture, Tab, focus loss, F11 и Lit/Unlit |
| Editor2Regression / Editor2Dx12Regression | PASS / PASS |
| ScalePlaneGizmoRegression | PASS |
| EditorLevelWorkflowRegression / EditorLevelWorkflowDx12Regression | PASS / PASS |
| EditorWorkflowRegression / EditorWorkflowDx12Regression | PASS / PASS |
| EditorUsabilityRegression / EditorUsabilityDx12Regression | PASS / PASS |
| AssetFileOperationsRegression / AssetFileOperationsDx12Regression | PASS / PASS |
| Editor25Regression / Editor25Dx12Regression | PASS / PASS |

`git diff --check`: PASS. Дополнительно проверены новые файлы на trailing whitespace. Полный CTest не запускался: выбран профиль общей истории/editor workflow, физики и Lua, затронутых этой интеграцией. Локальный лог прогона: `out/multi-profile.log`, подробные результаты: `out/build/x64-release/Testing/Temporary/LastTest.log` (оба являются build artifacts).

Для повторения в Developer Command Prompt после Release-сборки используйте существующий `tests/RunLuaScriptingDiagnostics.ps1` с полным путём к установленному `ctest.exe`: runner подставляет fixture layout и восстанавливает пользовательские файлы в `finally`. Он запускает весь набор. Для только новых тестов из build directory: `ctest -R "^MultiSelection(Dx12)?Regression$" --output-on-failure`; native-тесту нужен fixture layout, как указано выше. В этом прогоне использован локальный вариант runner с фильтром перечисленных 22 тестов.

## Ограничения и ручная проверка

- Multi-object property editing, group Copy/Duplicate, иной pivot и parenting не добавлялись. Inspector, prefab creation, Copy/Duplicate и asset assignment остаются одиночными операциями для Active (context action дублирует указанную строку).
- Non-uniform scale объектов с несовместимыми ориентациями может требовать shear: такой update отклоняется с сообщением; используйте uniform scale или совместимые оси. Пересечение scale через ноль и новое зеркальное отражение группы не поддерживаются.
- Дополнительные XY/XZ/YZ scale-plane handles предназначены для одиночного Transform; группа использует стандартные оси/центральный handle ImGuizmo.
- Интерактивный ручной QA физической мышью не проводился. Дополнительно проверить удобство общего gizmo на большой группе/разных DPI, многомониторном desktop, drag за границу окна, совместный выбор Light/PlayerStart/Mesh и личный docking layout. Автотесты покрывают программные события ImGui и native GLFW capture, но не все аппаратные условия.
