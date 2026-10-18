# P0.3d-A1.2 — Native RMB Free-Fly Stabilization

Дата: 2026-10-18. Ветка `project_system`.
**Verdict: INCOMPLETE — историческая причина RMB failure не установлена.**

Применено условие остановки раздела 5 задания: после ограниченной диагностики
исходный RMB отказ не воспроизведён с trace, поэтому speculative camera/input
workarounds и изменения CTest orchestration не добавлялись. Общий зелёный
baseline не заявляется; к P0.3d-A2 перехода нет.

## Исходные данные

Прочитаны отчёты
[A1.1](C:/Users/Тимофей/source/repos/WhispEngine/tests/ROADMAP_P03D_A11_PICKING_STABILIZATION_REPORT.md),
[A1](C:/Users/Тимофей/source/repos/WhispEngine/tests/ROADMAP_P03D_A1_WORLD_MODE_PERSISTENCE_REPORT.md)
и замечания P0.2 о native focus/capture failures. Исторический full CTest A1.1:
**60/61**, assertion `Play without player retains RMB free-fly`, после успешного
fullscreen picking; source logs — `out/p03da11-full-{ctest,detail}.log`.
Исторические логи не перезаписывались и не заменяются текущими isolated PASS.

Узкий аудит ограничен NativePlayerInput, Application::UpdateCameraController,
Play/Stop camera transitions, GLFW input/window lifecycle, Editor input routing
и CTest properties этого native executable. SceneSerializer/worldMode,
Collider semantics, Application ownership и HierarchyGraph не изменялись.

## Условия production activation

Без Lua camera lease editor free-fly разрешён и в Edit, и в Play без Player.
UpdateCameraController требует native focus и RMB=GLFW_PRESS, допускающий
camera input owner, отсутствие modal/text/tool blocking, отсутствие LMB и
нахождение native cursor внутри hovered viewport до начала capture.
Новый capture начинается на разрешённом RMB edge; при удержании камера
сохраняет capture, не требуя hover под подавленным виртуальным ImGui cursor.
При RMB release/focus loss capture освобождается. Lua camera lease исключает
editor free-fly. Stop сохраняет существующее правило release-before-recapture.
W обрабатывается по GLFW key state после успешного capture.

Ни одно из этих правил не менялось ради теста.

## Добавленная диагностика

В NativePlayerInput логируются Edit readiness, Play до RMB, отправка WM_RBUTTONDOWN
и каждый из исходных 20 held frames. Сохранены исходные native SendMessage/GLFW
input paths и failing assertion. Диагностика содержит:

- frame, Edit/Play/Pause, Player/PlayerStart counts и camera active;
- GLFW focus, AppFocusLost, cursor mode, HWND capture ownership;
- foreground PID/self PID и принадлежность foreground HWND;
- GLFW RMB/W, физические GetAsyncKeyState RMB/W, синтетический ImGui RMB;
- ImGui pressed/released, WantCaptureMouse, HoveredWindow/ActiveID;
- native/synthetic pointer, viewport bounds/hover, fullscreen и gizmo states;
- `firstBlock`: первое наблюдаемое условие, препятствующее activation, либо eligible.

Pressed/released относятся к ImGui; `nativeRmb` показывает удержание в GLFW.
Во время camera capture ImGui mouse намеренно подавлен, поэтому syntheticRmb=0
и syntheticMouse=-FLT_MAX при camera=1 не означают потерю native RMB.
WM messages создают GLFW states без физического нажатия: physicalRmb/W=0 в
этом harness ожидаемы и не доказывают production input failure.

Для покадрового trace исходные 20 frames выполняются как 20 вызовов Run с
frame limit 1. Это не retry до успешного capture и не увеличивает число frames,
но логирование/границы Run меняют timing: отсутствие failure не доказывает
исправление timing-sensitive причины. `firstBlock` — snapshot доступных условий
после кадра, не hook внутри private camera controller.

Добавлен явный диагностический CLI selector `--native-player-input`, запускающий
только соответствующий scenario. Стандартный CTest по-прежнему вызывает
`--dx12` со всеми четырьмя native scenarios; assertions не пропускаются.

## Наблюдения и точная граница доказательств

### 1. Обнаружен отдельный test harness defect readiness

Первый диагностический стандартный запуск остановился **до RMB**:
`picking transition/framebuffer/focus ready within frame budget`.
Лог `out/p03da12-diagnostic-detail.log`: native window hidden (`visible=0`),
`GLFW_FOCUSED=0`, при этом synthetic ImGui AppFocusLost=0; sizes/bounds/target
согласованы. NativeViewportDisplay вызывал glfwHideWindow, хотя readiness A1.1
требовал настоящий native focus. Синтетический AddFocusEvent не фокусирует HWND.

Минимальное исправление harness: NativeViewportDisplay показывает своё окно
и запрашивает GLFW/Win32 focus перед прежними bounded readiness assertions.
Требование focus и сам picking assertion не убраны. Это обоснованное изменение
window lifecycle, а не повторная диагностика уже исправленного mesh hit-test.
Production fix stale ImGuizmo из A1.1 сохранён без изменений.

**Этот отдельный дефект не доказывает причину исторического RMB assertion**
в последующем NativePlayerInput с другим Application/HWND.

