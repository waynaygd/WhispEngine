# Project System Stabilization Report

Дата: 2026-10-17. Ветка: `project_system`. Конфигурация: MSVC x64 Release, DX12 и debug layer (`WHISP_DX12_DEBUG=1`).

Вердикт: **READY FOR ROADMAP v1.0**.

## Исходное состояние и границы

Все шесть запрошенных документов/логов найдены и изучены: `tests/PROJECT_SYSTEM_MVP_REPORT.md`, `docs/PROJECT_SYSTEM.md`, `out/project-ctest-full-verified.log`, `out/project-ctest-full-final.log`, `out/project-ctest-isolated-repeats.log`, `out/project-profile-verified.log`.

Предыдущий полный результат: 50/54 PASS. Рабочее дерево уже содержало Project System MVP и пользовательские изменения. Исходный статус сохранён в `out/stabilization-initial-status.txt`, SHA256 защищённых файлов — в `out/stabilization-protected-hashes.json`. Чистого baseline до Project System нет; данный отчёт не утверждает, что обнаруженные ошибки существовали до MVP.

На исходных Release-бинарниках повтор четырёх тестов дал 2/4 PASS: обе Gameplay API проверки упали, Physics Events и Editor25 в этом запуске прошли (`out/stabilization-baseline.log`). Последующая инструментальная диагностика отдельно воспроизвела оба нестабильных сбоя.

## Причины и исправления

### A. LuaGameplayApiRegression — устаревший контракт Concert

Актуальный пользовательский `ConcertController.lua` устанавливает `active=false` в `on_start`; только событие `start_concert` запускает вращение и пульсацию. Старый тест ожидал поворот сразу после первого Update.

Тестовый Lua-отправитель использует реальный `World.send_event`, очередь доставляется следующим Update. Теперь CPU и DX12 проверяют нулевой поворот и неизменный свет до события; после него — поворот 0.05 радиан за 0.1 секунды и интенсивность `100 + sin(0.2) * 80`. Сохранены проверки стабильных Entity References, CloneData, дискового Save/Load, изоляции Editor World, Undo/Redo, удаления и generation reuse, hot reload, Transform/Light API и реального HDR-рендера. Добавлен Stop/повторный Play с новым неактивным экземпляром и повторной доставкой события.

Пользовательская игровая механика не менялась. При проверке полных логов отправитель дополнительно вынесен в отдельный `concert-signal.lua` внутри уникального fixture-каталога: hot reload другого тестового скрипта больше не меняет его программу. Transform/Light native callbacks теперь отдельно обязаны оставаться здоровыми. Это устраняет побочные ошибки fixture, обнаруженные после первого зелёного полного прогона.

### B. LuaGameplayApiDx12Regression — сохранённая раскладка и неверное окно ввода

Диагностика: координаты источника `(795,190)`, фактическое перемещение `(12,5)`, квадрат дистанции 169 при пороге 6; мышь удерживалась, но `HoveredWindow` и `ActiveIdWindow` были **Inspector**, а не Scene Hierarchy. Панели перекрывались/делили область из сохранённой раскладки. Порог drag был пройден; события доходили, но не до строки Hierarchy (`out/stabilization-drag-diagnostic.log`).

Harness явно сбрасывает docking layout перед первыми UI-кадрами, проверяет попадание координат в видимую Hierarchy и сохраняет настоящие mouse-down/move/release, `BeginDragDropSource`, payload и Inspector drop. Продолжительность задержек и drag threshold не увеличены. Сохранена диагностика на случай повторного отказа.

Native-сценарий выполняется с тремя значениями Application frame delta: 1/30, 1/60 и 1/144 секунды. Частоты относятся к часам Application; GLFW/ImGui сохраняют native clock и реальную последовательность mouse input. Production ImGui/input-код не менялся.

### C. LuaPhysicsEventsDx12Regression — wall-clock не гарантировал физический тик

