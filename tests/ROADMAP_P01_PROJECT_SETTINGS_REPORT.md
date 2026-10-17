# Roadmap v1.0 / P0.1 — Project Settings MVP

Дата: 2026-10-17. Ветка: `project_system`. Статус: **PASS — P0.1 реализован и проверен**.

## Результат узкого аудита

Изучены ProjectContext/ProjectManager, ApplicationProject, File menu и EditorProject,
SceneSettings/SceneSerializer, AA renderer path и EditorPreferences. Большая
перестройка не потребовалась. Project Browser и его стартовая компоновка сохранены.
SceneSerializer.cpp не менялся, scene schema осталась v4, descriptor formatVersion — 1.

У движка реально существуют только **Off, FXAA Low, FXAA High**. Они доходят через
Application::Run → IRenderAdapter::SetAntiAliasing до DX12 HDR/output post-process.
FXAA не является MSAA: effectiveSamples остаётся 1. Новые режимы и графические
подсистемы не добавлялись.

## Три уровня настроек

| Уровень | Параметры и хранение |
| --- | --- |
| Project Settings | Уже существующие name/directories/startupScene; новый default AA в `.whispproject` → `settings.rendering.antiAliasing` |
| Scene Settings | Scene AA override/source, lighting, ambient, environment/IBL, shadows, background/sun и primary directional reference; JSON scene schema v4 |
| Editor Preferences | Layout/window visibility, grid, snapping, editor camera navigation, helper visuals/visual override, asset presentation и recents; существующий `editor-preferences.json` и ImGui layout |

Input, Physics, Build & Packaging не реализованы и не представлены пустыми страницами.
General показывает имя, descriptor и каталоги read-only. Переименование проекта или
каталогов этим MVP не выполняется. Startup Scene редактируется с полной проверкой
файла сцены до сохранения.

## Хранение и атомарность

```json
{
  "formatVersion": 1,
  "name": "Game",
  "startupScene": "Scenes/Main.json",
  "directories": { "assets": "Assets", "scenes": "Scenes" },
  "settings": { "rendering": { "antiAliasing": 2 } }
}
```

AA строго целочисленный: 0=Off, 1=FXAA Low, 2=FXAA High. Null, bool, string,
float, отрицательные, неподдерживаемые и выходящие за int значения отвергаются.
Отсутствующие settings/rendering/antiAliasing используют Off. Присутствующие
settings/rendering должны быть объектами. Неизвестные верхние и вложенные JSON-поля
сохраняются; второго конфигурационного формата нет.

ProjectManager::WithSettings использует существующую процедуру WithStartupScene:
проверку безопасного scene path, сравнение актуального descriptor со snapshot,
запись временного файла, flush и атомарный replace. WithStartupScene делегирует
общей процедуре. Startup Scene и AA сохраняются **одной** записью; Application
дополнительно полностью загружает candidate scene в staging-world до записи.
Неуспех не публикует новый context и не меняет действующие настройки. В тесте
дескриптор блокируется Windows handle без FILE_SHARE_DELETE: ошибка replace
оставляет disk JSON, active context и scene settings прежними, temp удаляется.

UI редактирует копию ProjectSettings, а не активный объект. После успешной записи
Application публикует новый `shared_ptr<const ProjectContext>`. Старые snapshot
остаются неизменными. Корни не меняются, поэтому settings-only publication не
сбрасывает ResourceManager, Lua, миры и историю редактора. Внешнее изменение
JSON или смена опубликованного context при открытом грязном черновике защищены
от молчаливой перезаписи.

## Project Default / Scene Override

- Новая проектная сцена и новый Main.json получают `antiAliasingOverride: false`.
  Эффективное AA берётся из активного ProjectContext, без копирования default в сцену.
- Старые сцены без этого поля трактуются как явный override. Существующий
  `antiAliasing` остаётся прежним; если settings/AA отсутствовали, остаётся Off.
- При явном override используется числовое scene AA независимо от Project Default.
- При наследовании числовое поле остаётся авторским резервным значением, а renderer
  получает вычисленное project AA. Поэтому смена default не переписывает scene JSON,
  не помечает сцену изменённой и не создаёт команд в Undo/Redo сцены.
- В scene JSON флаг записывается только для false; отсутствие флага означает true.
  Сериализация прежних Legacy scenes сохраняет прежнюю форму. В отсутствие проекта
  действует числовое значение сцены. Legacy New Scene по-прежнему создаётся с Off.
- Включение override в Scene Settings копирует текущее эффективное AA, чтобы сам
  выбор источника не менял изображение. Изменения source/value проходят через
  существующие scene transactions и Undo/Redo.
- Play/Stop сохраняет авторский source/value отдельно от Runtime World. Project
  default можно изменить во время Play; после Stop наследование использует уже
  новое проектное значение. Save из Pause сохраняет авторский, не runtime override.
- CLI `--aa-quality` остаётся явным scene override, включая `--project`.

## Что работает в UI

File → Project Settings открывает отдельное dockable окно **Project Settings**,
а не Project Browser. Навигация содержит только General и Rendering. Подписи
расположены отдельно от полей; пути переносятся, каталоги показаны относительно
проекта с абсолютными путями в tooltip. General позволяет ввод Startup Scene и
Use current scene. Rendering предлагает три реально поддерживаемых режима и
показывает saved project default, effective scene AA и его source.

