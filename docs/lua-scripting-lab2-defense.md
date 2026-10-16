# ЛР №2 — Lua Scripting и Gameplay Layer

## Ветка и исходное состояние

Работа выполнена в существующей `scripting`, исходный commit
`321d4361bb713310d977d35ae7543396b33515d1`. До работы дерево было чистым.
Локальная `main` указывала на тот же commit. Merge, rebase, reset, commit и push
не выполнялись. Изменения предназначены для ревью в `scripting`.

## Карта интеграции

```text
Application
 ├─ m_World: данные Editor, ComponentRegistry, SceneSerializer v4
 ├─ m_RuntimeWorld: World::CloneData при Play, уничтожение при Stop
 └─ m_LuaRuntime: RAII Lua state + программы + экземпляры
       ├─ LuaRuntime::Impl::Load: asset → source → sandbox → schema → revision
       ├─ Impl::Sync: Entity → environment + behaviour table + self
       ├─ Impl::Call: protected C++ → Lua lifecycle
       ├─ Impl::Bind: Lua → проверяемые ECS/Input/Log операции
       └─ SpawnPrefab: JSON → ComponentRegistry → новая Entity/SceneIdentity

PhysicsSystem → EventBus::PublishCollision
              → Application listener → LuaRuntime::QueueCollision
              → bounded queue → on_event
```

`engine/scripting/LuaRuntime.{h,cpp}` объединяет загрузку, VM и экземпляры:
отдельные классы без самостоятельной ответственности не вводились.
`engine/scripting/Prefab.{h,cpp}` отвечает за безопасный путь и prefab spawn.

