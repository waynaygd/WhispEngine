# Editor.2D — Toolbar, Typography & General Polish

Дата проверки: 2026-10-21. Ветка: `engine_workflow`. Коммит не создавался.

## Аудит

До изменений обычные панели наследовали технический шрифт, а Inspector, Material Editor и Asset Browser отдельно выбирали пропорциональный. Play дублировался в Main Toolbar и Viewport. Длина верхней панели зависела от документа; при узком окне инструменты не имели общего компактного представления. На 150% DPI подпись Document и кнопки Save/Undo/Redo в UI Editor обрезались. Project Browser проверял доступность recent-файлов при отрисовке.

Отдельный запуск приложения выявил перенос видимости UI-панелей в предыдущий workspace: `OpenUIDesigner` включал панели до вызова `SwitchWorkspace`, повторно сохраняющего исходную компоновку. Это приводило к лишним вкладкам в Level и плавающим Widget Inspector/Hierarchy в Material. Теперь исходная компоновка сохраняется до включения UI-панелей. Пользовательские сохранённые layouts не удаляются.

## Визуальная система

Используется существующий `EditorTheme.h` и обычный ImGui Style. Нового механизма тем или сохранения настроек нет.

| Элемент | Значение при 100% DPI |
|---|---|
| Фон панелей | RGB 0.075 / 0.085 / 0.105 |
| Фон дочерней области | 0.065 / 0.074 / 0.092 |
| Основной текст | 0.88 / 0.90 / 0.94 |
| Accent | 0.32 / 0.57 / 0.79 |
| Warning / Error | приглушённые янтарный / красный |
| Padding окна / controls | 12×10 / 7×4 |
| Расстояние между controls | 7×6 |
| Disabled alpha | 0.5 |
| Скругления окна / control / tab | 5 / 4 / 4 |
| Toolbar и Document Tabs | высота control + spacing + 8 |
| Иконки Save/Play/Pause/Stop | векторные, около 8×10 |

Активные инструменты имеют выделенный фон. Общие кнопки, меню, секции, combo, numeric fields, search и resource pickers наследуют единый Style. Цвета осей Inspector сохранены. `Action`, `Decision`, `Help`, `Inline` — небольшие вспомогательные функции для состояний, подсказок и переноса элементов.

## Типографика и DX12

Основной UI использует системный Segoe UI 16, fallback — Arial. Существующее внутреннее имя `Browser UI` сохранено для панелей, явно выбирающих этот font. Consolas (`Editor Mono`) используется в Lua Source и сообщениях Console. Отдельных сторонних библиотек и font merging нет.

Latin/Cyrillic и используемые символы типов документов проверяются в native regression. Значки выбраны из реально доступных Segoe UI glyphs: ◊ Scene, ■ UI, ● Material, {} Lua, ▲ Prefab. Недоступные в Segoe UI ромбы и квадрат заменены после проверки снимков.

Шрифты добавляются один раз в существующий атлас. Размеры 16/24/36 и основные диапазоны прогреваются до создания backend SRV; минимальный размер атласа — 1024×1024. Нет нового per-frame font loading, нового GPU texture management или sync points. Код render passes и descriptor lifetime не менялся. При отсутствии обоих системных UI-шрифтов используется встроенный ImGui fallback; полный набор кириллицы в таком окружении не гарантируется.

## Toolbar и навигация

Main Toolbar содержит Save, Play, Pause/Resume, Stop, transform tools, Local/World, Snap и Workspace. Ширины основных команд постоянны при смене документа. Векторные значки сопровождаются текстом и tooltip. Disabled Save/Play/transform-команды объясняют причину: Play, другой тип документа, read-only Lua или неподдерживаемый World scale с shear.

При ширине меньше 1080 логических пикселей transform tools переходят в Tools. Workspace содержит существующие layout presets, Maximize/Restore, Focus Selection и Return to Scene для Prefab. На широком экране Maximize доступен также отдельной кнопкой. Save All не добавлен: существующей команды нет. Выделение остаётся доступно обычным способом; фиктивный режим Select не добавлен. Горячие клавиши сохранены.

Viewport оставляет Create, Placement, View и Display. В Display находятся Lit/Unlit, Game View, UI Preview, Grid и Light Gizmos. Вторая равнозначная кнопка Play удалена. Clean Play/F11 используют прежний lifecycle скрытия панелей.

Document Tabs используют компактные отступы, type icons, стандартные ImGui active/hover/close/dirty states, полный key/path в tooltip, popup списка вкладок и scrolling overflow. Суффикс `###document.id`, document identity, транзакции и dirty lifecycle сохранены.

## Docking и специализированные панели

Имена окон и dockspace IDs сохранены, поэтому обычные пользовательские layouts не требуют миграции. Панели используют общие title/tab colors и остаются dockable. Существующие Save Current Layout / Reset Current Layout продолжают действовать только на соответствующий workspace. F11 и Maximize Workspace остаются разными командами.

