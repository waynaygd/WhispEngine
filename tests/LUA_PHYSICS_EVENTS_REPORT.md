# Lua Physics Queries & Gameplay Events

Ветка `scripting`, текущее локальное состояние. Расширены существующие LuaRuntime, ColliderComponent, PhysicsSystem и EventBus. Новая gameplay-механика/сцена, отдельный EventBus, OS timers/threads, collision layers и Project System не создавались. Коммиты, push, reset/clean не выполнялись.

## Аудит

До изменений в C++ имелись Box/Sphere collision narrowphase, ориентация OBB, уже масштабированные размеры Collider и world-space offset. Публичного physics raycast не было. Editor mesh picking работает по геометрии Mesh и не подходит в качестве Collider query. В Collider не было trigger-флага, а PhysicsSystem генерировал только обычные CollisionEvent. Lua уже имел безопасные World handles, `on_event`, отложенное уничтожение и защищённые callbacks; таймеров не было.

Raycast добавлен в тот же physics translation unit, где находятся BoxAxes/SphereRadius/геометрия, с публичным `ecs/PhysicsQueries.h`. Trigger overlap использует прежний `Collide` narrowphase. Collision solver не переписан: для trigger-пар добавлены только исключения из contact generation и position projection, плюс отдельное tracking пересечений после физического шага.

## Physics.raycast

```lua
local hit = Physics.raycast(Camera.position(), Camera.forward(), 3.0, self.entity)
if hit and World.is_alive(hit.entity) then
    Log.info("Hit at distance " .. tostring(hit.distance))
    -- hit.position: Vec3, hit.normal: Vec3
end
```

Контракт:

- `Physics.raycast(origin: Vec3, direction: Vec3, max_distance: number, ignore?: Entity)` возвращает nil либо таблицу `{entity, position, normal, distance}`.
- Direction нормализуется, distance измеряется в world units. Direction должен быть конечным и ненулевым (почти нулевой вектор с squared length < 1e-12 отвергается). Координаты/компоненты direction ограничены ±1e6, max_distance — [0,1e6]. NaN/Inf и неверные типы отвергаются защищённым Lua binding.
- Нормализация не использует directional fallback solver; Sphere quadratic/discriminant вычисляется в double, чтобы маленький radius не терялся на больших world coordinates. Есть отдельные regressions для direction 1e-6 и Sphere radius 0,5 на расстоянии 100000.
- Box пересекается как ориентированный box через существующие axes; Sphere использует тот же radius = max(halfExtents), что solver. Центр = Transform.position + Collider.offset; offset — world-space. Collider.halfExtents уже масштабированы, Transform.scale повторно не применяется.
- Возвращается ближайший hit. При равном distance выбирается меньший Entity index. Луч изнутри Collider даёт distance 0, position = origin, normal = -normalized direction; это контракт начального overlap, не точка выхода.
- Ignore можно опустить либо передать nil. Явный dead/stale/foreign ignore вызывает ошибку. Запрос включает trigger colliders и Collider без Rigidbody; PlayerStart исключён, как и в PhysicsSystem. Фильтры слоёв и множественные исключения не добавлены.
- Используются текущие simulation Transform, а не render interpolation. Запрос ничего не интегрирует и не меняет World. Некорректная прямая ECS-геометрия пропускается. Entity, уже запланированные Lua World.destroy, не возвращаются; поиск продолжается до следующего живого Collider.

Camera.position/forward используют существующее camera permission/ownership. Сам Physics.raycast не требует владения камерой: можно передать собственные Vec3.

## World.find_in_radius

```lua
for _, entity in ipairs(World.find_in_radius(Vec3.new(0, 0, 0), 5.0)) do
    if World.is_alive(entity) then
        -- Work with this scene-local handle.
    end
end
```

Возвращается Lua array живых Entity с Transform, включая владельца запроса, если он находится в радиусе. Расстояние — между Transform.position и center, без учёта Collider. Граница включена, радиус 0 допустим. Порядок — возрастающий Entity index. Координаты конечные в ±1e6, radius конечный в [0,1e6]. Нет Transform/невалидная позиция/планируемое уничтожение → объект не попадает в результат. Запросы читают текущий World экземпляра; runtime handles не относятся к Editor World.

Оба query — линейные scans по сцене. Для MVP это простой предсказуемый механизм без отдельной broadphase-архитектуры.

## Timers

```lua
return {
    on_start = function(self)
        self.unlock = Timer.after(3.0, "door_unlock")
        self.beat = Timer.every(1.0, "concert_beat")
    end,
    on_event = function(self, event)
        if event.type == "door_unlock" then
            Log.info("Door unlocked")
        elseif event.type == "concert_beat" then
            -- event.timer is the handle returned by Timer.every.
            Timer.cancel(self.beat)
        end
    end
}
```

