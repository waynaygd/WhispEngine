# Lua Gameplay API Upgrade — Transform, Lighting & Entity References

Ветка `scripting`, текущее локальное состояние. Расширены существующие sol2 bindings, Lua schema и Inspector. Второй Lua VM/runtime gameplay subsystem, новый формат сцен или система освещения не создавались. Коммиты, push, reset/clean не выполнялись.

## API

```lua
World.set_rotation(entity, Vec3.new(x, y, z)) -- Euler radians, существующий Rz*Ry*Rx
World.set_scale(entity, Vec3.new(x, y, z))
Light.get_intensity(entity)                  -- number
Light.set_intensity(entity, intensity)
```

Transform setters проверяют World identity/generation/liveness и наличие Transform. Запись идёт через ComponentRegistry.Set с теми же finite/range ограничениями, что Inspector/scene registry: компоненты вектора в [-1e6,1e6]. Новые setters не вводят другой контракт для отрицательного/нулевого scale, уже допускаемого registry; для видимой обычной геометрии следует использовать ненулевой масштаб. Renderer/ECS получают изменённый действительный Transform.

Light выбирает DirectionalLight, PointLight либо SpotLight по существующему ECS-компоненту. Intensity записывается через тот же ComponentRegistry, диапазон [0,100000], NaN/Inf отклоняются. Отсутствующий Light или dead/stale/foreign handle вызывает понятную Lua error; существующий protected callback изолирует ошибку экземпляра, редактор продолжает работать. Scene lights заново собираются существующим rendering path, поэтому отдельный dirty flag/GPU light cache API не нужен.

Ограничения управления Transform и Sun/Sky:

- Это прямое изменение Transform, не физическая команда/teleport API. Для dynamic Rigidbody/CharacterMovement нельзя строить ходьбу, физический поворот или изменение тела через эти setters: используйте Character/Physics API. Прямые изменения могут быть скорректированы solver/lockRotation/interpolation; scale не является API изменения Collider halfExtents. Новые setters не меняют velocity, collision solver или grounded logic.
- Существующий SynchronizeSun остаётся авторитетным: Sky Drives Light может перезаписать rotation основного Directional, а Day/Night с driveLightProperties — его intensity/color перед rendering. Для ручной Lua-анимации солнца выберите Independent и отключите driveLightProperties. Light API не отключает эти режимы молча. Point/Spot не зависят от sun synchronization.

## Entity fields и сериализация

```lua
return {
    fields = {
        globe = { type = "entity", label = "Globe" },
        spotlight = { type = "entity", label = "Spotlight" }
    },
    on_update = function(self, dt)
        if self.globe and World.is_alive(self.globe) then
            local r = World.get_rotation(self.globe)
            World.set_rotation(self.globe, Vec3.new(r.x, r.y + dt, r.z))
        end
    end
}
```

У entity допустим отсутствующий default либо nil; числовой/строковый/table default отвергается на schema validation. Ссылки назначаются в Inspector текущей сцены. Старые number/integer/bool/string/vec3/asset fields сохраняют прежние схемы и представление.

Ссылка хранится в существующем LuaScriptComponent.properties:

```json
{
  "globe": { "$entity": "entity-...persistent-scene-identity..." },
  "spotlight": null,
  "rotation_speed": 0.5
}
```

`$entity` — значение SceneIdentityComponent, а не ECS index/generation/World number/pointer. Явный tag отличает ссылку от обычного string/asset. Валидатор допускает ровно один строковый ключ `$entity`, непустой ID длиной до 1024 без NUL; malformed objects отклоняются при scene/prefab load. Null означает пустое поле. Ошибка типа override относительно конкретной Lua schema проверяется runtime при создании экземпляра/hot reload.

Если программно созданный/старый объект ещё не имел SceneIdentity, Inspector assignment добавляет её в той же Undo transaction: Undo восстанавливает исходные reference/metadata, Redo возвращает точный ID. Обычные editor-created объекты уже имеют SceneIdentity. Прежний serializer fallback `local-index-generation` для других объектов не используется при назначении новых ссылок.

