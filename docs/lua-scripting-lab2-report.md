# ЛР №2: итог реализации и проверки

Дата: 15 октября 2026, Europe/Moscow.

Обновление после приёмки пользователем: все шесть ручных GUI-сценариев ЛР №2
(Inspector, Play/Input, Pause/Stop, Prefab и Hot Reload) подтверждены как PASS.
Исторические отметки NOT VERIFIED ниже описывают исходный автоматизированный этап;
перечисленные ручные проверки пользователем закрыты.

UX-патч Open Script: добавлены общий `editor/ScriptEditor.{h,cpp}`, кнопка Inspector,
двойной клик и контекстное действие Lua Asset Browser. Открывается canonical файл
из активного root через Visual Studio 2022 `/Edit`. В локальной проверке Windows
принял запуск для `out/build/x64-release/assets/scripts/wave_controller.lua`.
Отдельно проверяются активный staged path и отказ для пустого/небезопасного/
отсутствующего файла. Открытие не выполняет Reload, копирование или C++ build.

## A. Git status

- Ветка: существующая `scripting`.
- Исходный commit: `321d4361bb713310d977d35ae7543396b33515d1`.
- Начальное рабочее дерево чистое; локальная main указывала на тот же commit.
- Итог: изменённые и новые файлы оставлены незакоммиченными для ревью.
- Commit, push, merge, rebase, создание другой ветки не выполнялись.
- Созданные запуском движка source-side binary caches удалены; один изменённый
  tracked cache восстановлен из HEAD побайтно, без изменения Git index.

## B. Архитектура

`Application → LuaRuntime → programs + instances → Lua callbacks → safe ECS API`.
`LuaScriptComponent → ComponentRegistry → scene v4 / clone / Undo snapshots`.
`JSON prefab → isolated registry validation → destination Entity + fresh SceneIdentity`.
`PhysicsSystem → EventBus → bounded queue → Lua on_event`.

VM владеет `LuaRuntime::Impl`; World принадлежит Application. Lua обновляется один
раз за frame перед fixed physics, только в Play и без Pause. Spawn выполняется
синхронно вне ECS iteration; destroy применяется MutationQueue после callbacks.
Reload сначала валидирует candidate без gameplay API, затем меняет revision;
instances пересоздаются на следующем Update. Ошибка candidate сохраняет старую версию.

Подробная схема, API, владельцы, форматы и терминология лекции:
[документация для защиты](lua-scripting-lab2-defense.md).

## C. Матрица требований

PASS в графе реализации означает наличие кода; степень проверки указана отдельно.

