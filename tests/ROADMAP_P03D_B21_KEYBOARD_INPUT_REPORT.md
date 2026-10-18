# P0.3d-B2.1 — Keyboard Controller Regression Diagnosis

Дата: 2026-10-18. Ветка `project_system`.
**Текущий baseline: Release PASS, профиль 16/16 PASS, full CTest 69/69 PASS.**
**Verdict: INCOMPLETE для root-cause diagnosis — исторический отказ не
воспроизведён, обоснованного исправления причины нет.**
P0.3d-C/D не начинались. Неподтверждённого production fix нет.

## Узкий аудит и исходная неопределённость

Прочитан ROADMAP_P03D_B2_HIERARCHY_REPARENT_REPORT и сохранённый отказ B2:
`out/p03db2-native-input-failure.log`, full 68/69, assertion
`keyboard alternative moves and turns without capture`.
В B2 не записаны отдельные три значения: нельзя задним числом установить,
отказали yaw, movement или cursor (либо несколько). RMB trace относится к
более ранней успешной фазе и не диагностирует keyboard failure.

Изучены только NativePlayerInput и соседние native scenario prerequisites,
KeyboardTurnController (read-only), LuaRuntime Sync/Call/Update/FixedUpdate,
InputManager snapshot/bindings, Application GameplayInputAllowed,
UpdateGameplayInput/GameplayCapabilities и Editor BlocksGameplayInput.
Повторного аудита fullscreen picking/RMB исправлений не проводилось.

Стандартный `--dx12`: Native(assets) → NativeEditorMouseInput →
NativeViewportDisplay → NativePlayerInput. Существующий `--native-player-input`
изолирует последнюю функцию, сохраняя внутри неё весь исходный порядок:
RMB → Player/FPS → fullscreen/display → Tab/recapture → focus loss/recovery →
text-edit release → script replacement → E/W Run(20). Keyboard phase отдельно
не вырезана: это потеряло бы существенные prerequisites отказа.

Смена scriptPath обрабатывается Sync в Lua Update/FixedUpdate: старый instance
retire/clear control, Load нового program при отсутствии, новый instance и
on_start. Это replacement пути, не необходимость принудительно вызывать
ReloadScript или ждать файловый watcher. Наблюдатель не вызывает Load/Fields,
не форсирует готовность и не исправляет input ownership.

InputManager получает реальные GLFW key states в BeginFrame. E/W в harness
остались исходными SendMessageW WM_KEYDOWN/WM_KEYUP со scan code. GameplayInputAllowed
зависит от running/focus, UI blocking и captured cursor/попадания native cursor
в viewport. Keyboard script bind'ит TurnRight=E и MoveForward=W в on_start,
затем читает frame actions в on_update, вращает camera и задаёт movement.
Ни один из этих путей поведения не изменён.

## Диагностика

Исходный общий assertion сохранён без изменений. Перед ним фиксируются отдельно:
baseline/current/delta/step yaw, desiredDirection XYZ и XZ length, три booleans,
cursor mode, GLFW focus/AppFocusLost, native и physical E/W, frame actions,
ImGui capture/text/active ID, UI blocker/current input owner, Play/Pause,
Player/camera-target validity, cursor capture, mouse/viewport bounds,
requested/actual script path, loaded/present/matches/failed/revision,
on_start/on_update/on_fixed_update counts, Lua input/camera/cursor ownership,
ошибка script и совпадение ранее полученных component pointers с текущими.

Отдельный stack-owned bounded buffer: 26 строк по 3072 bytes. Четыре pre-state:
before-text-edit, after-text-edit, after-script-warmup, sent-keys. Затем ровно
20 samples исходного непрерывного Run(20) и result (всего 25). Hook NewFramePre
срабатывает после Lua/camera update и до сброса предыдущего UI ownership state.
Никакого Run(1) вместо Run(20), per-frame streams/files/console, sleeps или
дополнительных input events. Полный trace печатается при failure. При успехе
печатаются только компактные warmup/result records после проверяемой фазы.
Completeness assertion проверяет 20/25 и отсутствие overflow/truncation.

Отмечаются абсолютные первые sample frames с отсутствием yaw progress,
movement length <= .9 или cursor != NORMAL; per-frame records содержат
фактический blocker и Lua activity. Эти признаки — наблюдения, не автоматически
доказанная причина (например, отсутствие callback нужно сопоставлять с dt/input).