Разрешение выполняется по SceneIdentity только среди живых Entity текущего World, при создании Lua instance. Нет цели/неоднозначная identity → nil. Изолированные World с одинаковым Scene ID получают свои локальные handles. CloneData сохраняет SceneIdentity/properties как данные; Lua Runtime World создаёт self.globe/self.spotlight с runtime World identity и generation. Editor handles не копируются в self. Stop завершает runtime и возвращает исходные Editor World/properties.

После уничтожения уже выданный handle остаётся прежним: World.is_alive возвращает false, Transform/Light setters/getters отклоняют его. Повторное использование ECS slot не меняет handle на новую Entity. Ссылки намеренно не перепривязываются каждый кадр. После Undo удаления Inspector снова находит объект по identity; уже работающему экземпляру для получения восстановленного handle нужен Reload Script либо повторный Play. Ранее nil тоже разрешается заново при reload/пересоздании экземпляра.

Reload использует прежнюю проверку candidate script в disposable validation VM. Успешный reload пересоздаёт instance с существующими overrides и повторно разрешает ссылки в текущем World. Ошибочный schema/compile candidate сохраняет предыдущую рабочую программу. Если новая валидная schema несовместима с сохранённым override, instance получает диагностическую ошибку; данные не приводятся к другому типу молча.

## Inspector / Undo

Для entity поля показывается label из Lua и Combo с именем выбранного объекта. В списке — None и Entity текущего World. Объекты с одинаковыми именами имеют разные ImGui IDs. Отсутствующая сохранённая цель отображается как Missing entity. Кнопка X записывает null. Combo принимает существующий `WHISP_ENTITY` payload из Hierarchy, проверяя World и generation.

И список, и drop, и X вызывают один SetLuaEntityReference. Он проверяет schema и цель, сохраняет обычный EntitySnapshot, пишет properties, помечает scene dirty и завершает существующую Undo transaction. Scene Save/Load, duplication и prefab snapshots используют прежнюю сериализацию LuaScriptComponent.

Prefab spawning не менялся: свежий prefab root получает новую identity, external scene-local references в его properties разрешаются в World спавна, отсутствующие цели дают nil. Автоматического remap ссылок на оригинальный prefab root/несколько внутренних prefab entities нет; MVP не является asset-level actor reference graph. Межсценовые ссылки не поддерживаются.

## ConcertController.lua

Файл: `test_projects/legacy_workspace/Assets/scripts/ConcertController.lua`, штатно staged через LuaGameplayAssets в активный Asset Root (`assets/scripts/ConcertController.lua`).

1. Создайте Mesh, например Sphere, и назовите Globe. Создайте Spot Light, направьте его на освещаемую геометрию.
2. Create → Empty Entity, Add Component → Lua Script.
3. Выберите `scripts/ConcertController.lua` через Choose Script.
4. Назначьте Globe и Spotlight из Combo либо перетащите их из Hierarchy. Ненужную ссылку можно очистить X.
5. Нажмите Play в Lit Viewport: on_update вращает Globe вокруг Y и задаёт Spotlight intensity `5 + sin(time * pulse_speed) * 3`. Для контроллера не нужны Allow gameplay input/camera.
6. `rotation_speed` и `pulse_speed` доступны в Inspector. Lua overrides сохраняются в сцене, runtime edits отбрасываются на Stop.
7. Open Script открывает активную копию файла, которую использует Reload Script. Измените default rotation_speed либо код on_update, сохраните и нажмите Reload Script. Существующие явные Inspector overrides имеют приоритет над изменёнными defaults. Поля ссылок сохраняются, on_start сбрасывает time.

Полноценная концертная сцена не создавалась; пример управляет только назначенными объектами. Логика input/FPS не добавлялась в Application.

## Автоматические проверки

Release-сборка всех targets: PASS (`out/lua-api-release-final.log`, `out/lua-api-release-last.log`, exit 0). `git diff --check`: PASS.

