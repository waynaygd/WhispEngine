# Scriptable Gameplay Controller

Ветка `scripting`, текущее локальное состояние. Commit/push/merge/reset не выполнялись.

## Архитектура

Жёстко заданные `UpdatePlayerController` и `UpdatePlayerMouseLook` удалены из Application. WASD, Space, чувствительность и вычисление обзора находятся в `FirstPersonController.lua`. C++ больше не выполняет параллельный FPS controller. Free-fly редактора остаётся отдельным существующим механизмом вне Play.

Application предоставляет LuaRuntime host capabilities: кадровый InputManager, разрешённость ввода, безопасный native cursor capture и состояние игровой камеры. LuaRuntime остаётся единственным существующим VM/lifecycle/ECS-механизмом. Новая структура GameplayServices не создаёт отдельную систему скриптов.

LuaScriptComponent получил явные `inputEnabled` и `cameraEnabled`, по умолчанию false. Они сериализуются существующим ComponentRegistry и доступны в Inspector. Камера и курсор имеют владельца — Entity вызывающего скрипта. Второй скрипт не отбирает активную камеру неявно. Character-команды ограничены собственным живым Entity скрипта; игровые World API сохраняют прежнюю проверку принадлежности World и generation.

У каждого CharacterMovement применяется только его собственная команда. C++ не раздаёт ввод всем CharacterMovement. Поддерживается автономное поведение NPC без разрешения на чтение ввода.

## Порядок выполнения

1. InputManager один раз снимает состояния устройств и относительное перемещение мыши.
2. C++ обрабатывает focus/text/UI/Pause и обязательные Tab/Escape.
3. Lua `on_update` читает кадр и формирует команды.
4. Каждый fixed tick: Lua `on_fixed_update`, CharacterMovement, PhysicsSystem, контакты/grounded, история RenderInterpolation.
5. Камера получает интерполированную позицию цели после физики, затем выполняется рендер.

Frame pressed/released видны всем разрешённым скриптам кадра. Для fixed phase они накапливаются до первого физического тика и очищаются после всех callbacks этого тика. Поэтому несколько fixed ticks не повторяют edge, а кадры без тиков не теряют его. Запрос прыжка также сохраняется до физического шага. Binding нового действия или смена клавиши обновляет состояние соответствующего действия; одинаковый повторный bind не теряет pending edge.

Отключение, удаление, замена, ошибка или уничтожение скрипта очищает его команды и горизонтальную скорость, освобождает принадлежащие ему cursor/camera leases. Вертикальная скорость, гравитация, контакты и solver продолжают работать. Блокировка вращения и исправление опрокидывания Box сохранены.

Pause прекращает callbacks/физику и сбрасывает ожидающие команды ввода. Stop освобождает курсор, завершает экземпляры Lua, восстанавливает привязки ввода/free-fly camera редактора и отбрасывает Runtime World. Новый Play читает актуальные файлы из активного Asset Root; защищённый Hot Reload в текущем Play сохраняет прежнюю рабочую программу при ошибке новой версии.

## Lua API

Полная таблица вызовов, параметры и примеры: `docs/SCRIPTABLE_GAMEPLAY.md`.

```lua
Input.bind("MoveForward", "W")
Input.is_down("MoveForward")
Input.was_pressed("Jump")
Input.was_released("Jump")
local dx, dy = Input.mouse_delta()
Input.cursor_captured()
Input.capture_cursor(true) -- false: release

Character.move_local(self.entity, 0, 1)
Character.move_world(self.entity, Vec3.new(x, 0, z))
Character.jump(self.entity)
Character.is_grounded(self.entity)
Character.get_move_speed(self.entity)
Character.set_move_speed(self.entity, 6)
Character.get_jump_speed(self.entity)
Character.set_jump_speed(self.entity, 5)
Character.velocity(self.entity)
Character.stop(self.entity)
Character.eye_offset(self.entity)

if Camera.acquire() then
    Camera.follow(self.entity, Character.eye_offset(self.entity))
    if Input.cursor_captured() then
        Camera.rotate(dx * self.sensitivity, -dy * self.sensitivity)
    end
end
local yaw, pitch = Camera.angles()
Camera.set_angles(yaw, pitch)
Camera.forward()
Camera.flat_forward()
Camera.right()
Camera.position()
Camera.set_position(Vec3.new(0, 3, -5))
Camera.release()
```

Существующие `Input.is_action_held/pressed/released` сохранены как aliases с теми же разрешениями. Состояния клавиш/кнопок и named bindings принадлежат InputManager; Lua не получает GLFW window/keycode, указатель Rigidbody или доступ к OS.

Проверяются живой Entity, World/generation, право владения, компоненты, конечность чисел и диапазоны. Скорость 0–30, jumpSpeed 0–20; направление ограничивается единичной горизонтальной длиной. Камера оборачивает yaw и ограничивает pitch существующим пределом ±89°. Ошибки проходят через protected callbacks с прежним instruction budget и отображаются в Console/Error API.

## Примеры и PlayerStart

- `scripts/FirstPersonController.lua`: WASD относительно камеры, мышь, Space; три Inspector fields: Move Speed, Jump Speed, Mouse Sensitivity. Координаты Transform для ходьбы не меняются.
- `scripts/KeyboardTurnController.lua`: WASD, поворот Q/E, Space, без mouse look/capture. Замена выбранного asset меняет управление без сборки C++.
- `Create → Player → Player Start → Controller script` выбирает Lua asset. Runtime Player получает обычный LuaScriptComponent с этим путём и явными input/camera permissions. Пустой путь создаёт физического игрока без контроллера.
- Изолированная runtime-копия и удаление только runtime-маркеров сохранены.

