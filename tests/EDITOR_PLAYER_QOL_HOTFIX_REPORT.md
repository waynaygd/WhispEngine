# Editor & Player QoL Hotfix

Ветка `scripting`, текущее локальное состояние. Коммиты, push, merge, reset и clean не выполнялись.

## 1. Play Mode: причина и маршрутизация мыши

Существующий `CameraMouseFrame` фильтровал очередь ImGui перед GLFW/ImGui NewFrame только при `m_Camera.controlsActive`. Lua FPS использует отдельный `m_PlayerMouseCaptured`, поэтому игровой захват обходил guard. GLFW продолжает передавать виртуальные координаты при `GLFW_CURSOR_DISABLED`; одного переключения native cursor mode недостаточно для исключения hover/activation в панелях.

`Application::IsViewportMouseCaptured()` объединяет оба вида захвата. `Application::Run()` передаёт этот результат существующему `EditorLayer::RouteCameraMouse()`. Используется один guard для Edit и Play: mouse events фильтруются, клавиатура и focus events сохраняются, остаток удерживаемого жеста погашается после освобождения. Ограничение ImGui mouse input действует только в соответствующих кадрах; guard восстанавливает прежние флаги. `WantCaptureMouse` вручную не меняется.

`InputManager` продолжает опрашивать реальные GLFW devices. Lua mouse delta, команды CharacterMovement, физические тики и lifecycle не заменены. `GameplayInputAllowed`, проверка реальных UI widgets/text/popups, Tab, Escape и безопасное освобождение при потере фокуса сохранены. Проверка viewport background ID учитывает также максимизированное окно.

## 2. Character Height / Eye Height

Источник конфигурации — `PlayerStartComponent`:

- `characterHeight`: полная высота, default **1.70**, допустимый диапазон **0.20–10.00**.
- `eyeHeight`: высота глаз от ног, default **1.70**, диапазон **0–characterHeight**.

Оба поля зарегистрированы через обычный `ComponentRegistry`: Inspector, сцены и Undo/Redo используют существующую сериализацию и транзакции. При уменьшении роста глаза ограничиваются новой высотой; Undo восстанавливает оба значения. Ввод Eye Height выше тела отклоняется с ошибкой. Отсутствующие поля старых сцен получают defaults.

При Spawn:

```text
halfExtents = (0.30, characterHeight / 2, 0.30)
body.center = PlayerStart.position + (0, characterHeight / 2, 0)
camera.eye = body.center + (0, eyeHeight - halfExtents.y, 0)
```

Runtime Collider хранит фактическую геометрию; отдельного runtime-поля Character Height нет. `PlayerComponent.eyeHeight` получает значение конфигурации при спавне. Это runtime-снимок отдельного World, а не второй редактируемый источник конфигурации PlayerStart.

Предпросмотр тела, отметка глаз и runtime используют общие геометрические helpers. `Character.eye_offset` уже вычислял offset по runtime Collider, поэтому Lua API не потребовал изменения. Начальное положение камеры теперь также использует реальную половину высоты Collider. Transform.scale игнорируется; PlayerStart остаётся нефизическим маркером ног. Ширина сохранена **0.60**, без нового поля. Rigidbody lockRotation, grounded, jump и RenderInterpolation используют существующие механизмы.

## 3. F11 Viewport

`EditorLayer::Render()` обрабатывает F11 в Edit/Play/Pause, включая игровой mouse capture. Режим использует временное ImGui-окно `Viewport##Maximized` с `NoSavedSettings`, без docking, заголовка, рамки и padding. Оно занимает main viewport целиком и вызывает тот же `DrawViewport()` и тот же Renderer.

Обычный `Viewport` не перемещается и не отсоединяется от dock node. Основной dockspace поддерживается через `KeepAliveOnly`; флаги видимости панелей не меняются. Возврат F11 снова показывает обычные окна. Максимизация не сохраняется в preferences/INI и не является Windows exclusive fullscreen. Game View остаётся отдельной настройкой отображения helpers.

Переключение отменяет незавершённую edit-транзакцию и сбрасывает активный gizmo/plane/light drag. Из bounds изображения пересчитываются размеры target, aspect и координаты picking. Stop может оставить Viewport максимизированным; обратное переключение выполняется F11. Во время ввода текста или открытого popup F11 блокируется.

## 4. Lit / Unlit

Обработка display shortcuts перенесена перед запретом обычных редакторских hotkeys в Play. Исправлено прежнее обратное назначение:

- **Alt+3 — Lit** (`SetViewportUnlit(false)`).
- **Alt+4 — Unlit** (`SetViewportUnlit(true)`).

Требуется focus Viewport либо владение захваченным курсором. Реальные активные UI widgets, текст и popup блокируют переключение. Обычные W/E/R, редактирование Entity и другие edit-only shortcuts сохраняют прежние ограничения. Используется единственный существующий shading state RenderSystem; Combo читает его каждый кадр.

## 5. Изменённые файлы