### 2. Собственно RMB failure с новой диагностикой не пойман

Изолированный NativePlayerInput: PASS, 107 checks
(`out/p03da12-targeted-rmb.log`).
До press: focus=1, AppFocusLost=0, native/synthetic pointer=(370,289),
viewport=(12,210)–(729,368), hover=1, RMB/W up, Player/PlayerStart=0.
После WM_RBUTTONDOWN/W: native RMB=1, W=1, firstBlock=eligible.
На первом кадре capture: camera=1, GLFW_CURSOR_DISABLED (212995),
HWND capture принадлежит тесту. Capture сохраняется все 20 frames.

В этом **успешном** запуске foregroundOwn=0 и foreground PID отличался от PID
теста при native focus=1. Следовательно, несовпадение foreground HWND само по
себе не является доказанной причиной RMB отказа; speculative foreground lock
или изменение camera semantics не добавлялись.

Стандартный native executable после исправления видимости readiness:
PASS, 244 checks, 13.83 s (`out/p03da12-sequence-diagnostic-{ctest,detail}.log`).
В нём foregroundOwn=1; capture также включился на первом held frame.
Возможное влияние показанного predecessor HWND на timing/focus не отделено
от исходной непостоянности RMB failure — причинная связь не заявляется.

## Isolated / ordered CTest и orchestration

Фактический CTest inventory сохранён в `out/p03da12-ctest-inventory.json`:
command=`LuaGameplayControllerRegression.exe --dx12`,
working directory=`out/build/x64-release`, environment=`WHISP_DX12_DEBUG=1`,
**RUN_SERIAL=TRUE**. CTEST_PARALLEL_LEVEL в окружении не установлен;
команды этого этапа не задавали parallelism. Этот CTest уже не может
перекрываться с другими tests внутри того же CTest invocation.

Выполнена одна ограниченная упорядоченная последовательность:
ProjectCore, ProjectSystem CPU/DX12, ProjectSettings CPU/DX12, затем проблемный
native controller test. **6/6 PASS**, 29.79 s
(`out/p03da12-ordered-diagnostic-{ctest,detail}.log`). Controller: 244 checks,
13.42 s. Те же args/environment/working directory, foregroundOwn=1,
capture начинается на первом frame и сохраняется.

Различие с историческим full CTest остаётся не локализованным: в старом full
нет покадровых RMB guard snapshots. Конкуренция разных native CTest процессов
не наблюдалась. RUN_SERIAL уже есть; дополнительных global locks или
сериализации всей suite без доказательств не добавлено.

## Результаты запусков

| Запуск | Фактический результат | Источник в out/ |
|---|---|---|
| Diagnostic Release graph | PASS | p03da12-diagnostic-build.log |
| Diagnostic standard native до visibility fix | FAILED до RMB, native focus readiness, 8.99 s | p03da12-diagnostic-{ctest,detail}.log |
| Targeted NativePlayerInput | PASS, 107 checks | p03da12-targeted-rmb.log; p03da12-targeted-build.log |
| Standard native после visibility fix | PASS, 244 checks, 13.83 s | p03da12-sequence-diagnostic-{ctest,detail}.log; p03da12-sequence-diagnostic-build.log |
| Ordered Project → native controller | 6/6 PASS, 29.79 s | p03da12-ordered-diagnostic-{ctest,detail}.log |
| Final Release graph | PASS | p03da12-final-release-build.log |
| Full CTest этого этапа | **Не запускался**: RMB причина не установлена, сработало условие остановки | последний исторический full остаётся 60/61 |

Количество checks выросло за счёт отдельных frame Run assertions при trace;
исходные camera/picking assertions не ослаблялись и не удалялись.
Полная post-fix верификация из раздела 8 (включая отдельный SceneWorldMode,
полный native/Editor/Gizmo профиль и один full CTest) **не выполнена**: доказанного
RMB fix нет. Диагностические PASS не объявляются завершённой верификацией.

DX12 assertions в успешных стандартных native runs прошли; ERROR/CORRUPTION
markers в диагностических логах — 0. Финальные diff/hash checks приведены ниже.

## Файлы, сохранность и remaining blocker

Изменены только `tests/LuaGameplayControllerRegression.cpp` (trace, диагностический
selector, видимость окна в readiness scenario) и этот новый отчёт.
Production input/camera код не менялся. Начальный snapshot:
`out/p03da12-start-hashes.json`. Все ранее существовавшие изменения A1/A1.1
сохраняются, включая production picking guard и worldMode implementation.
Пользовательские Lua/scenes/assets, EventBus и конфигурация не изменялись.
Git commit/push/merge/rebase/reset/clean не выполнялись.

Финальная проверка: из **825** прежних файлов изменён только указанный test
source; все остальные прежние файлы совпадают по SHA256, включая A1/A1.1.
Защищённые assets/EventBus — **90/90 без изменений**.
`git diff --check` и whitespace check нового отчёта — **PASS**.

**Unresolved blocker:** нет trace неуспешного RMB activation/capture transition.
Не установлено, какой из guards либо какой native input/focus transition
нарушается в историческом full-only failure. Дальнейшее исправление должно
опираться на такой trace, а не на isolated PASS, повышение timeout или
предположение о foreground desktop. READY не заявляется. К A2 перехода нет.
