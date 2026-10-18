# P0.3d-A1.1 — Fullscreen Picking Stabilization

Дата: 2026-10-18. Ветка `project_system`. Работа ограничена fullscreen mesh
picking и его native regression; P0.3d-A2 не начинался.

## Root cause: production guard, не SceneSerializer и не desktop timing

`EditorLayer::DrawLightGizmos` выполняет выбор mesh через CPU ray/AABB.
В этом пути нет GPU entity-ID picking buffer. До исправления условие входа
требовало `!ImGuizmo::IsOver()` даже при **пустом selection**, когда transform
gizmo вообще не рисуется. ImGuizmo хранит глобальный последний transform и
operation; IsOver вычисляет попадание по этому контексту. Enable(false/true)
при F11 не очищает матрицы/operation. DrawGizmo при пустом selection возвращает
управление до Manipulate, поэтому старый hover остаётся доступен picking guard.

Воспроизведение с диагностикой до исправления:
`out/p03da11-diagnostic-{ctest,detail}.log`, **FAILED** на исходном assertion
`fullscreen mouse picking selects Entity`.

| Состояние | Наблюдение в кадре клика 583 |
|---|---|
| GLFW window / framebuffer | 1280×720 / 1280×720 |
| Content DPI / ImGui framebuffer scale | (1,1) / (1,1) |
| Focus | GLFW focused=1, AppFocusLost=0 |
| ImGui display / viewport bounds | 1280×720 / (0,0)–(1280,720) |
| Mouse / normalized picking UV | (640,360) / (0.5,0.5); ray NDC=(0,0) |
| Render target | 1280×720, GPU readback size уже проверен assertion |
| Hover / overlap | hover=1, HoveredWindow=Viewport##Maximized |
| Capture | WantCaptureMouse=1, GLFW_CURSOR_NORMAL; active ID до клика=0 |
| Input edge | MouseDown[0]=1, IsMouseClicked(0)=1 |
| Gizmo | IsOver=1, IsUsing=0, selection отсутствует |
| Expected / actual Entity | 5:1 / invalid (4294967295:0) |

То есть click приходит правильно, target и координаты согласованы. Блокируется
именно ветка mesh picking из-за stale gizmo hover. WantCaptureMouse=1 здесь
нормален для ImGui viewport и сам по себе не является причиной отказа.
Числовой active ID, появляющийся после кадра клика, также не доказывает
перекрытие: до клика active ID=0, hover принадлежит нужному viewport.

Контрольное изменение **только production guard**, с тем же первоначальным
тестом и диагностикой: `out/p03da11-fixed-isolated-{ctest,detail}.log`,
**PASS**, 19.86 s. До клика все перечисленные параметры, включая IsOver=1 и
пустой selection, те же; после кадра клика selected=5:1. Это подтверждает
причину без изменения mouse coordinates, допуска или самого assertion.

Категории focus loss, window/framebuffer lag, coordinate mismatch, DPI scaling,
render-target resize и overlap не объясняют наблюдавшийся исходный сбой.
Проверка выполнена при DPI=1; произвольные monitor/DPI конфигурации этим
наблюдением не объявляются проверенными. Предыдущая гипотеза о native desktop
нестабильности для этого assertion не требуется: дефект воспроизводим и устранён
конкретным production изменением.

## Исправление

Mesh picking игнорирует IsOver, если нет выбранного Entity, сохраняя остальные
guards: IsUsing, active item, modal/input text, camera capture и прочие условия.
При существующем selection правила взаимодействия с настоящим gizmo прежние.
Это соответствует уже существующему guard в Fog picking.

ImGuizmo dependency, transform/gizmo framework и UI архитектура не менялись.
Selection по-прежнему устанавливается настоящим SelectViewportEntity после
реального mouse click и ray/AABB hit, а не напрямую из теста.

## Regression / readiness

Исходный failing assertion сохранён. Тест также выбирает cube после
fullscreen → windowed → fullscreen и после resize fullscreen viewport до
980×660 (mouse=(490,330), UV=(0.5,0.5), target=980×660).
Для каждого сценария selection сначала очищается; выбор выполняет реальный
Editor input/picking path. Проверки gizmo, отмены transaction через F11,
возврата dock/layout и Play/Pause fullscreen также сохранены.

Добавлена bounded readiness проверка (не более 12 дополнительных frames):
native focus, AppFocusLost, согласованность window/framebuffer/ImGui sizes,
viewport pixel dimensions и наличие viewport texture. До click проверяются
GPU readback dimensions и hover актуального viewport. Произвольных sleeps,
skip, until-pass и ослабления assertions нет. Диагностика печатает состояние
до клика, на down frame и после release; при readiness failure печатается
not-ready state и тест падает, а не пропускается.

## Почему исходный SceneSerializer не устранял сбой A1