В UI Editor ширина Document combo учитывает подпись, toolbar и Save/Undo/Redo переносятся при нехватке места; Canvas Name располагается над полем. Undo/Redo disabled при отсутствии истории. Preview/Fit/Zoom и подсказка управления адаптируются к ширине. В Material Editor Save/Revert/View также переносятся. Material Preview, Widget Layout, Runtime UI, resource picker semantics и история документов не переписывались.

## Project Browser и диалоги

Стартовый chooser имеет заголовок WhispEngine, подзаголовок, Recent/New/Open, Continue last project, приглушённые переносимые пути и понятное empty state. Размер ограничен доступной областью; recent list скроллится. Проверка доступности recent-файла выполняется при Open, а не при отрисовке каждого кадра. Открытие остаётся синхронным; искусственный loading/progress не добавлялся.

Основные действия в Document Transition, Project Switch, Rename и Import выделены; Discard/Delete имеют destructive color. Ошибки остаются рядом с операцией и выделены общим Error color. Порядок Save/Discard/Cancel и подтверждённые правила conflict protection/транзакций сохранены. Save не получил нового назначения.

## Проверки и визуальные материалы

Сборка Release завершилась успешно. Новый `EditorVisualUXRegression` прошёл **678 checks**, используя настоящие Application, ImGui и DX12 с `WHISP_DX12_DEBUG=1`. Проверены команды Save/Play/Pause/Resume/Stop, Move/Rotate/Snap, Display/Grid, docked Inspector, все пять типов документов, dirty indicator, Save/Discard/Cancel, Prefab return, UI/Material/Lua, compact Tools, Focus, F11 и resize/DPI. Material Preview остаётся активным. UI-панели не становятся видимыми в Material workspace. Финальный DX12 validation error count — **0**.

Общий связанный прогон: **23/24 passed**, 273.74 s. Успешны ProjectSystem (+DX12), EditorEntityIcons DX12, PrefabDocument (+restart), EditorDocument (+restart), WidgetDesigner, Workspace (+restart/corruption/migration), InteractiveRoom, EditorWorkflow (+DX12), AssetFileOperations (+DX12), AssetBrowserUX (+restart), InspectorUX, MaterialEditorUX (+restart) и EditorVisualUX.

**Незакрытая нестабильность UI4Regression:** один отдельный прогон прошёл целиком вместе с InteractiveRoom и EditorVisualUX (`out/editor2d-final-input-visual.log`), но общий итоговый прогон упал на `Fade Out completion restores FPS capture`, а отдельный повтор — на `native styled Button click`. В диагностическом прогоне перед M обнаружен `GLFW_FOCUSED=0` при отсутствии editor input block; восстановление фокуса в тесте уменьшает проблему, но не устраняет все отказы. Нельзя утверждать, что весь UI4 workflow стабилен или что каждый отказ доказанно вызван только фокусом. Runtime UI и gameplay capture код не менялись; проверки не ослаблены. Полный зелёный прогон 24/24 не получен.

Логи: [связанный прогон](../out/editor2d-final-related.log), [успешный отдельный UI4/Room/Visual](../out/editor2d-final-input-visual.log), [последний UI4 повтор](../out/editor2d-ui4-final-repeat.log).

### Запуск реального приложения и проверка изображений

Отдельно запущен собранный **WhispEngine.exe**, без test runner, шесть раз по 30 кадров, с DX12 debug layer. Использованы копия executable, собственные preferences/config и GUID-проект visual regression внутри `out/build/x64-release/editor2d-app`; исходные пользовательские данные не открывались для изменения. Все шесть запусков завершились с кодом 0. Снимки сняты встроенным frame capture и просмотрены визуально.

| Экран | Результат |
|---|---|
| [Project Browser](../out/build/x64-release/editor2d-app/project-browser.png) | Название, recent/continue, читаемый путь, footer помещается без лишнего scrollbar |
| [Level Editor](../out/build/x64-release/editor2d-app/level.png) | Одна Play-группа, контекстная Viewport-панель, нет посторонних UI-вкладок |
| [UI Editor](../out/build/x64-release/editor2d-app/ui.png) | Кириллический Canvas Name, Document label и Save/Undo/Redo помещаются |
| [Material Editor](../out/build/x64-release/editor2d-app/material.png) | Сфера Preview видима, свойства справа, нет случайных floating Widget panels |
| [Prefab Editor](../out/build/x64-release/editor2d-app/prefab.png) | Активна Prefab-вкладка; Play disabled, transform tools доступны |
| [800×600](../out/build/x64-release/editor2d-app/compact.png) | Глобальные команды помещаются, transform tools в Tools, docking сохранён |

Дополнительно просмотрены native-снимки при 150%: [UI 1280×720](../out/build/x64-release/editor-visual-ux-regression/ui-1280-150.png), [Material](../out/build/x64-release/editor-visual-ux-regression/material-1280-150.png). Найденные обрезания UI controls исправлены. Длинные dock/tab titles могут сокращаться обычным ImGui ellipsis; доступ к панели сохраняется.