Lua удобен для коротких итераций над правилами игры без C++-сборки; VM
встраивается в процесс движка. sol2 даёт проверяемые C++ usertypes и
`sol::protected_function`. Lua 5.4.8 собирается статически из C sources;
архив закреплён SHA256. sol2 3.3.1 закреплён commit
`dca62a0f02bb45f3de296de3ce00b1275eb34c25`. См. `cmake/Lua.cmake`.
Системная установка Lua не нужна. Ссылки на первичные материалы:
[Lua releases](https://www.lua.org/ftp/),
[sol2 protected_function](https://sol2.readthedocs.io/en/latest/api/protected_function.html).

### Владение и изоляция

`Application::m_LuaRuntime` живёт меньше обоих World. В `Impl` Lua state
объявлен до Lua references и уничтожается после них. `Stop` вызывает
`on_destroy`, освобождает экземпляры, очищает команды, события и input history,
отвязывает World и выполняет GC. Definitions сохраняются для следующего Play.

`LuaScriptComponent` содержит только `scriptPath`, `enabled` и JSON `properties`.
Он копируется обычным `World::CloneData`; Lua references в нём отсутствуют.
Стабильный ID — `ComponentId("LuaScript")`. Add/Remove/Reset/Capture/Restore
встроены в `ComponentRegistry::Build`, поэтому работают существующие
сериализация, Duplicate, Copy/Paste и Undo/Redo.

Каждая Entity получает отдельные environment, заново исполненную definition
и таблицу `self`. Даже локальные mutable upvalues файла не разделяются между
экземплярами. `self.entity` — value handle `{index,generation,world}`;
каждое обращение к Transform проверяет `World::IsAlive`. Удалённые,
пересозданные и принадлежащие другому World handles не разыменовываются.
`get_position/get_rotation/get_scale` возвращают копии Vec3, не component pointers.

### Lifecycle и порядок фаз

`Application::SetEditorPlayMode(true)` клонирует Editor World и вызывает
`LuaRuntime::Start`. Экземпляры создаются на первом `Update`, `on_start`
вызывается один раз перед первым `on_update`.

Порядок `Application::Run`:

1. Poll input / существующие resource finalization и config reload.
2. GameUpdate: camera, один `LuaRuntime::Update(dt)`, существующая state machine.
3. Ноль или несколько fixed physics ticks; collision callbacks только ставят события.
4. Общая Application MutationQueue, diagnostics.
5. UI и существующий RenderSystem для окон.

Lua использует variable dt с ограничением `min(dt, 0.1)` и не обновляется
дополнительно на fixed ticks. Частота Lua не зависит от числа окон.
Pause не вызывает Lua Update, сохраняя экземпляры. Resume продолжает их.
Stop освобождает Lua до уничтожения Runtime World, Editor World остаётся прежним.

`Impl::Sync` работает со снимком `World::Entities()`, вне `World::ForEach`.
Поэтому synchronous `World.spawn_prefab` возвращает уже живую Entity.
Её Lua instance создаётся на следующем Update. Это явная безопасная фаза spawn,
а не фиктивный pending handle. `World.destroy` помечает handle недоступным сразу
и ставит команду в существующую `ecs::MutationQueue`. После callbacks выполняются
`on_destroy`, затем очередь применяется. Повторный destroy безвреден.

VM и mutable ECS принадлежат главному потоку. Worker threads enkiTS не вызывают
Lua; существующие physics jobs заканчиваются до публикации collision events.
Новая система заданий или Resource Manager не создавалась. VM хранит только
definitions/instances, а mesh/material/texture по-прежнему загружает существующий
ResourceManager через RenderSystem. Чтение небольших Lua/JSON файлов синхронное.
Tracy zone: `LuaGameplayUpdate`.

### API игрового скрипта

Файл возвращает таблицу с `fields` и необязательными callbacks:

```lua
return {
  fields = {
    speed = { type="number", default=1, min=0, max=20, label="Speed" }
  },
  on_start = function(self) self.elapsed=0 end,
  on_update = function(self,dt)
    self.elapsed=self.elapsed+dt
    local p=World.get_position(self.entity)
    World.set_position(self.entity,Vec3.new(p.x+self.speed*dt,p.y,p.z))
  end,
  on_event = function(self,event) end,
  on_destroy = function(self) end
}
```

| API | Семантика |
|---|---|
| `Vec3.new(x,y,z)` | Value vector с `x/y/z` |
| `World.is_alive(entity)` | World + generation + pending-destroy validation |
| `World.find_by_tag(name)` | Массив всех живых Entity с Tag, первый — `[1]` |
| `World.get_position/get_rotation/get_scale(entity)` | Копия Transform поля |
| `World.set_position(entity,vec3)` | Проверяет handle и конечность координат |
| `World.spawn_prefab(key,position)` | Живая Entity после успешного восстановления |
| `World.destroy(entity)` | Deferred command, больше не участвует в Lua update |
| `World.send_event(target,type,amount)` | Числовое gameplay message в bounded queue |
| `Input.is_action_held/pressed/released(name)` | Именованное действие InputManager |
| `Log.info/warning/error(text)` | Существующие Logger и Console |

Collision event: `{type="collision", other=Entity, amount=0}`. Gameplay message:
`{type="damage", amount=1}`. Максимум 1024 ожидающих событий. Очередь забирается
после Sync; сообщения из on_update/on_event доставляются в следующем Update,
что предотвращает рекурсивную доставку. Очередь collision получает настоящий
EventBus движка; числовые Lua messages помещаются в ту же очередь доставки.

`LuaAttack` привязан к F в `ConfigureInputBindings`; игровое правило выбора цели
находится в Lua. `Application::IsInputActionActive` учитывает захват клавиатуры
ImGui, viewport ownership и camera control. Held/edges семплируются один раз
за Update и одинаковы для всех экземпляров. Editor Focus/F работает только
в Edit благодаря существующему `EditorHotkeysAllowed`.

### Fields и редактор

Поддержаны `bool`, `integer`, `number`, `string`, `vec3`, `asset`;
schema задаёт default, min/max и label. Vec3 default — `{0,0,0}` в Lua;
JSON override — `[0,0,0]`. `entity` зарезервирован. Максимум 128 полей.
В runtime значение доступно как `self.speed`, а не как глобальная переменная.

`EditorInspector.cpp::DrawInspector`: статические свойства показывает registry,
динамические — существующий Drawer. Lua fields входят в обычный entity snapshot,
используют PushUndoBeforeChange/FinishTransaction и dirty notifications.
При несовместимом override доступна кнопка `Use default`.
Schema не регистрируется как новый ComponentDescriptor.

В Inspector есть picker Lua assets, текстовое поле и drag/drop `WHISP_ASSET`,
Enabled, Reload, статус и диагностика. Asset fields поддерживают текст и drop;
prefab kind определяется по default с `.prefab.json`.
Рядом с Reload находится `Open Script`: он открывает активный `.lua` в Visual
Studio 2022, а полный canonical path показывает в tooltip. В Asset Browser то же
действие доступно двойным кликом по Lua или через `Open in Visual Studio`.
Общий модуль — `editor/ScriptEditor.cpp`: resolver использует тот же `SafeAsset`,
что Reload; поиск IDE — vswhere (17.x/Core Editor), затем проверенный devenv из PATH.
Открытие через Windows `ShellExecuteExW` и
[`devenv /Edit`](https://learn.microsoft.com/en-us/visualstudio/ide/reference/edit-devenv-exe?view=visualstudio)
переиспользует IDE и не вызывает Reload или C++ build. Если активен staged root,
редактируется именно staged-файл; копирования в source root при открытии нет.
После сохранения файла нажать Reload самостоятельно. Пустой scriptPath отключает
кнопку; отсутствующий файл/IDE или ошибка запуска показываются через Editor error
и существующий Logger. Другой редактор автоматически не выбирается.
Asset Browser индексирует `.lua` и полный суффикс `.prefab.json` раздельно.
Доступны Create Lua Script, Create Prefab (из выбранной Entity) и контекстное
Instantiate Prefab. Создание prefab исключает SceneIdentity; instantiate
входит в существующий Undo/Redo. Имена новых файлов выбираются без перезаписи.

### Transactional Reload

`LuaRuntime::ReloadScript` читает файл и исполняет candidate в отдельной временной
VM с изолированным environment без World/Input/Log. Даже math.randomseed candidate
не меняет состояние работающей VM. Синтаксис, return table, schema и callbacks
проверяются до замены source/fields/revision. Старый program остаётся при ошибке,
а diagnostic содержит asset и callback. Копии library tables не позволяют
candidate менять API существующих экземпляров.

После успешного Reload на следующем неприостановленном Update `Sync` завершает
старые instances и создаёт новые с текущими overrides. Mutable runtime state
сбрасывается. При изменении path/properties экземпляр также пересоздаётся;
при disabled/remove уничтожается. Gameplay error отключает дальнейшие callbacks
экземпляра, кроме защищённого on_destroy. Исправление + Reload восстанавливает его.
В Edit gameplay callbacks не запускаются; разрешена только загрузка schema.

Политика явная: **сохранить файл → Reload Script → Play**. Автоматического polling
timestamp нет. Live Reload доступен с пересозданием state, перенос state не реализован.

### Prefab pipeline

Формат `{"version":1,"components":{...}}` использует те же имена и поля, что scene v4.
`SpawnPrefab` проверяет путь, лимит JSON 1 MiB, версию и компоненты, восстанавливает
их через registry в временном World, затем создаёт сущность назначения.
При ошибке destination restore созданная Entity удаляется. Новая SceneIdentity
генерируется всегда; позиция переданного spawn заменяет сохранённую.
Скрипт из prefab стартует в следующем Lua Update.

`SafeAsset` требует portable relative key, нужный suffix, существующий asset root,
canonical containment; traversal/absolute/Windows drive/NUL и symlink escape
отклоняются. Lua paths разрешаются строго внутри активного AssetPaths root.

## Соответствие лекции №5

| Термин | Реализация |
|---|---|
| Gameplay Foundation | LuaRuntime поверх существующих World/Input/EventBus/Logger |
| Tool-side / Runtime | LuaScriptComponent JSON против VM/environment/self |
| Component Model | ComponentId("LuaScript"), обычные ECS Entity |
| Data-driven | fields в Lua, overrides в scene v4, components в prefab |
| Spawner | scripting::SpawnPrefab / World.spawn_prefab |
| Type Schema | Schema / ValidValue и Inspector Drawer |
| Entity Handle | Entity value + World::IsAlive, generation и world identity |
| Batched/Phased Update | один GameUpdate, snapshot IDs, deferred destruction |
| Event Messaging | collision EventBus → queue → on_event; числовые Lua messages |
| Embedding | Application владеет LuaRuntime, Impl владеет sol::state |
| Bidirectional Bindings | Impl::Call и Impl::Bind |
| Hot Reload | candidate evaluation → revision → instance recreation |

Gameplay не обращается к DX12/Vulkan/GPU. Lua меняет ECS, существующий RenderSystem
отображает эти данные. Вторая иерархия GameObject не вводилась.

## Сборка и тесты

В Developer PowerShell/Command Prompt for VS 2022 x64 из корня репозитория:

```powershell
cmake --preset x64-release -DFETCHCONTENT_FULLY_DISCONNECTED=OFF -DFETCHCONTENT_UPDATES_DISCONNECTED=ON -DWHISP_BUILD_LUA_TESTS=ON -DWHISP_BUILD_PHYSICS_TESTS=ON
cmake --build out/build/x64-release --parallel 4
ctest --test-dir out/build/x64-release -R '^LuaScriptingRegression$' --output-on-failure
ctest --test-dir out/build/x64-release --output-on-failure --timeout 180
# Воспроизводимая GUI-регрессия с временным известным layout и восстановлением preferences:
./tests/RunLuaScriptingDiagnostics.ps1 -BuildDirectory out/build/x64-release
```

DX12, Vulkan, Tracy в проверяемой конфигурации включены. Первый configure требует
доступа к Lua/GitHub. После загрузки можно включить disconnected mode.
`LuaScriptingRegression` создаёт отдельный scratch в `out`, не меняет исходные
скрипты. Проверяет protected calls, независимые instances, Transform, overrides,
EventBus, transactional Reload, traversal, prefab rollback, infinite loop,
input edges, удаление компонента, Application clone/lifecycle и Lua victory/defeat.
Итоги фактических запусков и оставшиеся проверки — в `lua-scripting-lab2-report.md`.

## Демонстрация на 5–7 минут

1. **0:00–0:45.** После сборки перейти в `out/build/x64-release` и выполнить
   `./WhispEngine.exe --lua-demo`.
   Аргумент открывает `scenes/lua_wave_demo.json`, ставит камеру на арену и
   выбирает WaveController. Для повторного открытия — File → Open Scene и тот же key.
2. **0:45–1:30.** Inspector → Lua Script → Wave Size: изменить 5 на 10.
   Прокрутить Inspector вниз до Wave Size. Показать Script asset, Enabled, остальные typed fields. Save сохраняет сцену
   в активный asset root. Проверить Ctrl+Z/Ctrl+Y, затем вернуть 10 и Save.
3. **1:30–2:30.** Play. В Hierarchy появляются Enemy, красные кубы идут к зелёному
   Core. Кликнуть viewport для ввода и нажимать F отдельными нажатиями.
   В Console видны спавны/атаки/жизни. Pause/Resume сохраняет состояние.
   Stop возвращает исходные четыре Entity.
4. **2:30–3:15.** Asset Browser → prefabs → `wave_enemy.prefab.json`.
   Открыть `out/build/x64-release/assets/prefabs/wave_enemy.prefab.json` в редакторе кода: показать Transform/MeshRenderer/Material/LuaScript.
   Context menu → Instantiate Prefab, затем Undo. В Inspector созданного Enemy
   видны Speed/Damage/Hit Radius. Demo использует объекты без Rigidbody:
   движение целиком управляется Lua, конфликтов с динамической физикой нет.
5. **3:15–4:15.** Изменить **активный** `out/build/x64-release/assets/scripts/enemy_chase.lua`: строку
   `local step=math.min(self.speed*dt,distance)` на
   `local step=math.min(self.speed*dt*2,distance)`.
   Выбрать Enemy (создать из prefab в Edit, если требуется), Reload Script.
   Play показывает удвоенную скорость без C++ rebuild. После Stop удалить
   временный Enemy через обычный редактор, если он был создан для Reload.
6. **4:15–5:15.** В этом же файле временно написать `return {` без закрытия.
   Reload показывает load error с путём/строкой; редактор продолжает работать,
   старая версия остаётся. Восстановить файл и Reload. Другой вариант:
   добавить `error("demo failure")` первой строкой on_update: Play показывает
   protected runtime error; исправление + Reload возвращает поведение.
7. **5:15–6:30.** Для быстрой победы: Wave Size=2, Waves=1, Attack Radius=30,
   нажать F по каждому врагу. Core поднимается, Console пишет VICTORY.
   Для поражения: Core Lives=1, не атаковать; Core опускается, Console пишет DEFEAT.
   Открыть LuaRuntime::Call/Bind/Sync, Prefab.cpp и LuaScriptComponent.h;
   объяснить оба направления bindings, безопасный handle и очередь мутаций.

**Какой файл редактировать:** `AssetPaths::ResolveAssetRoot()` выбирает первый
активный root относительно cwd/exe. При запуске из корня это обычно
`engine/assets`; из папки exe — `out/build/x64-release/assets`.
Точный root печатает стартовый Logger (`Asset validation: asset root`).
Для защиты запускать из папки exe и править активную копию. DX12 ожидает compiled
shaders относительно рабочей папки сборки, поэтому запуск из корня репозитория
может завершиться ошибкой отсутствующего DXIL. `LuaGameplayAssets` копирует source
scripts/prefabs/demo при каждой сборке; можно отдельно выполнить
`cmake --build out/build/x64-release --target LuaGameplayAssets` из корня,
чтобы синхронизировать source assets без компиляции C++. Такая синхронизация
перезаписывает staged copies: изменения для хранения в Git переносить в `engine/assets`.

## Короткие ответы на вопросы

| Вопрос | Ответ |
|---|---|
| Embedded scripting? | C++ host создаёт VM и вызывает скрипт внутри своего процесса. |
| Зачем Lua? | Менять gameplay без перекомпиляции движка. |
| Почему Lua, а не C++? | Быстрее итерации над правилами; низкоуровневые системы остаются C++. |
| Lua VM? | Интерпретатор, память и состояние исполнения, здесь sol::state в Impl. |
| Bindings? | Проверяемая граница вызовов и преобразования значений двух языков. |
| C++ → Lua против Lua → C++? | Impl::Call вызывает callback; Impl::Bind регистрирует World/Input/Log. |
| Как вызывается on_update? | Application GameUpdate → LuaRuntime::Update → protected_function(self,dt). |
| Как Lua меняет ECS? | set_position проверяет handle и записывает Transform на owner thread. |
| Где состояние поведения? | Отдельная self, environment и upvalues для каждой Entity. |
| Почему не raw pointers? | Storage/World могут исчезнуть; handle проверяется на каждом обращении. |
| Stale handle? | Ссылка на уже удалённую generation или прежний World. |
| Зачем generation? | Повторно использованный index не должен оживлять старую ссылку. |
| Hot Reload? | Загрузка candidate без перезапуска exe или C++ сборки. |
| Что со старыми instances? | После успешного Reload on_destroy, затем новые self с overrides. |
| JSON Prefab? | Одно описание компонентов, восстанавливаемое registry с новым ID. |
| Prefab против scene? | Шаблон одного объекта против набора объектов и настроек мира. |
| Type schema? | Метаданные типа/default/range/label для проверки и Inspector. |
| Почему нельзя менять структуру во время ECS iteration? | Это инвалидирует storage/итераторы и нарушает фазовые гарантии. |
| MutationQueue? | Применяет отложенные destroy после Lua callbacks на границе фазы. |
| Почему главный поток? | Одна VM и World имеют одного владельца; workers не касаются Lua. |
| Lua Update и Physics? | Один variable Update перед fixed physics ticks; collision доставляется позже. |
| Как события доходят до Lua? | EventBus listener ставит collision в bounded queue, Update вызывает on_event. |
| Почему не DX12/Vulkan из Lua? | Gameplay меняет данные ECS; GPU ресурсами управляет renderer. |

## Границы реализации

Sandbox снижает риск ошибок проекта, но не является изоляцией враждебного кода:
нет жёсткого VM heap quota, native library calls вроде string.rep не ограничены
instruction hook, возможен memory exhaustion. Hook прерывает Lua loops после
200000 инструкций на один protected call; `pcall/coroutine/debug/load` не выданы,
так что скрипт не может штатно снять hook или бесконечно перехватывать его ошибку.
Asset containment не защищает от внешней конкурентной подмены symlink между
проверкой и открытием файла. Проектные assets считаются локальными доверенными данными.

Автоматического file watcher, state migration, VM debug console, параллельных VM
и фоновой загрузки Lua через jobs нет. Runtime diagnostic хранится по asset key;
ошибка overrides одной Entity видна также в Inspector других Entity с этим скриптом.
Prefab loader не кэширует JSON; лимит файла и число объектов ограничены.
Список tag queries сортируется по порядку World::Entities, spatial index не добавлен.