LuaRuntime добавляет только read-only InspectInstance и три счётчика фактически
вызванных callbacks в Instance. Счётчик увеличивается после проверки существования
function перед её protected call, failed/error остаются существующими runtime
значениями. Нет новых callbacks, inputs, loads, waiting/retry или frame phases.
Diagnostics возвращает borrowed string_views: они немедленно форматируются в
буфер до следующего runtime mutation; ссылки между кадрами не сохраняются.

## Первые наблюдения

| Запуск | Targeted NativePlayerInput | Standard --dx12 |
| --- | --- | --- |
| Результат | 90 checks PASS | 227 checks PASS, 13.42 s |
| Keyboard warmup/result frame | 170 / 190 | 868 / 888 |
| Previous yaw | 1.39750004 | 1.39750004 |
| Current yaw | 1.64777565 | 1.64752066 |
| Delta yaw | 0.250275612 | 0.250020623 |
| desiredDirection XYZ | .997038543, 0, -.0769033208 | .997058153, 0, -.0766490847 |
| XZ length / conditions yaw,move,cursor | 1 / 1,1,1 | 1 / 1,1,1 |
| Cursor | 212993 (GLFW_CURSOR_NORMAL) | то же |
| GLFW focus / AppFocusLost | 1 / 0 | 1 / 0 |
| Native E/W, frame E/W на result | 1/1, 1/1 | то же |
| Physical E/W | 0/0 (исходный SendMessage harness) | то же |
| ImGui keyboard capture/text/block/active | 0/0/0/0 | то же |
| Input owner / Play/Pause | 6 (Runtime) / 1,0 | то же |
| Player/camera target/component pointers | valid / same | то же |
| Player cursor captured / editor camera active | 0 / 0 | то же |
| Native cursor / viewport | 370,289 / 12,210,729,368; inside=1 | то же |
| Requested and actual instance | KeyboardTurnController.lua, loaded/present/matches=1 | то же |
| Lua revision/start/update | 1 / 1 / 4→24 | то же |
| Lua failed/error | 0 / empty | то же |
| Lua input/camera/cursor | 1/1/0 | то же |

До отправки E/W оба native/frame key states равны 0. После warmup FPS instance
уже заменён Keyboard instance, on_start=1, updates=4, capture released. Признаков
stuck E/W, active text, stale instance, невыполненной замены, утраты focus или
invalid handles в этих двух запусках нет. Разный frame offset соответствует
предшествующим native scenarios; dt-зависимая величина yaw немного отличается.

Источники: `out/p03db21-targeted.log`,
`out/p03db21-standard-{ctest,detail}.log`.
В observed runs конечного failing condition нет. Исторический первый failing
keyboard frame B2 недоступен: тогда observer не существовал. Два успешных
запуска не доказывают устранение intermittent issue и не дают root cause.

## Изменённые файлы

- `tests/LuaGameplayControllerRegression.cpp`: отдельный bounded keyboard trace,
  read-only ApplicationTestAccess, completeness check и post-phase summaries.
- `engine/scripting/LuaRuntime.h`, `LuaRuntime.cpp`: read-only instance diagnostics
  и callback counts. Input/controller/camera semantics не менялись.
- Этот новый отчёт.

HierarchyEdit, Reparent/Unparent, Undo/Redo, ImGui Tree, SceneSerializer,
Application ownership, Collider semantics, пользовательские Lua/scenes/assets
и EventBus не редактировались. Входные SHA-256/status:
`out/p03db21-start-hashes.json`, `out/p03db21-start-status.txt` (842 файла).
Все предыдущие незакоммиченные изменения сохраняются.

## Обязательные проверки до full

- Диагностическая сборка Controller target: PASS,
  `out/p03db21-diagnostic-build.log`.
- Release всех targets: PASS, `out/p03db21-release-build.log`.
- Targeted: один `--native-player-input`, 90 checks PASS,
  `out/p03db21-targeted.log`.
- Standard native: один CTest `LuaGameplayControllerDx12Regression`,
  227 checks PASS, 13.42 s (CTest total 13.43 s),
  `out/p03db21-standard-{ctest,detail}.log`.
- Профиль Controller + Editor/Hierarchy B2: **16/16 PASS, 49.87 s**,
  `out/p03db21-profile-{ctest,detail}.log`. Включает Controller CPU/DX12,
  Reparent 176/294, Tree 74/308, Activation 104/71, Ownership 101/20,
  MultiSelection 2550/395, Editor2 и Workflow CPU/DX12. Native Controller
  в профиле также прошёл 227 checks.

Эти проверки выполнены по требуемому порядку, не в качестве retries до PASS.
Ни одного failure на диагностической версии до full не было. После Release
исходники не менялись. Полный CTest запускается ровно один раз.