| Требование | Реализация | Файлы | Проверка |
|---|---|---|---|
| Embedded Lua 5.4 + sol2 | PASS | cmake/Lua.cmake, scripting/LuaRuntime.cpp | Release build + VM regression |
| RAII, owner thread | PASS | LuaRuntime::Impl, Application.h | lifecycle/Shutdown regression |
| LuaScriptComponent, registry | PASS | ecs/components/LuaScriptComponent.h, ComponentRegistry.cpp | descriptor + scene roundtrip |
| Двусторонние bindings | PASS | LuaRuntime::Impl::Bind/Call | Transform/callback assertions |
| Независимые instances | PASS | LuaRuntime::Impl::Sync | два Entity с одним script, разный state |
| Play/Stop isolation | PASS | Application::SetEditorPlayMode | 20 реальных Application clone/stop циклов |
| Pause/Resume | PASS | Application::Run, SetEditorPaused | headless gate/state; ручной click NOT VERIFIED |
| Protected execution/diagnostics | PASS | Evaluate, Call, Logger | syntax/runtime/type/forbidden API/loop tests |
| Safe Entity handles | PASS | Entity, World::IsAlive, LuaRuntime::Transform | stale generation, foreign World, repeated destroy |
| Input held/pressed/released | PASS | LuaRuntime::DiscoverInput, Application input callback | headless edges; физическая F NOT VERIFIED |
| JSON prefab spawn | PASS | scripting/Prefab.cpp | components, position, fresh ID, rollback |
| Mutation safety | PASS | snapshot IDs, ecs::MutationQueue | spawn/destroy через gameplay, native ECS regressions |
| Настоящее событие | PASS | Application collision listener, EventBus, QueueCollision | EventBus → Lua, demo damage messages |
| Transactional Reload | PASS | LuaRuntime::ReloadScript | 20 Reload, bad/missing file, repair, old version survives |
| Typed fields + schema | PASS | Schema, ValidValue, EditorInspector.cpp | bool/integer/number/string/vec3, overrides, rejection |
| Inspector integration | PASS | EditorInspector.cpp | native frame capture: component/status/Reload/fields; clicks NOT VERIFIED |
| Undo/Redo + dirty + serialization | PASS | existing EditorTransactions + registry Lua JSON | Lua field Undo/Redo, duplicate, scene restore |
| Lua/Prefab Asset Browser | PASS | AssetCatalog.cpp, EditorAssets.cpp | native frame capture: creation buttons; drop/click NOT VERIFIED |
| Create Lua / Create+Instantiate Prefab | PASS | EditorAssets.cpp | implementation reviewed; full interactive flow NOT VERIFIED |
| Wave Survival полностью на Lua | PASS | wave_controller.lua, enemy_chase.lua | headless victory/defeat + rendered enemies |
| Демо scene/assets deployment | PASS | lua_wave_demo.json, LuaGameplayAssets target | native DX12 launch and frame capture |
| Existing Engine regressions | PASS | 32 CTest targets | финальный полный прогон 32/32 после изоляции synthetic input и задания layout |
| Defense documentation | PASS | lua-scripting-lab2-defense.md | architecture, lecture mapping, Q&A, 5–7 min script |
| Автоматический watcher, state migration | NOT IMPLEMENTED | — | дополнительные возможности, не базовый Reload |
| Фоновое чтение scripts через jobs | NOT IMPLEMENTED | — | Lua и ECS остаются на owner thread |
| Полная изоляция враждебного Lua | PARTIAL | sandbox allowlist + instruction hook | нет heap quota / timeout native calls |

## D. Фактически выполненные проверки

Среда: Windows, VS 2022 MSVC x64 Release, Ninja, NVIDIA GeForce RTX 4060 Laptop GPU.
DX12, Vulkan и Tracy оставлены включёнными. Lua 5.4.8 статически, sol2 3.3.1.

```powershell
cmake --preset x64-release -DFETCHCONTENT_FULLY_DISCONNECTED=OFF -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
cmake --build out/build/x64-release --parallel 4
ctest --test-dir out/build/x64-release -R '^LuaScriptingRegression$' --output-on-failure
ctest --test-dir out/build/x64-release --output-on-failure --timeout 180
```

Полная сборка выполнена. После дополнительных проверок typed fields
`LuaScriptingRegression`: **187 assertions passed**, CTest PASS. Последние три
проверки подтверждают, что отвергнутый candidate с math.randomseed не влияет
на случайное состояние работающей VM.
Проверка `git diff --check` не обнаружила whitespace ошибок.

История регрессий зафиксирована без сокрытия отказов:

1. Первый полный CTest: **27/32 PASS**. Editor2 / Editor2Dx12 ожидали 12 builtins.
   Проверка обновлена на 13 и дополнена проверкой LuaScript descriptor; остальные
   assertions сохранены. Три DX12 editor проверки отказали на scale handle capture,
   viewport readback и randomized pixel oracle.
2. Повтор проблемных тестов: Lua и оба Editor2 PASS; Editor25Dx12 PASS;
   LevelWorkflow/Workflow оставались зависимыми от текущих сохранённых layout/preferences.
3. Существующие, собранные до этой задачи binaries из `shadows-dx12` прошли эти
   три проверки со своими preferences. Отдельная проверка текущего binary с тем же
   layout прошла проблемный scale handle capture.
4. Полный прогон текущей сборки с этим известным diagnostic layout:
   **32/32 PASS, 158.37 s**. Исходные пользовательские `imgui.ini` и preferences
   восстановлены; SHA256 до/после совпадают.