- `Timer.after(delay, event_type) -> integer handle`: одно событие.
- `Timer.every(interval, event_type) -> integer handle`: повторяющееся событие.
- `Timer.cancel(handle) -> bool`: true только при отмене существующего таймера данного владельца. Чужой, отменённый или устаревший handle даёт false. IDs монотонны в пределах LuaRuntime и не переиспользуются при Stop/Start.
- Событие идёт в существующий защищённый `on_event(self,event)` владельца: `type`, `timer`, `amount=0`, безопасный невалидный Entity в `other`. Никаких callbacks из worker threads.
- Delay конечный в [0,1e6] секунд, repeating interval строго >0; event_type — 1..128 символов. До 128 timers на instance и 1024 на runtime; превышение даёт Lua error.
- Движение времени происходит только в LuaRuntime.Update(dt), который Application вызывает при IsSimulationRunning. FixedUpdate не продвигает timers, поэтому число physics ticks не дублирует события. Pause останавливает Update и таймеры; wall-clock sleep их не продвигает.
- После Sync и очистки Character frame commands доставляются timers, затем ранее накопленные gameplay/collision events, затем on_update. Команды Character из timer callbacks поэтому не теряются перед fixed physics tick.
- Ровно одно событие на repeating timer за Update: пропущенные периоды объединяются, следующий deadline сохраняет фазу. Большой dt не создаёт неограниченную очередь catch-up callbacks. Новые timers, поставленные во время обработки due timers, рассматриваются на следующем Update.
- After(0), созданный в on_start, может сработать в том же Update; созданный в on_update — в следующем. Это не синхронный рекурсивный вызов.
- Stop, удаление/отключение LuaScriptComponent, уничтожение owner, изменение properties и успешный Reload очищают timers прежнего instance. Callback error также освобождает timers владельца. Ошибочный candidate hot reload сохраняет предыдущую программу и её timers, как и прежний контракт atomic reload.
- Timers — transient gameplay state, не сериализуются в сцену/prefab. Их создаёт on_start нового instance.

## Triggers

У Collider в Inspector/scene registry добавлен `Is trigger` (`isTrigger`, default false). Старые сцены без поля загружаются как обычные colliders. Scene/prefab/CloneData используют прежнюю сериализацию компонентов.

```lua
on_event = function(self, event)
    if event.type == "trigger_enter" then
        if World.is_alive(event.other) then Log.info("Entered") end
    elseif event.type == "trigger_exit" then
        -- other can have been destroyed; check liveness before accessing it.
        if World.is_alive(event.other) then Log.info("Exited") end
    end
end
```

- Проверяются пары Collider, где хотя бы один isTrigger. Для trigger и статической цели Rigidbody необязателен. Геометрия/вращение/offset соответствуют solver; для истинного overlap дополнительно требуется nonpositive manifold separation, а не только speculative contact margin.
- Trigger-пара не попадает в impulse solver, warm-start contact manifolds или position projection; trigger Rigidbody продолжает обычное free integration. Обычные контакты используют прежний путь.
- Tracking выполняется на главном потоке после всех physics substeps/jobs. Пара учитывает обе generation и отдельную World identity. Enter — один раз при появлении, Exit — один раз при исчезновении; события доставляются обеим живым сторонам с LuaScriptComponent через существующий EventBus/QueueCollision/on_event.
- CollisionEvent расширен trailing `CollisionKind` с default Contact. Обычный `collision` не меняется. Application не передаёт trigger-события в GroundCharacter и contact-pair accounting.
- При уничтожении одной стороны Exit получает выжившая сторона; `other` сохраняет прежний безопасный handle, `World.is_alive(other)` возвращает false. Новая Entity в том же slot не становится старым other.
- Pause сохраняет overlap tracking, чтобы Resume не породил лишний Enter и не потерял Exit. Stop/ResetState/смена World очищают пары. Добавление/удаление Collider или снятие isTrigger учитывается на следующем physics tick.
- При отсутствии triggers есть быстрый early return после линейного поиска флагов. При наличии — O(T×N) geometry checks с консервативным sphere rejection и прежним narrowphase; отдельной spatial grid не добавлено.

## Порядок в Application

Input/ownership → Lua Update (Sync, timers/events, on_update) → ноль/несколько fixed ticks (Lua on_fixed_update, Character commands, PhysicsSystem) → camera/interpolation/render. Physics события фиксируются после solver и доставляются Lua на следующем Update. Mutations от Lua остаются отложенными до существующей phase boundary. Pause не продвигает Lua/physics, Stop завершает LuaRuntime и сбрасывает PhysicsSystem.

При native тестировании учтено, что каждый новый Application::Run сбрасывает fixed accumulator: тест запускает несколько кадров подряд, а не одиночные Run(1), которые не позволяли выполнить fixed tick.

## Проверки

Release-сборка всех targets: PASS, exit 0 (`out/lua-physics-release-final.log`, финальная сборка после численного уточнения raycast — `out/lua-physics-release-last.log`). `git diff --check`: PASS.