До исправления цикл делал sleep вне `Application::Run`, затем запускал по 8 быстрых кадров. Вход в Run сбрасывает fixed-step clock и первый delta; накопленный во внешнем sleep интервал терялся. При быстром DX12 эти кадры не набирали 1/60 секунды.

На четвёртом диагностическом повторе зафиксировано: `enter=1 exit=0 timer=61 simSeconds=0 ticks=0 dt=0.0020304 targetX=20 sensorX=0`. Между перемещением объекта и assertion физика не продвинулась (`out/stabilization-trigger-diagnostic.log`).

Изучен путь: положительный fixed dt → PhysicsSystem::UpdateTriggers → сравнение trigger pairs → EventBus → LuaRuntime::QueueCollision → следующий Lua Update → on_event. Pause сохраняет пары; выход корректно отправляется на следующем положительном физическом тике. Дефект trigger pairs или очереди не подтверждён.

Добавлен минимальный закрытый адаптер времени: Time по умолчанию использует steady_clock; только friend `ApplicationTestAccess` может задать тестовый delta. Тест сохраняет настоящий Application::Run, fixed scheduler, PhysicsSystem, EventBus и Lua. В каждом сценарии при 30/60/144 FPS проверяются реальные тики, ровно один Enter/Exit, отсутствие дублей после дополнительных тиков, заморозка Pause, timers, queries, hot reload и Stop. Sleep удалён; семантика физики не менялась.

### D. Editor25Dx12Regression — сравнение разных стадий finalization

Seed сохранён: `0x25c0ffee`, 80 итераций. Первый диагностический сбой: iteration=1, 1008 изменённых байт, первая координата `(152,26)`, максимальная разница 255. Снимки локализовали разницу в прямоугольнике x=152..167/y=26..46: верхний helper cube с missing texture, чёрный фон против белого fallback.

Повтор с версиями воспроизвёл сбой после шести успешных запусков: `missingBefore=8/1/2 missingAfter=9/2/2` (handle/GPU version/CPU version), `loading=0`. То есть на первом снимке GPU-версия 1 уже не соответствовала CPU-версии 2; следующий кадр загрузил актуальную версию. Полный лог: `out/stabilization-material-versions.log`; начальная диагностика: `out/stabilization-material-diagnostic.log`; PPM/JSON: `out/diagnostics/editor25/mismatch-*`.

Harness ожидал CPU jobs только перед последним из восьми warmup-кадров. CPU finalization могла именно тогда сменить версию; RenderSystem::FinalizeTextureGpu отклоняет устаревшую заявку и ставит новую на следующий кадр. `loadingCount==0` не означает, что GPU-версия актуальна.

Исправление: DrainProjectLoads до сравнения, затем явная проверка текущих GPU-версий selected/blue/missing/default текстур в пределах прежнего лимита 8 кадров. При неготовности тест падает отдельным assertion. Побайтовое ordinary/instanced сравнение, независимые цветовые oracle и их допуски, isolation материалов, unload/reload и отдельное владение default GPU handle сохранены. Добавлены failure-only снимки/состояние и версии.

Readback выполняется через ExecuteImmediateCommandList с fence перед Map; порядок GPU-команд и retirement изучены. Дефект fence/instancing не подтверждён. Смена проектов и очистка RenderSystem/ResourceManager отдельно проверяются ProjectSystemDx12Regression.

## Узкий аудит Project System

Изучены ProjectContext/ProjectManager, ApplicationProject, AssetPaths, ResourceManager::DrainProjectLoads/ClearAll и RenderSystem::ReleaseGpuResources; сохранена архитектура одного ResourceManager, двух миров, scene schema v4 и Legacy Mode.