| Regression | Результат |
|---|---|
| LuaGameplayApiRegression / LuaGameplayApiDx12Regression (новые) | 2/2 PASS |
| LuaScriptingRegression (включая Wave Survival и prefab) | 1/1 PASS |
| LuaGameplayControllerRegression / LuaGameplayControllerDx12Regression | 2/2 PASS |
| MultiSelectionRegression / MultiSelectionDx12Regression | 2/2 PASS |
| Editor2Regression / Editor2Dx12Regression | 2/2 PASS |
| EditorWorkflowRegression / EditorWorkflowDx12Regression | 2/2 PASS |
| EditorUsabilityRegression / EditorUsabilityDx12Regression | 2/2 PASS |
| FogRegression / FogDx12Regression | 2/2 PASS |
| LightSystemRegression / LightSystemDx12Regression | 2/2 PASS |
| ShadowMappingRegression / ShadowMappingDx12Regression | 2/2 PASS |
| PbrCoreRegression / PbrCoreDx12Regression | 2/2 PASS |

Итого 21/21 уникальных профильных тестов. Основной compatibility прогон: 19/19, 62,82 s (`out/lua-api-profile.log`). Финальный прогон после дополнения Undo metadata и stale-setter coverage: новые API, MultiSelection и EditorWorkflow CPU/DX12 — 6/6, 20,02 s (`out/lua-api-final-tests.log`). Полный CTest не запускался; профиль охватывает изменённые schema/serialization/Inspector и соседние gameplay/rendering системы. DX12 validation нового native теста: 0 ERROR/CORRUPTION.

Новый LuaGameplayApiRegression проверяет Transform rotation/scale, registry bounds/NaN/Inf, все три типа Light, invalid/missing components, typed entity overrides и malformed JSON, assignment/clear/Undo/Redo, disk Save/Load, CloneData, runtime/editor isolation, delete/Undo, index reuse, nil/missing target, atomic failed reload, успешный reload с новым default и старой ссылкой, совместимость всех прежних field types и prefab spawning со scene-local reference.

Новый LuaGameplayApiDx12Regression использует реальные ImGui items и synthetic pointer events: Combo assignment, X, Undo/Redo, drag из реальной Hierarchy в Inspector. Затем создаёт изолированный Runtime World, запускает ConcertController и проверяет rotation; direct Lua updates в Pause детерминированно меняют rotation/scale и intensity, сравнение actual HDR readbacks проверяет изменение Mesh и освещения. Проверены Stop/сохранность authored scene, размер HDR target и DX12 validation 0 ERROR/CORRUPTION. Для HDR readback включён штатный FXAA path, Renderer не менялся.

Существующий LuaScriptingRegression проверяет Wave Survival victory/defeat, prefab spawning/rollback, lifecycle, прежние типы полей и hot reload; LuaGameplayController CPU/DX12 — FPS/input/camera/PlayerStart. Старые тесты не ослаблялись. Preferences/imgui.ini после native тестов восстанавливаются имеющимся diagnostics runner.

## Изменённые файлы

- `engine/scripting/LuaRuntime.cpp`: Transform/Light bindings, entity schema и self fields.
- `engine/scripting/EntityReference.h`: общий формат/валидатор/resolver Scene Identity.
- `engine/ecs/ComponentRegistry.cpp`: validation tagged reference/null в properties.
- `engine/editor/EditorInspector.cpp`, `EditorLayer.h`: assignment command и Combo/drop/X.
- `test_projects/legacy_workspace/Assets/scripts/ConcertController.lua`: рабочий пример.
- `tests/LuaGameplayApiRegression.cpp`, `CMakeLists.txt`: CPU/DX12 regressions и dependency на staged Lua assets.
- `tests/LUA_GAMEPLAY_API_UPGRADE_REPORT.md`: этот отчёт.

## Ограничения / ручная проверка

- Проверить ConcertController на пользовательских материалах/светильниках, включая параметры Spot cone/range, Lit/Unlit и приоритет Sun/Sky. Native synthetic scene подтверждает rendering, но не заменяет проверку конкретной сцены пользователя.
- Проверить длительный Play/hot reload/Inspector scrolling и drag-and-drop на физической мыши при разных DPI. Automated pointer tests выполнены на фиксированном layout.
- References только scene-local, snapshots handles разрешаются при создании instance, а не непрерывно; требования к повторному разрешению после Undo описаны выше. Prefab graph remapping и межсценовые ссылки в MVP не добавлены.
- Физические возможности, input routing, Fog/PBR/shadow rendering source code не менялись. Для управления dynamic Rigidbody нужны физические команды, а не прямой Transform.