### Производительность

Использован существующий `EditorLayer::Statistics::uiMs`: это wall time editor render scope, включающий вызовы существующего UI preview, а не изолированная стоимость новых кнопок. На каждый сценарий — 12 warmup и 16 измеряемых кадров, одинаковый собственный проект, NVIDIA RTX 4060 Laptop, Release/DX12 debug layer. Медианы, ms:

| Окно / DPI | Level | UI | Material | Assets |
|---|---:|---:|---:|---:|
| 1280×720 / 100% | 0.197 | 15.549 | 0.608 | 0.210 |
| 1600×900 / 100% | 0.208 | 17.091 | 0.613 | 0.205 |
| 1920×1080 / 100% | 0.185 | 18.073 | 0.550 | 0.217 |
| 1280×720 / 150% | 0.168 | 17.694 | 0.520 | 0.190 |
| 1600×900 / 150% | 0.185 | 19.845 | 0.653 | 0.211 |
| 1920×1080 / 150% | 0.240 | 19.910 | 0.563 | 0.202 |

[Сырые mean/median/p95](../out/build/x64-release/editor-visual-ux-regression/ui-cost.csv). UI Workspace заметно дороже остальных; короткие повторные прогоны давали разную абсолютную стоимость. Отдельный before/after benchmark исходной ревизии не выполнялся, поэтому количественное отсутствие регрессии по CPU не заявляется. Исправление видимости убирает выполнение UI-панелей в обычных Level/Material/Assets layouts.

Существующий AssetBrowserUXRegression проверяет **1000 assets + 64 folders**. Средняя стоимость browser presentation: Grid 0.084 ms, List 0.076 ms, Compact 0.066 ms за 48 кадров. Steady-state path probes — **0**, количество видимых карточек ограничено clipping; unified folder grid, thumbnail caching и catalog semantics сохранены. [CSV](../out/build/x64-release/asset-browser-ux-regression/unified-performance.csv).

В изменённых presentation paths нет новых per-frame filesystem checks; удалён прежний recent-project probe. Нет новых DX12 waits, preview textures или пересоздания атласа каждый кадр.

## Ограничения

- Проверенная матрица: 1280×720, 1600×900, 1920×1080 при Style DPI 100%/150%; дополнительно 800×600 при 100%. Это native resize и применение существующего DPI helper, а не физическое переключение DPI монитора Windows.
- При очень узком docking пользователь может уменьшить область сильнее доступного места для длинного текста. Доступны перенос controls, scrolling, tabs overflow и Focus. Все произвольные пользовательские layouts не перебирались.
- Сохранённый zoom/pan UI-документа не сбрасывается при resize. Для полного показа Canvas используется Fit; это проверено отдельной native командой.
- Ранее сохранённые вручную видимые или плавающие UI-панели не скрываются автоматически. Исправлено новое загрязнение исходного workspace при открытии UI; для уже сохранённого нежелательного layout предусмотрен существующий Reset Current Layout.
- Lua Workspace остаётся read-only просмотром с существующим открытием во внешнем редакторе.
- Кириллические надписи проверены; обработка произвольных Unicode resource filenames не входила в этот этап.

## Изменённые файлы

- `engine/editor/EditorTheme.h` — общие Style tokens и небольшие presentation helpers.
- `engine/render/backends/dx12/Dx12RenderAdapter.cpp` — только инициализация шрифтового атласа.
- `engine/editor/EditorUi.cpp` — Main Toolbar и monospace Console.
- `engine/editor/EditorLayer.cpp` — контекстные команды Viewport.
- `engine/editor/EditorDocuments.cpp` — внешний вид tabs, transition и Lua Source.
- `engine/editor/EditorProject.cpp` — chooser, recent presentation и project decision colors.
- `engine/editor/EditorWidgetDesigner.cpp` — responsive controls и порядок включения UI-панелей.
- `engine/editor/EditorRuntimeUI.cpp` — только editor properties UI-документа.
- `engine/editor/EditorMaterial.cpp` — responsive toolbar и ошибки.
- `engine/editor/EditorAssets.cpp`, `EditorImportUi.cpp` — presentation диалогов.
- `tests/EditorVisualUXRegression.cpp`, `CMakeLists.txt` — целевая native regression.
- `tests/PrefabDocumentRegression.cpp` — актуальные IDs Save / Workspace → Return to Scene.
- `tests/UI4Regression.cpp` — восстановление системного фокуса перед native вводом; игровые проверки не ослаблены.
- `docs/EditorVisualPolish.md` — этот отчёт.

Пользовательские сцены, префабы, engine assets и ЛР №3 не редактировались. ECS, ResourceManager, Runtime UI и DX12 renderer pipeline не изменены.