Профильные тесты проверяют Create/Open/Close, startup scene, Save/Discard/Cancel, Edit/Play/Pause, безопасный preflight до разрушения активного состояния, сброс selection/Undo/runtime/Lua/cursor, независимость каталогов и одинаковых Lua/texture keys, отсутствие fallback на чужие legacy assets, активный hot reload, перенос Unicode-проекта, prefab/material/scene roundtrip и retirement старых GPU handles.

ProjectCoreRegression дополнен проверками drive-relative `C:outside`, UNC, успешной публикации startup descriptor, отклонения stale descriptor и отсутствующей startup scene с сохранением descriptor, удаления временных файлов. Windows junction создаётся непосредственно через FSCTL_SET_REPARSE_POINT: ResolveAsset и Create через junction обязаны отказать, удаление fixture не затрагивает target. Этот тест прошёл. Создание настоящего symlink по-прежнему недоступно без Windows privilege; junction обеспечивает выполняемую проверку reparse-point защиты, но не выдаётся за отдельный symlink-прогон.

Новых подтверждённых production-дефектов Project System в этом аудите не найдено. Новые возможности Roadmap не реализованы.

## Файлы, изменённые именно этим проходом

| Файл | Назначение |
| --- | --- |
| `engine/core/Application.h` | friend-доступ тестового адаптера к Time; существующие MVP-изменения сохранены |
| `engine/core/Time.h`, `engine/core/Time.cpp` | необязательный закрытый тестовый delta; steady_clock по умолчанию |
| `tests/ApplicationTestAccess.h` | проверенная подстановка delta без публичного gameplay API |
| `tests/LuaGameplayApiRegression.cpp` | актуальный event-контракт, повторный Play, docking и native 30/60/144 FPS |
| `tests/LuaPhysicsEventsRegression.cpp` | fixed ticks вместо sleep, три частоты, проверки отсутствия дублей |
| `tests/Editor25Regression.cpp` | барьер готовности CPU/GPU и диагностика без изменения визуальных assertions |
| `tests/ProjectCoreRegression.cpp` | drive-relative/UNC, descriptor и настоящий junction |
| `tests/PROJECT_SYSTEM_STABILIZATION_REPORT.md` | этот отчёт |

Остальные изменения, отображаемые Git, существовали до stabilization pass. `Application.cpp` и пользовательские ассеты этим проходом не редактировались.

## Верификация

- Полная Release build: PASS, `out/stabilization-build-2.log`; сборки после fixes и изоляции fixture: PASS, `out/stabilization-release-build.log`, `out/stabilization-release-build-final.log`.
- Профильный прогон исправлений (до отдельного sender-файла): **8/8 PASS**, 64.97 сек, `out/stabilization-profile.log`; полные выводы — `out/stabilization-profile-detail.log`.
- Project System: **3/3 PASS** в профильном прогоне.
- Lua Gameplay CPU/DX12, Lua Physics CPU/DX12 и Editor25 DX12: PASS в профильном прогоне.
- Пять последовательных повторов каждого из трёх native-тестов: **15/15 запусков PASS**, 241.25 сек, `out/stabilization-repeats.log` и `out/stabilization-repeats-detail.log`.
- После выделения sender-файла: Gameplay API CPU и DX12 по пять раз подряд — **10/10 PASS**, 84.38 сек, `out/stabilization-gameplay-repeats-final.log`; побочных nil-ошибок sender больше нет. Physics/Editor25 binaries с успешными пятью повторами не менялись.
- Окончательный полный CTest №1: **54/54 PASS, 0 FAIL**, 308.67 сек, `out/stabilization-ctest-full-3.log`; полный вывод — `out/stabilization-ctest-full-3-detail.log`.
- Окончательный полный CTest №2: **54/54 PASS, 0 FAIL**, 263.26 сек, `out/stabilization-ctest-full-4.log`; полный вывод — `out/stabilization-ctest-full-4-detail.log`.
- DX12 validation ERROR/CORRUPTION: **0**; native assertions GetValidationErrorCount()==0 пройдены, оба полных вывода проверены на validation/shutdown errors (`out/stabilization-validation-audit.json`). Ожидаемые ошибки negative Lua/shader-fallback tests не являются DX12 validation errors.
- `git diff --check`: **PASS, exit 0**, `out/stabilization-diff-check.log`. Только уведомления Git о нормализации CRLF/LF.
- SHA256 `ConcertController.lua`, `FirstPersonController.lua`, `EventBus.h`: **3/3 совпадают с исходными**, `out/stabilization-protected-verification.json`.