| Regression | Результат |
|---|---|
| LuaPhysicsEventsRegression / LuaPhysicsEventsDx12Regression (новые) | 2/2 PASS |
| PhysicsRegression | 1/1 PASS |
| CharacterMovementRegression / CharacterMovementDx12Regression | 2/2 PASS |
| RenderInterpolationRegression | 1/1 PASS |
| LuaGameplayApiRegression / LuaGameplayApiDx12Regression | 2/2 PASS |
| LuaGameplayControllerRegression / LuaGameplayControllerDx12Regression | 2/2 PASS |
| LuaScriptingRegression (Wave Survival/prefab/lifecycle) | 1/1 PASS |
| MultiSelectionRegression / MultiSelectionDx12Regression | 2/2 PASS |
| Editor2Regression / Editor2Dx12Regression | 2/2 PASS |
| EditorWorkflowRegression / EditorWorkflowDx12Regression | 2/2 PASS |
| EditorUsabilityRegression / EditorUsabilityDx12Regression | 2/2 PASS |
| FogRegression / FogDx12Regression | 2/2 PASS |
| LightSystemRegression / LightSystemDx12Regression | 2/2 PASS |
| PbrCoreRegression / PbrCoreDx12Regression | 2/2 PASS |

Итого 25/25 профильных CTest, 136,35 s (`out/lua-physics-profile.log`). После добавления численных raycast checks повторены новые CPU/DX12 тесты и PhysicsRegression: 3/3 PASS, 23,68 s (`out/lua-physics-final-tests.log`). DX12 validation нового native теста: 0 ERROR/CORRUPTION. Полный CTest не запускался; профиль охватывает изменённую физику, Lua API/events, lifecycle и соседние rendering/editor системы.

Новый CPU-тест: raycast nearest/hit/miss/segment/ignore, нормализованное направление, sphere, OBB rotation/world offset, отсутствие двойного scale, origin inside, dead/foreign handles и CloneData; proximity radius 0/inclusive/order/non-Collider entities; Lua pending destroy filtering; invalid args; trigger enter/stay/exit/death/reuse/reset/world replacement, отсутствие физического импульса, save/load и legacy default; timers once/every/cancel, FixedUpdate independence, long-frame coalescing, reload/Stop/delete, callback error и budgets.

Новый DX12-тест: реальный Runtime World/Application, Camera.position/forward raycast, proximity, Physics→EventBus→Lua trigger enter/exit, gameplay timers, Pause с wall-clock задержкой, hot reload без старых repeat events, Stop с сохранением Editor World и DX12 validation 0 ERROR/CORRUPTION. Существующие тесты не ослаблялись.

## Файлы

- `engine/ecs/PhysicsQueries.h`: публичный C++ read-only query API.
- `engine/ecs/systems/PhysicsSystem.cpp/.h`: queries на существующей геометрии, trigger filtering/tracking/lifecycle.
- `engine/ecs/components/ColliderComponent.h`, `ComponentRegistry.cpp`: isTrigger/default/Inspector/serialization.
- `engine/ecs/events/EventBus.h`: CollisionKind через прежний EventBus.
- `engine/core/Application.cpp`: отделение trigger events от grounded/contact accounting; остальная логика не менялась.
- `engine/scripting/LuaRuntime.cpp`: bindings, owner timers и существующий on_event.
- `tests/LuaPhysicsEventsRegression.cpp`, `CMakeLists.txt`: CPU/DX12 regression target.
- `tests/LUA_PHYSICS_EVENTS_REPORT.md`: этот отчёт.

## Ограничения и готовность

API готов для Lua-логики определения объекта перед игроком, поиска соседей, trigger-зон и задержанных событий. Игровые правила/концертная сцена в этой задаче не добавлены.

- Box и Sphere — текущие collider types; Mesh/Convex/Capsule queries не добавлялись. Контракты Collider dimensions/offset остаются прежними.
- Queries O(N), triggers O(T×N), без collision layers и continuous trigger detection. Очень быстрый объект, полностью прошедший trigger между fixed ticks, может быть пропущен. Для большого числа плотных сенсоров нужна отдельная оптимизация.
- Lua collision/event queue сохраняет прежний предел порядка 1024 событий; экстремальный event storm может привести к пропуску событий. Repeating timers намеренно coalesce пропущенные периоды, а не воспроизводят каждое событие при низком FPS.
- Сохранённые hit/nearby/other handles надо повторно проверять через World.is_alive перед последующим использованием. После уничтожения ссылка не переназначается на новый index occupant.
- Требуется ручная проверка пользовательских trigger volumes, направлений камеры/FPS capture и Pause/Reload в конкретной сцене. Автоматические тесты используют воспроизводимые fixtures и synthetic/native input; GUI на разных DPI вручную не проверялся.
