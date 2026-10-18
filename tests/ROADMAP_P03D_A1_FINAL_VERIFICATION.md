# P0.3d-A1 — Final Baseline Verification

Дата: 2026-10-18. Ветка `project_system`.
Область: финальная проверка текущего baseline A1/A1.1/A1.2.
Исторические отчёты и неудачные логи сохранены.

## Состояние изменений

- **A1:** явный persisted `worldMode` в schema v4, old missing marker → RootOnly,
  strict mismatch/type validation и staged rollback. Реализация не менялась.
- **A1.1:** исправлен production mesh-picking guard: stale ImGuizmo hover при
  пустом selection не блокирует выбор mesh. Production fix сохранён.
- **A1.2:** исправлено противоречие test harness «скрытый GLFW HWND и обязательный
  native focus». Существующие native focus/readiness assertions сохранены;
  историческая причина отдельного RMB free-fly failure не доказана.

Изучены отчёты
[A1](C:/Users/Тимофей/source/repos/WhispEngine/tests/ROADMAP_P03D_A1_WORLD_MODE_PERSISTENCE_REPORT.md),
[A1.1](C:/Users/Тимофей/source/repos/WhispEngine/tests/ROADMAP_P03D_A11_PICKING_STABILIZATION_REPORT.md),
[A1.2](C:/Users/Тимофей/source/repos/WhispEngine/tests/ROADMAP_P03D_A12_RMB_STABILIZATION_REPORT.md).

## RMB trace без разбиения Run

Возвращён исходный **один `Run` с frame limit 20** после тех же native RMB/W
events. Камера, число кадров, исходный assertion
`Play without player retains RMB free-fly` и input conditions не менялись.
20 отдельных Run и синхронный per-frame console/file output удалены.

Использован существующий **test-only ImGui NewFramePre context hook**:
в Application этот этап находится после camera update и до обработки нового
ImGui кадра, поэтому ещё видны предыдущие UI ownership/hover state.
Production APIs/input architecture не расширялись.

Буфер фиксирован: 24 записи по 2048 bytes (48 KiB). Записываются три исходных
snapshot (Edit ready, Play before press, sent-down) и 20 held-frame snapshot.
Нет per-frame streams/files, sleeps, дополнительных input events или retry loops.
Новая строгая проверка требует ровно 20 frames / 23 records, отсутствие
overflow и truncation. Hook снимается после Run и также RAII при exception.
Полный накопленный trace печатается **только при failure** в NativePlayerInput.

Поля: frame/phase, Play/Pause, Player/PlayerStart, native/synthetic/physical RMB/W,
LMB, ImGui pressed/released, GLFW focus/cursor/capture, foreground PID/ownership,
AppFocusLost, native/synthetic pointer, bounds/hover, ActiveID/HoveredWindow,
WantCaptureMouse, input owner, gizmo и camera active, `firstBlock`.
Это наблюдаемый snapshot после camera update, а не доступ к private edge-state
controller. `firstBlock` отражает доступные guards; отсутствие доступа к
previousRightMouseDown не скрывается. Сам sampling имеет небольшой observer
overhead и не доказывает устранение intermittent timing issue.

## Результаты текущего baseline

Все логи расположены в локальном игнорируемом `out/`; каждый CTest запуск имеет
отдельные summary/detail files. Повторов until-pass, skip и ослабления assertions нет.

| Проверка | Результат | Источники в out/ |
|---|---|---|
| Release build | PASS | p03da1-final-release-build.log |
| SceneWorldModeRegression | 1/1 PASS, 406 checks, 0.63 s | p03da1-final-SceneWorldModeRegression-{ctest,detail}.log |
| Стандартный LuaGameplayControllerDx12Regression | 1/1 PASS, 226 checks, 20.41 s | p03da1-final-LuaGameplayControllerDx12Regression-{ctest,detail}.log |
| Editor/Project/Hierarchy profile | 16/16 PASS, 55.41 s | p03da1-final-profile-{ctest,detail}.log |
| Один полный CTest | **61/61 PASS**, 373.25 s, exit code 0, без повторов | p03da1-final-full-{ctest,detail}.log |