Два промежуточных полных прогона до изоляции sender-fixture также дали 54/54 PASS: 326.48 и 318.90 сек (`out/stabilization-ctest-full-1.log`, `out/stabilization-ctest-full-2.log`). В приёмке окончательного состояния используются отдельные полные прогоны №3 и №4.

## Ограничения

Результаты относятся к текущей Windows/DX12 машине и Release-конфигурации. Прохождение конечного числа повторов не доказывает отсутствие всех возможных проблем. Настоящий symlink требует недоступной привилегии; junction реально проверен. Физический перенос на другой диск/компьютер, device removal/OOM, принудительное завершение процесса во время записи и конкурирующий злонамеренный файловый процесс не тестировались. Эти ограничения не заменяют результаты обязательных проверок.

Commit/push/merge/rebase/reset/clean и смена ветки не выполнялись.

## Финальный Git status

Из исходного status не исчезла ни одна запись. Финальный status содержит 37 modified и 10 untracked записей. Рабочее дерево намеренно остаётся незакоммиченным. Полный снимок: `out/stabilization-final-status.txt`.

```text
 M CMakeLists.txt
 M WhispEngine.cpp
 M engine/CMakeLists.txt
 M test_projects/legacy_workspace/Assets/scripts/ConcertController.lua
 M engine/assets/scripts/FirstPersonController.lua
 M engine/core/Application.cpp
 M engine/core/Application.h
 M engine/core/AssetPaths.cpp
 M engine/core/AssetPaths.h
 M engine/core/Time.cpp
 M engine/core/Time.h
 M engine/ecs/events/EventBus.h
 M engine/ecs/systems/RenderSystem.cpp
 M engine/editor/AssetCatalog.cpp
 M engine/editor/AssetCatalog.h
 M engine/editor/AssetImport.cpp
 M engine/editor/EditorAssets.cpp
 M engine/editor/EditorImportUi.cpp
 M engine/editor/EditorLayer.cpp
 M engine/editor/EditorLayer.h
 M engine/editor/EditorPreferences.cpp
 M engine/editor/EditorPreferences.h
 M engine/editor/EditorTransactions.cpp
 M engine/editor/EditorUi.cpp
 M engine/editor/SceneCatalog.h
 M engine/render/IRenderAdapter.h
 M engine/render/backends/dx12/Dx12EditorVisuals.cpp
 M engine/render/backends/dx12/Dx12RenderAdapter.h
 M engine/resources/ResourceManager.cpp
 M engine/resources/ResourceManager.h
 M engine/resources/loaders/MeshLoader.cpp
 M engine/resources/loaders/TextureLoader.cpp
 M engine/scripting/LuaRuntime.cpp
 M engine/scripting/Prefab.cpp
 M tests/Editor25Regression.cpp
 M tests/LuaGameplayApiRegression.cpp
 M tests/LuaPhysicsEventsRegression.cpp
?? docs/PROJECT_SYSTEM.md
?? engine/core/ApplicationProject.cpp
?? engine/core/ProjectContext.cpp
?? engine/core/ProjectContext.h
?? engine/editor/EditorProject.cpp
?? tests/ApplicationTestAccess.h
?? tests/PROJECT_SYSTEM_MVP_REPORT.md
?? tests/PROJECT_SYSTEM_STABILIZATION_REPORT.md
?? tests/ProjectCoreRegression.cpp
?? tests/ProjectSystemRegression.cpp
```