Save валидирует и сохраняет черновик, Cancel отбрасывает его без применения.
Заголовок отмечает unsaved document; footer показывает статус. Закрытие X с
грязным черновиком предлагает Save and close / Discard / Keep editing. Ошибка
сохранения оставляет окно и черновик открытыми. Проектный черновик участвует в
существующем Save / Discard / Cancel при смене проекта и выходе.

Native тест реально активирует General/Rendering controls, Use current scene,
radio modes, Save/Cancel, Scene Override и Undo/Redo, затем переходы A/B с
Cancel/Discard/Save and continue. Проверяется реальное GPU AA и выделенный
post-process target. Снимки: [General](../out/diagnostics/project-settings/general.png), [Rendering](../out/diagnostics/project-settings/rendering.png).

## Изменённые файлы

- `engine/core/ProjectContext.h/.cpp`: typed project AA, строгий parser, defaults,
  общая атомарная запись и наследуемый шаблон Main.json.
- `engine/core/Application.h/.cpp`, `engine/core/ApplicationProject.cpp`: сохранение
  project settings, разрешение effective AA, source в Play/Stop и authoring save.
- `engine/scene/SceneSettings.h/.cpp`: необязательный совместимый флаг наследования.
- `engine/render/AntiAliasing.h`: общие названия и validator существующих режимов.
- `engine/editor/EditorProjectSettings.cpp`: новое dockable окно с черновиком.
- `engine/editor/EditorLayer.h/.cpp`: state/render окна и scene inheritance UI.
- `engine/editor/EditorUi.cpp`: отдельный File → Project Settings.
- `engine/editor/EditorProject.cpp`: dirty-draft в lifecycle Save/Discard/Cancel;
  стартовая компоновка DrawProjectBrowser не изменялась.
- `WhispEngine.cpp`: сохранён явный смысл CLI AA override.
- `engine/CMakeLists.txt`, `CMakeLists.txt`: новый UI source и два профильных теста.
- `tests/ProjectSettingsRegression.cpp`: CPU и настоящий DX12/UI regression.
- `docs/PROJECT_SYSTEM.md`: хранение, интерфейс и правила наследования.
- `tests/ROADMAP_P01_PROJECT_SETTINGS_REPORT.md`: этот отчёт.

## Проверки

- Release build: PASS — `out/p01-release-build-final.log`. Первый запуск линкера
  в sandbox не смог создать temp; повтор с обычным temp-доступом MSVC прошёл.
- Финальный профиль: **5/5 PASS**, 11.43 сек — `out/p01-profile-final.log` и полный
  вывод `out/p01-profile-final-detail.log`.
- Старые Project System CPU/DX12 и ProjectCore — PASS. Новые ProjectSettings CPU/DX12
  — PASS (75 CPU и 64 native/DX12 checks в профильном запуске); добавлено два содержательных теста, общее число выросло с 54 до 56.
- Полный CTest: **56/56 PASS, 0 FAIL**, 317.36 сек — `out/p01-ctest-full.log`, полный вывод `out/p01-ctest-full-detail.log`.
- DX12 validation ERROR/CORRUPTION: **0**. Профильные native assertions zero пройдены; финальные profile/full logs не содержат validation/shutdown errors (`out/p01-validation-audit.json`). Ожидаемые ошибки negative Lua/shader tests не относятся к DX12 validation.
- `git diff --check`: **PASS, exit 0**, `out/p01-diff-check.log` (только уведомление о нормализации CRLF/LF).
- Пользовательские Lua/EventBus: 6/6 SHA256 совпадают с исходными
  (`out/p01-protected-hashes.json`, `out/p01-protected-verification.json`).

## Ограничения MVP

Только AA стало новым rendering default; lighting/shadows/environment не перемещались.
Имя и каталоги read-only. Project settings не имеют отдельного Undo stack — до Save
работают черновик и Cancel, а source/value сцены используют её обычный Undo/Redo.
Автоматический reload внешних изменений descriptor не добавлялся: после конфликта
требуется отменить черновик/переоткрыть проект. Нового font asset не добавлено:
существующий шрифт редактора может отображать неподдерживаемые глифы Unicode-пути
как `?`; сами UTF-8/Windows filesystem пути не изменяются. UI проверен на снимках
DX12 при 1280×720 и через native controls; ручные drag-docking и другие DPI не
сертифицировались. Startup Browser не перерабатывался.

Пользовательские Lua, сцены, модели и EventBus не редактировались. Commit/push/
merge/rebase/reset/clean и смена ветки не выполнялись. Исходный Git status был чистым
(`out/p01-initial-status.txt`); текущие изменения оставлены незакоммиченными.

Финальный Git status: 16 modified, 3 untracked; снимок `out/p01-final-status.txt`.

```text
 M CMakeLists.txt
 M WhispEngine.cpp
 M docs/PROJECT_SYSTEM.md
 M engine/CMakeLists.txt
 M engine/core/Application.cpp
 M engine/core/Application.h
 M engine/core/ApplicationProject.cpp
 M engine/core/ProjectContext.cpp
 M engine/core/ProjectContext.h
 M engine/editor/EditorLayer.cpp
 M engine/editor/EditorLayer.h
 M engine/editor/EditorProject.cpp
 M engine/editor/EditorUi.cpp
 M engine/render/AntiAliasing.h
 M engine/scene/SceneSettings.cpp
 M engine/scene/SceneSettings.h
?? engine/editor/EditorProjectSettings.cpp
?? tests/ProjectSettingsRegression.cpp
?? tests/ROADMAP_P01_PROJECT_SETTINGS_REPORT.md
```