5. Повтор через добавленный `RunLuaScriptingDiagnostics.ps1` с тем же fixture:
   **29/32 PASS, 145.39 s**. Снова отказали EditorLevelWorkflowDx12Regression
   (`actual scale plane handle capture 0`), EditorWorkflowDx12Regression
   (`visible light handle captures pointer before mesh`), Editor25Dx12Regression
   (`selected occluded visible range handle picking`). Таким образом, layout
   **не устраняет всю нестабильность**; причины этих pointer/focus отказов пока
   на этом этапе ещё не были установлены. Assertions не отключались.
6. После усиления candidate isolation выполнена полная пересборка всех targets,
   затем отдельно LuaScriptingRegression: **187/187 assertions, PASS**.
7. Диагностика нашла перезапись synthetic mouse input живым GLFW input:
   запрошено `(667,373)`, перед обработкой сцены ImGui видел `(1269,8)`.
   Добавлен `SyntheticEditorInput`: во время синтетического теста принимаются
   только явно поставленные тестом ImGui events; GLFW backend production не менялся.
   Исходные assertions picking/capture/drag/Undo/readback сохранены. Без layout fixture
   raw tests всё ещё зависели от пользовательских размеров viewport.
8. **Последний полный прогон** после этой правки, через
   `RunLuaScriptingDiagnostics.ps1`: **32/32 PASS, 158.46 s**.
   Лог `out/lua-ctest-isolated-final.log`. Пользовательские preferences восстановлены.

Для воспроизведения сохранены `tests/fixtures/lua-diagnostics` и
`tests/RunLuaScriptingDiagnostics.ps1`. Скрипт временно задаёт известный layout,
запускает неизменённые CTest assertions, в finally восстанавливает исходные байты.
Параметр `-UseCurrentLayout` позволяет воспроизвести зависимость от пользовательской
конфигурации. `-CTest` принимает путь к ctest.exe, если его нет в PATH.

Targets, прошедшие в последнем полном прогоне:
PhysicsRegression, ResourceRegression, RenderInterpolationRegression,
GpuInstancingRegression, LightingRegression, MaterialSystemRegression (CPU/DX12),
ShadowMappingRegression (CPU/DX12), LuaScriptingRegression, ShadowQualityDx12Regression,
PbrCoreRegression (CPU/DX12), PbrTexturesRegression (CPU/DX12), IblRegression (CPU/DX12),
LightSystemRegression (CPU/DX12), Editor2Regression (CPU/DX12),
EditorVisualPolishRegression (CPU/DX12/shader fallback),
EditorLevelWorkflowRegression (CPU/DX12), EditorWorkflowRegression (CPU/DX12),
EditorUsabilityRegression (CPU/DX12), Editor25Regression (CPU/DX12).

### Native GUI render smoke

Из отдельной runtime directory с assets/config/shaders запущен настоящий WhispEngine:

```powershell
WhispEngine.exe --lua-demo --frames 180 --capture-frame editor.ppm
WhispEngine.exe --lua-demo --play --frames 1000 --capture-frame play.ppm
```

Штатный `SaveFrameImage` сделал GPU readback **вместе с ImGui**.
Изображения преобразованы PPM → PNG без изменения содержимого и просмотрены.
Подтверждены arena/Core, выбранный WaveController, Lua Script в Inspector,
Enabled, picker, Reload Script, Loaded: 6 fields, Lua/Prefab creation buttons;
в последнем Play-кадре — четыре живых Enemy из prefab и восемь runtime Entity
вместо четырёх (один из пяти успел достигнуть Core). Лог содержит пять spawn,
изменение жизней Core и `Wave Survival stopped` при штатном Shutdown.

Артефакты внутри игнорируемого `out`:

- `out/build/lua-gui-smoke/editor.png`, `play.png`;
- `out/build/lua-gui-smoke/capture-editor.log`, `capture-play.log`;
- `out/lua-build-verified.log`, `lua-build-last.log`;
- `out/lua-ctest.log`, `lua-ctest-recheck.log`, `lua-ctest-diagnostic-layout.log`.
- `out/lua-ctest-final.log` — прогон через добавленный воспроизводимый скрипт.
- `out/lua-ctest-input-isolation.log`, `lua-ctest-isolated-final.log` — диагностика
  и итоговый успешный полный прогон после изоляции synthetic input.

