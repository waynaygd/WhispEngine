# Editor-only Entity Icons & Hover Tooltips

Реализовано в локальной ветке `scripting`. Коммиты и отправка изменений не выполнялись.

## Overlay и picking

- Entity с Transform без MeshRenderer получает ромб с маленьким кубом, размером 22 px × DPI. Выбранная Entity отмечается золотой рамкой. Светлая обводка и тёмная подложка сохраняют контраст на фоне сцены.
- Переиспользуется `ProjectGizmo`: отсечение за камерой, по near/far и за пределами Viewport. Размер и область попадания заданы в экранных координатах и не зависят от расстояния или Transform.scale.
- PlayerStart, Directional/Point/Spot Light и Fog исключены из fallback-кандидатов. Их существующие helpers сохраняют приоритет. Два канала текущего ImDrawList располагают универсальные иконки под специальными маркерами; специальная область hover и handles блокируют fallback-picking.
- Универсальные иконки сортируются по глубине, затем по Entity index/generation. Рисуются от дальних к ближним; последняя подходящая иконка является единой целью tooltip и клика. При одинаковой глубине побеждает меньший ID, независимо от порядка создания и повторного использования индексов ECS.
- Выбор использует `SelectViewportEntity`: обычный клик, Ctrl toggle, Shift add и Active Entity. Используется существующий флаг поглощённого helper-клика, чтобы тот же клик не запускал mesh-picking или новый gizmo drag.
- Приоритет ImGuizmo, захват камеры, RMB, текстовый ввод, модальные окна и активные drag/handles сохранены.

## Tooltip и режимы

- Штатный ImGui tooltip получает актуальный Tag непосредственно из текущего World; пустой или отсутствующий Tag даёт `Entity #<index>`.
- Hover Entity, имя и кандидаты не сохраняются между кадрами. Удаление, смена поколения и Editor/Runtime World не оставляют устаревших ссылок.
- Overlay доступен в обычном и F11 Viewport в Edit Mode, скрыт в Play и Game View; учитывает общий переключатель Editor visuals.
- Иконки рисуются только через ImDrawList, без изменения курсора layout или content bounds. Tooltip — стандартное временное окно ImGui без сохранения docking layout.
- ECS-компоненты, физические объекты, Scene/Prefab serialization, Lua API, игровой Renderer и шейдеры не изменялись. Сравнение readback игрового render target подтверждает отсутствие иконок в нём.

## Изменённые файлы

- `engine/editor/EntityIcons.h` — eligibility, имя и порядок кандидатов.
- `engine/editor/EditorEntityIcons.cpp` — drawing, hover, tooltip и выбор.
- `engine/editor/EditorLayer.h`, `EditorLayer.cpp` — подключение прохода и приоритет слоёв.
- `engine/editor/LightGizmos.cpp`, `EditorFogGizmos.cpp` — передача существующего специального hover в общий проход.
- `engine/CMakeLists.txt`, `CMakeLists.txt` — модуль и регистрация CPU/DX12 tests.
- `tests/EditorEntityIconsRegression.cpp` — новые регрессионные проверки.
- `tests/EDITOR_ENTITY_ICONS_REPORT.md` — этот отчёт.

## Проверки

Release-сборка всех целей завершилась успешно. Все **12 профильных тестов прошли**:

| Группа | Результат |
|---|---|
| EditorEntityIconsRegression / EditorEntityIconsDx12Regression | 2/2 |
| FogPostHotfixRegression / FogPostHotfixDx12Regression | 2/2 |
| MultiSelectionRegression / MultiSelectionDx12Regression | 2/2 |
| EditorWorkflowRegression / EditorWorkflowDx12Regression | 2/2 |
| EditorUsabilityRegression / EditorUsabilityDx12Regression | 2/2 |
| ScalePlaneGizmoRegression | 1/1 |
| LuaGameplayControllerRegression | 1/1 |

Новые CPU-проверки: Empty/Lua eligibility, специальные типы и MeshRenderer, имя/rename/fallback, удаление и generation/World safety, порядок перекрывающихся кандидатов, проекция и culling, существующая Ctrl/Shift selection.

Новый DX12-тест использует настоящий Application/ImGui/render loop с синтетическими событиями ImGui: hover без selection, изменение tooltip, Empty/Lua picking, Ctrl/Shift, перекрытие и совпадение hover/pick, приоритет Fog, удаление/Undo, Play/Stop/Game View, неизменность игрового render target, F11 туда/обратно, resize, сохранение dock node, DPI 100/150/200%, Move/Rotate/Scale после выбора и Undo. Проверяется наличие живого tooltip и его геометрия; точные строки отдельно проверяются CPU-тестами. Сохранён и визуально проверен снимок настоящего DX12 backbuffer с иконками и tooltip.

ImGui ErrorCallback не зарегистрировал layout diagnostics; asserts не отключались. **DX12 validation errors: 0.** `git diff --check` прошёл. Полный CTest не запускался: изменения ограничены редакторским overlay и picking.

## Ручная проверка

Рекомендуется пройти пользовательский сценарий в видимом редакторе с физической мышью: создание Empty, добавление Lua, hover/selection, gizmos, Play/Stop и переключение F11. Автотесты используют синтетические события, а не физический ввод.

Переход между мониторами с разным системным DPI и произвольный пользовательский docking layout требуют ручной проверки. В DPI-fixture заранее резервируется font atlas 1024×1024 для тестовых масштабов; жизненный цикл font textures DX12 не изменялся.