226 checks вместо 244 в A1.2: удалены 19 дополнительных Run-return checks,
добавлена одна проверка полноты bounded trace; исходные camera assertions
не удалялись и не ослаблялись. В стандартном native запуске прошли все исходные
Edit/Play/Player/capture/Stop/fullscreen/picking/gizmo сценарии. Пустой failure
trace на PASS ожидаем: полнота буфера проверена отдельным assertion.

В полном CTest также прошёл стандартный LuaGameplayControllerDx12Regression
(22.30 s) с исходным RMB assertion, непрерывным Run и проверкой полноты trace.
Failed assertions/blockers в текущем прогоне не наблюдались, поэтому failure
trace не печатался. Разницы между последним успешным и неуспешным кадрами
текущего прогона нет; исторический failure trace отсутствовал в старых full logs.

Полный per-test результат и output сохранены в
[full summary](C:/Users/Тимофей/source/repos/WhispEngine/out/p03da1-final-full-ctest.log)
и [full detail](C:/Users/Тимофей/source/repos/WhispEngine/out/p03da1-final-full-detail.log).
Profile включает Project Core/System/Settings CPU/DX12, ECS Hierarchy,
Runtime Hierarchy CPU/DX12, Transform Contract, Editor2/Workflow CPU/DX12,
MultiSelection CPU/DX12 и ScalePlaneGizmo. В нём сохранены прежние checks:
Runtime 296 / DX12 16, ECS Hierarchy 261, Transform 718, MultiSelection 2550 / 395.

**DX12 validation — 0 errors:** GetValidationErrorCount()==0 assertions в
успешных native tests прошли; D3D12 ERROR/CORRUPTION и DX12 validation-failure
markers в standalone native/profile/full detail logs — 0. Shutdown validation
failure markers отсутствуют. Ожидаемые Lua/asset negative-test сообщения не
приравниваются к GPU validation errors.

## Файлы и границы

Изменения только этой финальной проверки:

- `tests/LuaGameplayControllerRegression.cpp` — bounded in-memory trace и
  непрерывные 20 кадров вместо A1.2 per-frame Run/stream logging.
- Этот отчёт.

Снимок до этапа: `out/p03da1-final-start-hashes.json`, **826** файлов.
Production-код, SceneSerializer/worldMode, World DataMode, Collider semantics,
Application ownership, HierarchyGraph, пользовательские Lua/scenes/assets и
EventBus не менялись. Все предыдущие незакоммиченные изменения сохраняются.
Commit/push/merge/rebase/reset/clean не выполнялись.

Финальные проверки: из 826 исходных файлов изменён только указанный test source,
все остальные прежние файлы совпадают с начальным SHA256 snapshot. Все
**89 assets + EventBus (90/90)** сохранены. `git diff --check` — **PASS**;
whitespace check этого нового отчёта — PASS. Старые A1/A1.1/A1.2 отчёты
не переписывались и остаются историческими; текущий итог фиксируется здесь.

## Остаточные риски / решение

Исторический RMB failure остаётся **intermittent issue без доказанного root cause**.
Он не объявляется исправленным по isolated PASS и не приписывается внешнему
focus без наблюдаемого failure trace. Предыдущий full 60/61 остаётся историческим
результатом A1.1, не перезаписывается текущими проверками.

**Решение: READY для перехода к P0.3d-A2 на текущем проверенном baseline.**
A1 persistence и A1.1 picking функционально и регрессионно проверены на этом
состоянии: Release PASS, SceneWorldMode 406 checks PASS, native/controller PASS,
profile 16/16 PASS, full CTest 61/61 PASS, DX12 validation 0, diff check PASS.
Исправление A1.2 visibility/readiness также включено в проверенную сборку.

Рекомендуется переходить к A2 с сохранением RMB issue в техническом долге:
«периодический отказ Play без PlayerStart / RMB free-fly; root cause не доказана;
при следующем воспроизведении сохранить bounded continuous trace и первый
наблюдаемый blocker». Единственный успешный full run не доказывает полного
устранения нестабильности. Bounded observer снижает прежнее timing distortion,
но не даёт нулевого observer effect. DPI/monitor конфигурации этим прогоном
исчерпывающе не проверены.

**A2 в рамках этого задания не начинался.** Публичный Hierarchy Editor,
Tree/Reparent/Gizmo/Prefab features по-прежнему не включаются.