**Ограничение GUI:** `computer-use` обнаружил окно, но доступ к нему завершился
ошибкой `Computer Use app approval timed out`. Поэтому реальная последовательность
мыши/drag-and-drop/F/Inspector Save и Reload click не подтверждена.
Штатный frame capture подтверждает отрисовку и Play, headless/API tests подтверждают
логику, но они не заменяют эту оставшуюся интерактивную проверку.

## E. Изменённые и новые файлы

Изменены:

- `CMakeLists.txt`, `WhispEngine.cpp`, `engine/CMakeLists.txt`;
- `engine/core/Application.h`, `engine/core/Application.cpp`;
- `engine/ecs/ComponentRegistry.h`, `engine/ecs/ComponentRegistry.cpp`;
- `engine/editor/AssetCatalog.cpp`, `engine/editor/EditorAssets.cpp`,
  `engine/editor/EditorInspector.cpp`;
- `tests/Editor2Regression.cpp`.
- `tests/EditorLevelWorkflowRegression.cpp`, `tests/EditorWorkflowRegression.cpp`,
  `tests/Editor25Regression.cpp`: изоляция synthetic input от GLFW desktop events,
  исходные picking/readback assertions сохранены.

Добавлены:

- `cmake/Lua.cmake`;
- `engine/ecs/components/LuaScriptComponent.h`;
- `engine/scripting/LuaRuntime.h`, `LuaRuntime.cpp`, `Prefab.h`, `Prefab.cpp`;
- `engine/assets/scripts/wave_controller.lua`, `enemy_chase.lua`;
- `engine/assets/prefabs/wave_enemy.prefab.json`;
- `engine/assets/scenes/lua_wave_demo.json`;
- `tests/LuaScriptingRegression.cpp`, `tests/RunLuaScriptingDiagnostics.ps1`;
- `tests/SyntheticEditorInput.h`;
- `tests/fixtures/lua-diagnostics/imgui.ini`, `editor-preferences.json`;
- `docs/lua-scripting-lab2-defense.md`, `docs/lua-scripting-lab2-report.md`.

## F. Показ на защите

Точная инструкция на 5–7 минут, включая выбор активной staged Lua-копии,
смену Wave Size 5 → 10, Play/Stop, F, prefab, смену скорости, syntax error,
Reload/repair и C++-файлы для объяснения — в разделе «Демонстрация»
[документации](lua-scripting-lab2-defense.md).

Краткий старт из `out/build/x64-release`: `./WhispEngine.exe --lua-demo`.
Изменять активные файлы `assets/scripts/*.lua`; для хранения в Git перенести
изменения в `engine/assets/scripts`. Target LuaGameplayAssets синхронизирует
source → runtime без компиляции C++.

## G. Ограничения и технический долг

- Обязательная интерактивная GUI-последовательность остаётся NOT VERIFIED.
- Raw CTest с произвольным persisted layout может давать отказы из-за слишком
  малого viewport. Для полного GUI-набора использовать предоставленный скрипт
  нормализации layout с восстановлением. Перезапись synthetic input живым курсором
  устранена в тестах, без изменений production input.
- Randomized Editor25 pixel test в первом прогоне отказал и прошёл при повторе;
  причина этого отдельного нестабильного результата не установлена.
- Нет heap quota и ограничения времени внутри native Lua library calls;
  инструкция hook не превращает sandbox в изоляцию враждебного кода.
- Нет автоматического timestamp watcher, миграции runtime state, parallel VM/jobs.
- Reload намеренно сбрасывает state; сохраняются только сериализованные overrides.
- Lua/Pefab loader читает ограниченные файлы синхронно; prefab JSON не кэшируется.
- Canonical path containment не закрывает внешнюю конкурентную подмену файлов.
- DX12 запуск ожидает рабочую папку сборки для compiled shaders — это существующее
  ограничение движка, его не меняли в рамках Lua subsystem.

## H. Готовность к ревью и merge

Ветка **готова к code review**: код интегрирован, сборка и 187 Lua assertions
проходят, native кадры просмотрены. Последний полный прогон: **32/32 PASS**.
Ранее остававшиеся шесть ручных GUI-сценариев пользователь выполнил успешно.
Прежнее ограничение интерактивной проверки ЛР №2 закрыто его подтверждением.
Merge и push не выполнены.