## Единственный полный CTest

**69/69 PASS, 258.57 s, exit 0**, без повторов/skip/assertion weakening.
Источники: `out/p03db21-full-ctest.log`, `out/p03db21-full-detail.log`.
Native Controller — **227 checks PASS, 13.70 s**; CPU Controller — 62 PASS.
B2 Reparent — **176 CPU / 294 DX12 PASS**, Tree — 74/308 PASS; остальные
Hierarchy/Ownership/Project/Editor/Physics/Lua/render regressions также PASS.

Keyboard result в full:

- Frame 888, baseline yaw **1.39750004**, current **1.6474632**,
  delta **0.249963164 > 0**.
- DesiredDirection **(.997062564, 0, -.0765917897)**,
  XZ length **1 > .9**.
- Cursor **212993 (NORMAL)**. Conditions **1,1,1**: ни одно не отказало.
- Focus **1**, AppFocusLost **0**, native E/W и frame E/W **1/1**;
  physical E/W **0/0** (неизменённый native-message harness).
- Keyboard capture/text/UI block/active ID **0/0/0/0**, input owner **6 Runtime**,
  Play/Pause **1/0**, player и camera target alive, pointers same.
- Native cursor **370,289**, viewport **12,210,729,368**, inside=1;
  native RMB/LMB **0/0**, player capture **0**, editor camera **0**.
- Requested/instance paths совпадают: **scripts/KeyboardTurnController.lua**,
  loaded/present/matches **1/1/1**, failed **0**, error empty, revision **1**.
- on_start **1**, on_update **4→24**, luaInput/luaCamera/luaCursor **1/1/0**.
  `on_fixed_update` у этого script отсутствует, поэтому actual callback count
  fixed=0 ожидаем и не означает отсутствие fixed simulation.

Warmup/result frames стандартного отдельного и full запусков совпали (868/888),
targeted использовал 170/190; все сохранили исходные 20 кадров. Зафиксированных
конечных failing conditions в B2.1 нет. Полные success buffers намеренно не
печатаются: при failure будут доступны первые divergence frames и все samples.
Исторический первый failing frame B2 и конкретный failed member conjunction
по-прежнему **неизвестны**, поскольку в старом запуске их не записывали.

## DX12 и целостность

**DX12 validation errors: 0**: native assertions завершились успешно, поиск
D3D12 ERROR/CORRUPTION/severity и `DX12 validation failed at shutdown` в
targeted/standard/profile/full logs дал 0 markers.
Источник: `out/p03db21-validation-check.log` и соответствующие detail logs.

**git diff --check: PASS**. Начальные SHA-256 842 файлов: удалений нет,
изменены только три заявленных исходника, остальные исходные dirty-файлы
совпадают. Все 90 assets/EventBus сохранены, B2 implementation files сохранены.
Исходный общий assertion проверен на точное присутствие без ослабления.
Источники: `out/p03db21-final-integrity.log`, `out/p03db21-final-status.txt`.
Application.cpp не редактировался/не перекодировался; CRLF warning относится
к прежнему baseline. Commit/push/reset/clean/merge/rebase не выполнялись.

## Решение и риски

**INCOMPLETE по критерию root cause пользователя; текущий проверенный baseline
при этом зелёный (69/69).** Ни одно из трёх условий не отказало в новых запусках.
Утверждать, что причиной старого failure были focus, input ownership или Lua
replacement, оснований нет. Неподтверждённый production/harness fix не вносился.
Выполнено законченное диагностическое изменение, готовое сохранить данные при
следующем отказе; выполнять новые full runs ради воспроизведения/PASS не стали.

Остаточные риски:

- Historical intermittent failure остаётся необъяснённым. Targeted/standard/
  profile/full успешны, но это не доказательство устранения причины.
- Наблюдатель ограничен памятью и не делает per-frame I/O, однако любое
  инструментирование слегка меняет timing. Число кадров/events и activation
  conditions сохранены; отсутствие влияния на intermittent timing не обещается.
- Counter/view diagnostics — owner-thread read-only данные; string_views нельзя
  удерживать через runtime mutation. Они не сериализуются и не меняют Lua API.
- Длительный ручной native focus/input soak не проводился. При новом failure
  сохранить полный keyboard buffer и проверить первый расходящийся кадр, не
  делать вывод только по последнему состоянию или RMB trace.

Переход к C/D не выполнялся. B2 graph-edit реализация не изменена, assertions
не отключены, пользовательские файлы сохранены.