- `engine/core/Application.cpp`, `Application.h`: объединённый mouse capture и начальная камера по реальному Collider.
- `engine/ecs/components/PlayerStartComponent.h`: два параметра конфигурации.
- `engine/ecs/PlayerSpawn.h`: геометрические helpers и применение параметров при спавне.
- `engine/ecs/ComponentRegistry.cpp`: поля Inspector, defaults и валидация.
- `engine/editor/EditorInspector.cpp`: информация о фактическом размере предпросмотра.
- `engine/editor/LightGizmos.cpp`: тело и глаза PlayerStart по конфигурации.
- `engine/editor/EditorLayer.cpp`, `EditorLayer.h`: F11, сохранение dock layout, display shortcuts и имя активного Viewport.
- `tests/CharacterMovementRegression.cpp`: рост, дисковый Save/Load, legacy defaults, Undo/Redo, preview/runtime, grounded/jump.
- `tests/LuaGameplayControllerRegression.cpp`: совместные native/ImGui/DX12 сценарии mouse capture, F11, picking, gizmo, размеры target, shading, text guards, focus и fullscreen Stop.
- `tests/EditorUsabilityRegression.cpp`: новое требуемое назначение Alt+3/Alt+4.
- `docs/SCRIPTABLE_GAMEPLAY.md`: параметры, ограничения и управление.
- `tests/EDITOR_PLAYER_QOL_HOTFIX_REPORT.md`: этот отчёт.

## 6. Проверки

**Release сборка успешна. Финальный профильный CTest: 13/13 PASS, 61.34 секунды.**

| Набор | Результат |
|---|---|
| PhysicsRegression | PASS |
| RenderInterpolationRegression | PASS |
| LuaScriptingRegression | PASS |
| CharacterMovementRegression / CharacterMovementDx12Regression | PASS / PASS |
| LuaGameplayControllerRegression / LuaGameplayControllerDx12Regression | PASS / PASS |
| Editor2Regression / Editor2Dx12Regression | PASS / PASS |
| EditorWorkflowRegression / EditorWorkflowDx12Regression | PASS / PASS |
| EditorUsabilityRegression / EditorUsabilityDx12Regression | PASS / PASS |

Новые/обновлённые проверки покрывают:

- Edit и Play capture, отсутствие стороннего hover/activation при ЛКМ/ПКМ, реальные GLFW polling → Lua → fixed physics, Tab, recapture, focus loss и Escape Stop захваченного FPS в fullscreen.
- Default/custom height, валидацию, eye/body geometry, центр от ног, preview helpers, настоящий дисковый Save/Load, legacy defaults, Undo/Redo обоих размеров, grounded и jump высокого персонажа.
- F11 в Edit/Play/Pause, без PlayerStart и с Lua Player; отсутствие panel chrome, точные bounds/размеры framebuffer, размер DX12 readback, resize, восстановление dock assignment/позиции/размеров панелей, отсутствие fullscreen-window в INI.
- Picking Entity и transform gizmo в fullscreen, отмену pending transform edit по F11; сохранение существующих editor selection/gizmo/menu сценариев.
- Alt+3 Lit / Alt+4 Unlit в Edit/Play, при mouse capture и F11, text-input guards; текстовый ввод блокирует и F11.

На диагностическом прогоне обнаружена зависимость старого CharacterMovement DX12-теста от скорости GPU: после 240 кадров прошло только **0.366667 s** fixed simulation, тело ещё падало (**Y=1.18306, grounded=false**) и не могло достичь пола примерно за 0.69 s. Тест теперь ограниченно ждёт реального grounded/contact, сохраняя строгую проверку высоты приземления и 5-секундный deadline. Физический solver не менялся. После исправления ожидания весь указанный набор прошёл одним прогоном.

Логи: `out/qol-final-build.log`, `out/qol-final-tests.log`; диагностика старого ожидания — `out/qol-landing-diagnosis.log`. `git diff --check` выполнен без ошибок whitespace. Пользовательские `imgui.ini` и `editor-preferences.json` сохранялись и восстанавливались runner-ом побайтно.

**Полный CTest не запускался:** изменения локальны для конфигурации спавна и presentation/input редактора; профильный набор включает соответствующие Physics, Lua, interpolation и editor CPU/DX12 регрессии.

## 7. Ограничения и ручная проверка

Native-тест использует Win32-сообщения собственному окну, реальные GLFW snapshots, ImGui events и настоящий DX12 Renderer. Это автоматическая интеграционная проверка; ручное использование физической мыши/клавиатуры не выдаётся за выполненное.

Дополнительно вручную проверить:

1. Ощущения raw mouse, физические Alt/Tab/Escape/F11 и точный возврат курсора в исходную точку.
2. Перенос окна между мониторами с разным DPI, resize в F11 и picking/gizmo после смены DPI.
3. F11/возврат и последующий перезапуск редактора с индивидуальным пользовательским docking layout.
4. Текстовые поля Inspector/Asset Browser и настоящие модальные диалоги вместе с физическими hotkeys.

Изменение роста применяется при следующем Play; это конфигурация спавна, а не runtime resizing физического тела.