[Отчёт A1](C:/Users/Тимофей/source/repos/WhispEngine/tests/ROADMAP_P03D_A1_WORLD_MODE_PERSISTENCE_REPORT.md)
сохраняет исторический результат 60/61 и отдельную проверку executable с
исходной реализацией SceneSerializer из HEAD. Mesh picking guard и глобальное
состояние ImGuizmo в таком executable оставались прежними. NativeViewportDisplay
создаёт сцену через NewScene/CreatePrimitive; persisted worldMode не участвует
в участке fullscreen click. Теперь причина подтверждена диагностикой и
контрольной сменой только production picking guard. A1 implementation не менялась.

## Проверки

- Diagnostic Release build — PASS (`out/p03da11-diagnostic-build.log`).
- Diagnostic isolated CTest до исправления — **FAILED**, сохранён как есть.
- Узкий production fix, прежний assertion — **PASS**, 19.86 s
  (`out/p03da11-fixed-build.log`, `out/p03da11-fixed-isolated-{ctest,detail}.log`).
- Окончательный Release build после расширения regression — **PASS**
  (`out/p03da11-release-build.log`).
- Три заранее заданных независимых последовательных запуска окончательного
  native test, по **225 checks PASS** в каждом:

  | Запуск | Результат | Время | Отдельные логи в out/ |
  |---|---|---|---|
  | 1 | PASS | 20.22 s | p03da11-isolated-1-{ctest,detail}.log |
  | 2 | PASS | 18.93 s | p03da11-isolated-2-{ctest,detail}.log |
  | 3 | PASS | 19.93 s | p03da11-isolated-3-{ctest,detail}.log |
- Профильный CTest: **12/12 PASS**, 72.10 s
  (`out/p03da11-profile-{ctest,detail}.log`): SceneWorldMode (406 checks),
  Project Core/System/Settings, Editor Level Workflow/Workflow и MultiSelection
  CPU/DX12.
- **Один полный CTest: 60/61 PASS, 1 FAILED**, 359.94 s, exit code 8,
  без автоматических или ручных повторных полных прогонов
  (`out/p03da11-full-{ctest,detail}.log`).
- В полном прогоне исправленный fullscreen picking прошёл во всех трёх
  точках (first-fullscreen, fullscreen-reentry, fullscreen-resized); detail
  содержит selected=expected=5:1 после каждого click. Позже в том же executable
  упал **другой assertion** из NativePlayerInput:
  `Play without player retains RMB free-fly`. Он проверяет IsCameraControlActive
  после запуска Play без PlayerStart и native RMB/W input на протяжении 20 frames.
  Это не assertion выбора Entity. Во всех трёх независимых isolated runs он
  проходил. Причина этого отдельного native camera-capture сбоя пока не
  локализована; потеря focus или нестабильность desktop не заявляются доказанными.
  Его assertions и код не менялись. Повторный полный прогон не запускался.
- Native DX12 validation: во всех трёх успешных isolated runs прошли
  GetValidationErrorCount()==0 assertions. D3D12 ERROR/CORRUPTION markers
  в diagnostic/fixed/isolated/profile/full logs — **0**.
- `git diff --check` — **PASS**. Whitespace check нового отчёта — PASS.
- Финальная SHA256-проверка: из **824** исходных файлов изменены только
  LightGizmos.cpp и LuaGameplayControllerRegression.cpp. Все остальные файлы,
  включая незакоммиченный A1, сохранены. **90/90** защищённых assets/EventBus
  совпадают со снимком до этапа.

## Изменённые файлы и сохранность baseline

Изменения только этого этапа:

- `engine/editor/LightGizmos.cpp` — guard stale gizmo hover при пустом selection.
- `tests/LuaGameplayControllerRegression.cpp` — диагностика и дополнительные
  настоящие mouse-picking проверки после reentry/resize с bounded readiness.
- Этот отчёт.

Входные незакоммиченные правки A1 (CMakeLists.txt, SceneSerializer.cpp/.h,
новый SceneWorldModeRegression и отчёт A1) сохранены. Снимок содержимого файлов
до этапа: `out/p03da11-start-hashes.json`.
World DataMode, Collider semantics, Application World ownership, HierarchyGraph,
пользовательские Lua/scenes/assets и EventBus не изменялись.
Git commit/push/merge/rebase/reset/clean не выполнялись.

## Остаточные риски и вердикт

**Verdict: INCOMPLETE по общему baseline.** Первопричина исходного fullscreen
mesh picking исправлена и подтверждена контрольным запуском, тремя независимыми
native runs и успешным picking внутри полного CTest. Однако полный CTest
остаётся 60/61 из-за другого assertion RMB free-fly в NativePlayerInput.
Его причина не установлена и не объявляется автоматически внешним focus issue.
Общий зелёный baseline не заявляется. Исторические failures сохранены,
assertions не ослаблялись и не пропускались.

Диагностика текущего picking выполнена при DPI=1, без проверки разных физических
мониторов; native focus/readiness проверяется bounded assertions, а не skip.
Следующая работа для восстановления общего baseline должна локализовать именно
RMB capture failure, а не повторять полные прогоны до PASS.
Перехода к A2, Tree UI, Reparent, Gizmo features или Prefab нет.