Выбран минимальный вариант конфигурации спавна, явно разрешённый заданием: script asset на PlayerStart. Prefab-система не переписана, второго формата сериализации Lua properties нет. На обычном authoring Character параметры задаются существующими Lua properties в Inspector. Минимальный PlayerStart использует defaults выбранного скрипта; prefab/отдельные overrides параметров на маркере пока не добавлены.

## Изменённые файлы

| Файл | Изменение |
|---|---|
| `engine/scripting/GameplayServices.h` | Host capabilities и следование камеры |
| `engine/scripting/LuaRuntime.h/.cpp` | Input/Character/Camera API, fixed lifecycle, права, leases, очистка команд и ошибки |
| `engine/platform/InputManager.h/.cpp` | Кадровые held/pressed/released, mouse delta, named actions, snapshot/restore привязок |
| `engine/core/Application.h/.cpp` | Удаление FPS поведения, подключение host services, fixed порядок и интерполированная камера |
| `engine/ecs/components/LuaScriptComponent.h` | Явные input/camera permissions |
| `engine/ecs/components/PlayerStartComponent.h` | Выбор controller script |
| `engine/ecs/PlayerSpawn.h` | Назначение выбранного LuaScriptComponent runtime-персонажу |
| `engine/ecs/ComponentRegistry.cpp` | Inspector/serialization новых полей через существующий registry |
| `engine/assets/scripts/FirstPersonController.lua` | Рабочий FPS controller |
| `engine/assets/scripts/KeyboardTurnController.lua` | Альтернативное управление |
| `engine/assets/scenes/lua_wave_demo.json` | Явное разрешение ввода старому wave controller |
| `tests/LuaGameplayControllerRegression.cpp` | Новый Input/Lua/Physics/DX12 regression |
| `tests/LuaScriptingRegression.cpp` | Явное input permission тестовым акторам старого API |
| `tests/Editor2Regression.cpp` | Проверка расширенного LuaScript descriptor |
| `CMakeLists.txt` | CPU/DX12 targets, fixtures и staging dependencies |
| `docs/SCRIPTABLE_GAMEPLAY.md` | Инструкция и API reference |
| `tests/SCRIPTABLE_GAMEPLAY_REPORT.md` | Этот отчёт |

PhysicsSystem, solver, Asset Browser, SceneSerializer и исходники ImGuizmo не изменялись.

## Автоматические проверки

Новый тест использует реальные Lua-файлы, настоящий InputManager с детерминированными device samplers и настоящий PhysicsSystem. Проверяются FPS/альтернативное управление, diagonal cap, zero/multiple fixed ticks, held jump, повторный прыжок, отсутствие проседания, удаление/disable скрипта без fallback WASD, продолжение гравитации, input gating, запрет управления другим персонажем, уничтожение Entity, NaN, camera permissions/lease release, on_stop и свежий asset на новом Play.

Отдельный DX12-тест действительно запускает Application: скрипт выдаёт команды движения, персонаж приземляется, камера совпадает с RenderInterpolation, Pause останавливает движение, Stop сохраняет Editor World. Для этой автоматической сцены используется автономный тестовый скрипт; физическая клавиатура/мышь подменяются только в CPU-тесте host/device boundary, а не в production API.

Результаты:

- Полная Release-сборка: **PASS**, `out/script-controller-final-build.log`.
- Первый полный CTest: **39/39 PASS**, 251.60 секунды, `out/script-controller-ctest.log`.
- После добавления проверки переназначения Actions выполнен повторный полный прогон: **38/39 PASS**, 276.11 секунды, `out/script-controller-final-ctest.log`. Все новые gameplay, старые Lua/CharacterMovement/Physics и Editor Workflow проверки прошли.
- Единственный сбой повторного прогона: старый `Editor25Dx12Regression`, assertion `randomized selected texture independent oracle`. Изолированный повтор с тем же диагностическим layout: **1/1 PASS**, 19.44 секунды, `out/script-controller-editor25-rerun.log`. Проверка не ослаблялась; код текстур/материалов не менялся. Причина нестабильности этого отдельного теста в рамках задачи не устанавливалась.
- Новый CPU regression: **54 проверки PASS** (`out/script-controller-cpu.log`). Новый нативный DX12 regression: **10 проверок PASS** (`out/script-controller-native.log` и результат теста в полном CTest), validation errors **0**.
- Таким образом, каждый из 39 сценариев имеет успешный результат, с указанным отдельно повтором старого texture oracle. Финального единственного полного прогона 39/39 после последней правки не заявляется.
- `git diff --check`: **PASS**. Preferences/layout пользователя сохранялись и восстанавливались тестовыми scopes и диагностическим runner.

## Ручные проверки и ограничения

Не заявляется ручная desktop-проверка реальных WASD/Space, raw mouse, клика захвата, Tab/Escape, focus transitions и ввода в ImGui text fields. Их нужно проверить в пользовательской видимой сессии; автоматические проверки покрывают блокировку ввода и команды на host boundary и настоящий DX12 lifecycle.

Существующий динамический Box не превращён в capsule/kinematic controller. Ступени, moving-platform attachment, camera collision, Interact/raycast и prefab selection на PlayerStart не добавлялись. API позволяет строить разные схемы поведения и offsets камеры в Lua, но не добавляет отсутствующие физические возможности. Action names общие внутри Play; несколько авторизованных скриптов должны согласовывать их имена и самостоятельно запрашивать эксклюзивную камеру.
