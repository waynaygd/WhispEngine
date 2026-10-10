# Render Interpolation для WhispEngine

10 октября 2026, Windows, MSVC x64 Release, DX12, Tracy 0.14.1. Ветка `job_system`; прежние локальные изменения, отчёты и тесты сохранены. Commit/push/merge/PR не выполнялись.

## Проблема и data flow

После внедрения Fixed Timestep PhysicsSystem обновляет ECS Transform с частотой 60 Hz. RenderSystem раньше копировал последнюю физическую позу каждый визуальный кадр. При 120/144 Hz несколько кадров показывали одинаковую позу, затем переходили к следующей, создавая ступенчатость.

TransformComponent хранит position, Euler rotation и scale. Parent/child transform hierarchy в существующей ECS нет. PhysicsSystem обновляет Transform после Integrate/Solver/Pose/Projection; все worker stages ожидаются до возврата. Application запускает ticks вне циклов renderer/windows. RenderSystem собирает CPU snapshots, готовит общие MVP packets и передаёт их IRenderAdapter. Камера остаётся frame update; её поза не интерполируется этим изменением.

Новый путь:

```text
BeginTick: сверка внешних правок, previous = current
    → World.UpdateFixedSystems(1/60), завершение задач и callbacks
    → EndTick: current = завершённая физическая поза
    → Editor / SetFrame: сверка edits и alpha
    → RenderGather: Resolve RenderPose
    → RenderPrepare: model/MVP matrix
    → общий IRenderAdapter / backend
```

FixedStepClock, параметры физики, Broadphase, solver iterations и JobSystem не изменены. При нескольких catch-up ticks история содержит последние два завершённых ticks, а не состояния начала/конца всего визуального кадра. При отсутствии tick сохраняется та же пара, меняется alpha. Избыточное время обрабатывает прежняя задокументированная политика максимум четырёх ticks.

## Архитектура и математика

Добавлен RenderInterpolation, принадлежащий Application; RenderSystem получает указатель на это хранилище. Все вызовы происходят на главном потоке вне работы physics workers. В хранилище нет ссылок/указателей на ECS-компоненты и нет GPU resources.

Record хранит Entity с generation, копию последнего physical Transform для обнаружения правок, previous/current RenderPose и отметку актуальности. Используется переиспользуемый vector по индексу ECS slot. Размер ограничен high-water World.GetCapacity; поколения предотвращают смешивание разных объектов в одном slot. Это O(ECS slot capacity), а не растущий кеш истории ticks/сцен. Clear очищает records, сохраняя capacity для перезапуска. Capture инвалидирует отсутствующие тела; stale records никогда не разрешаются для другой generation. Пиковая capacity остаётся до уничтожения Application, как у slot pool самого World.

История ведётся для dynamic Rigidbody с simulatePhysics=true, Collider и Transform, включая sleeping. Static и объекты без физики используют актуальный Transform напрямую. Число в панели — число подходящих физических тел, а не число уже загруженных/видимых мешей. GPU finalization и async resource lifetime остались в существующем RenderSystem.

Alpha = accumulator / (1/60), с защитным clamp в [0,1), в том числе после преобразования double в float. Nonfinite alpha даёт 0. Position и scale — линейная интерполяция. Rotation конвертируется из Euler в quaternion с порядком Rz*Ry*Rx, соответствующим прежним column-major model matrices. SLERP нормализует кватернионы, меняет знак второго при отрицательном dot, выбирая короткую дугу, ограничивает dot и использует нормализованный lerp для близких вращений. Противоположные представления q/-q не вызывают полного оборота; wrap через ±π не интерполируется через нулевые Euler angles.

RenderSnapshot теперь содержит RenderPose. Model matrix формируется непосредственно из quaternion/position/scale; обратного преобразования в Euler нет. Исходный Transform никогда не заменяется визуальным, даже временно. Математический тест независимо сравнивает quaternion matrix со старой формулой Rz*Ry*Rx с nonuniform scale.

Эта схема намеренно отображает состояние с задержкой до одного fixed tick (около 16.7 ms), без экстраполяции. Она сглаживает отображение между доступными состояниями, не ускоряя симуляцию и не устраняя тяжёлые физические кадры.

## Lifecycle и teleport

- Новый объект: previous=current при первом capture. Объект, существовавший до первого tick, интерполируется от своей spawn-позы к результату tick, без полёта из нуля.
- Delete, снятие Rigidbody/Collider и World.Clear: следующий capture инвалидирует историю. RenderSystem всё равно перечисляет живые ECS entities, поэтому history не может самостоятельно породить draw удалённого объекта.
- Generation reuse: Entity сравнивается целиком; новая сущность не получает предыдущую позу старой.
- Inspector, gizmo, undo и gameplay edits между ticks: сравнение physical Transform с source сбрасывает previous/current. SetFrame вызывается после editor перед render-подготовкой каждого окна. Resolve также проверяет source, если Transform изменён frame-системой уже после SetFrame, и возвращает фактическую новую позу.
- Teleport между ticks обнаруживается автоматически. Для teleport **внутри physics collision callback** вызывается `Application::NotifyTeleport(entity)` после записи нового Transform: без явного уведомления renderer не может отличить такой скачок от результата физики. Это API доступно gameplay коду. Пока не существовало отдельного engine teleport API; произвольные изменения внутри tick автоматически не классифицируются.
- Create/Clear/Restart, Play/Stop/Resume, update-mode и загрузка сцены используют существующий ResetPhysicsClock, который теперь очищает render history. При Stop SetFrame делает previous=current и Resolve возвращает physical pose независимо от остатка alpha. Вновь включённый Play начинает с текущей позы.
- ON/OFF не меняет clock или physics. История продолжает обновляться в OFF, позволяя безопасно включить ON. При переключении визуальная поза может измениться в пределах предыдущего/текущего tick: OFF показывает current, ON — задержанное состояние; это ожидаемый эффект сравнения, не изменение физики.

Смена cameras/aspect/windows не продвигает history. Общие render poses используются при повторных submissions. Pointer RenderSystem→history не владеет GPU ресурсами; штатный Shutdown очищает системы до освобождения Application. Деструктор RenderSystem не читает history.

## Debug colliders, бэкенды и ImGui

По умолчанию debug box/sphere использует тот же Resolve, что и mesh, с collider offset и collider dimensions. Сохранена существующая семантика world-space offset. В Physics Stress Test добавлен `Physics Debug Pose (colliders)`: он намеренно показывает истинную ECS-позу collider, поэтому отличие от интерполированного меша в этом режиме обозначено явно.

Добавлены `Render Interpolation` (по умолчанию ON), alpha и число подходящих dynamic bodies. Прежние 60 Hz/ticks, FPS/frame time, Create/Clear/Restart/Play/Stop/Serial/Parallel сохранены. Для CLI OFF используется `--no-interpolation`.

DX12 получает прежние MVP через общий IRenderAdapter, viewport и GPU resource path не менялись. Vulkan также собран с ENABLE_VULKAN=ON, но отдельный runtime Vulkan smoke не выполнялся. В текущем VkRenderAdapter нет overrides UploadMesh/DrawMesh для общей resource mesh сцены; это существующее ограничение, не исправлявшееся в задаче. Его test primitives получают matrix через SetTestTransform. Реальная работа нескольких OS окон не проверялась; проверены повторные submissions с двумя camera aspect через recording adapter. Интерполяция не дублируется в бэкендах.

## Regression и отсутствие влияния на физику

Release incremental build всех целей успешна (build-preference.log). Последний CTest — 3/3 passed: PhysicsRegression 10.00 s, ResourceRegression 0.37 s, RenderInterpolationRegression 4.84 s; всего 15.22 s. Новые тесты не заменяют старые.

RenderInterpolationRegression проверяет alpha 0/.25/.5/.75/.99999; position и scale lerp; quaternion вращение, равные/близкие и q/-q, переход ±π, независимое соответствие старой model matrix; stationary/sleeping, static fallback, новый/удалённый объект, reuse поколения, World.Clear, removed Rigidbody, Inspector-style edit, explicit teleport, Stop/resume/restart reset, irregular deltas, stall/catch-up/drop и диапазон alpha. Исходные position/rotation/scale проверяются после Resolve и повторных render submissions.

RecordingAdapter принимает данные от **настоящего RenderSystem** с ResourceManager и общими mesh/shader/texture keys. Проверены готовые submitted MVP, ON/OFF, совпадение debug collider/mesh, physical debug mode, удаление без последующих draws, две camera projections, create/clear для 100/250/500/1000. Это CPU regression общего render пути; он не проверяет GPU pixels. В texture path ожидаемо использован существующий fallback defaults/texture.

FixedTimestepRegression теперь выполняет BeginTick/EndTick/SetFrame/Resolve и переключает interpolation OFF/ON во время прежнего cadence-теста. При 30/60/120/144 Hz в Serial/Parallel все восемь комбинаций дали 240 ticks за четыре simulated seconds; position/rotation/velocity/angular velocity/sleeping совпали с допуском 1e-5. Полная последовательность пар Entity в collision events совпала (227972). OFF-базовая комбинация и ON/toggle комбинации сравниваются между собой. Проверены unchanged Transform после Resolve и finite render positions.

ResourceRegression прошёл cache reuse, async finalization, fallback и shutdown pending decode. Старые physics tests sleeping/waking/support removal, task lifetime, generations и stress lifecycle также прошли.

## Реальный DX12 smoke и Tracy

Выполнены десять запусков обычного движка, все exit 0:

- 100/500/1000 кубов, ON и OFF, по 300 frames с Tracy capture (шесть запусков).
- 250 кубов Serial, 300 frames.
- Lifecycle 744 frames: Create/Stop/Resume/Serial/Parallel/Restart/Stop/Clear/recreate/close; Stop phases имели ноль ticks, Cleared — ноль тел.
- Fixed StateMachine, 500 кубов, 300 frames, injected sleep 1000 ms на frame 50. Реальный frame 1010.245 ms, четыре ticks/16 internal substeps, dropped 0.933333333 s, следующий frame один tick без дополнительного dropped time. Physics clock не создаёт бесконечного catch-up.

Дополнительно выполнен smoke финальной сборки: 500 кубов ON, 120 frames, exit 0. Все семь Tracy captures, включая controlled-render regression, завершились capture exit 0. В обычном движке записаны RenderInterpolationSync/RenderPoseCapture, RenderInterpolationResolve, RenderGather/Prepare, PhysicsFixedTick и прежние physics/JobSystem zones. Код physics не был изменён. После captures добавлены проверки scale/двух camera submissions и сохранение предпочтения Physics Debug Pose при загрузке сцены; render interpolation math и измеряемые loops не менялись. Финальная сборка проверена CTest и дополнительным DX12 smoke.

**Не выполнялись:** ручная оценка плавности монитором/видеозаписью, клики всех Inspector/gizmo/undo сценариев через UI, фактическое открытие нескольких OS окон и отдельный Vulkan runtime. Inspector-style правки и общий render MVP путь проверены программно. Не выдаём эти проверки за human visual QA. Реальные запуски проверяют DX12 render path, ресурсные загрузки, viewport, profiler и штатный выход.

## Стоимость ON/OFF на одинаковых позах

Контролируемый test использует неизменные previous/current snapshots с translation и ненулевым rotation, alpha=.5, одинаковые resource/camera/counts. На каждом размере OFF/ON/OFF/ON, по 300 samples, итого 600 samples на режим. Physics и GPU не исполняются recording adapter; это **headless CPU render preparation**, не сравнение FPS игры. Ни один sample не меняет physical Transform.

Значения ниже — mean в мкс из Tracy, усреднение двух серий. Resolve — сумма per-object zones в RenderGather за sample, включая static floor fallback. Sync включает PoseCapture, его нельзя прибавлять второй раз.

| Кубы | Gather OFF / ON | Prepare OFF / ON | Resolve OFF / ON | Sync OFF / ON |
|---:|---:|---:|---:|---:|
| 100 | 207.29 / 205.54 | 6.16 / 6.03 | 3.69 / 5.72 | 3.14 / 3.00 |
| 250 | 495.48 / 497.00 | 14.86 / 14.66 | 7.93 / 12.40 | 6.83 / 6.61 |
| 500 | 861.44 / 852.18 | 30.10 / 29.97 | 14.78 / 24.10 | 14.03 / 13.80 |
| 1000 | 1696.24 / 1785.87 | 63.38 / 66.64 | 30.38 / 53.04 | 31.76 / 36.87 |

ON добавляет примерно 9.3 мкс в Resolve для 500 и 22.7 мкс для 1000. Разница Gather подвержена CPU/системному шуму; его уменьшение при 500 не является доказательством ускорения. На 1000 Gather mean вырос примерно на 0.090 ms (~5.3%). Sync/история ведутся и в OFF, чтобы переключение сохраняло корректные снимки. Дополнительные Begin/End captures вокруг каждого tick также стоят CPU: они не входят в Gather и учитываются отдельно в traces. Steady visual frames не увеличивали snapshot capacity на всех размерах. BufferGrowths отслеживает только vector storage; глобальные allocations не трассировались.

Для CPU времени всей controlled sample (Sync + отдельный Resolve probe + полный RenderSystem.Update), из сумм chrono mean:

| Кубы | OFF, мкс | ON, мкс |
|---:|---:|---:|
| 100 | 223.05 | 223.37 |
| 250 | 532.59 | 539.88 |
| 500 | 937.62 | 938.58 |
| 1000 | 1856.34 | 1978.76 |

Это стоимость диагностического цикла, не frame time с editor/GPU/physics.

## Реальные frame times: границы сравнения

300 frames/run, CPU wall frame из CSV; времена stages ниже mean мкс из Tracy. Реальные histories различаются из-за cadence/catch-up/dropped time: эти значения служат smoke/профилированием, **не доказательством эффекта ON на FPS**.

| Кубы | Режим | Frame median / p95 / max, ms | Gather mean, мкс | Prepare mean, мкс | Sync mean, мкс |
|---:|---|---:|---:|---:|---:|
| 100 | ON | 6.945 / 7.098 / 46.559 | 196.68 | 7.75 | 7.10 |
| 100 | OFF | 6.941 / 7.094 / 19.378 | 194.70 | 7.48 | 5.55 |
| 500 | ON | 7.025 / 81.960 / 101.775 | 990.78 | 37.01 | 25.70 |
| 500 | OFF | 7.874 / 77.908 / 99.892 | 1022.40 | 39.48 | 23.45 |
| 1000 | ON | 46.619 / 200.957 / 270.030 | 1926.02 | 78.47 | 48.48 |
| 1000 | OFF | 73.445 / 217.714 / 292.748 | 2206.35 | 85.26 | 56.14 |

PhysicsFixedTick mean в 1000 runs — 28.17 ms ON / 30.01 ms OFF. Тяжёлые физические кадры и замедление simulation при overload остаются. Интерполяция их не устраняет и параметры физики не маскирует. Sample count Gather=300; первые frames могут ещё не иметь всех готовых async render resources.

## Файлы и воспроизведение

Добавлены:

- engine/ecs/RenderInterpolation.h — math, RenderPose, value history и lifecycle.
- tests/RenderInterpolationRegression.cpp — проверки общего render path и controlled comparison.
- tests/RunRenderInterpolationDiagnostics.ps1 — воспроизводимые localhost Tracy/DX12 runs.
- tests/RENDER_INTERPOLATION_REPORT.md.

В этом проходе изменены Application.h/.cpp, RenderSystem.h/.cpp, EditorLayer.cpp, WhispEngine.cpp, CMakeLists.txt, tests/PhysicsRegression.cpp и tests/README.md. World/PhysicsSystem/FixedStepClock/JobSystem/ResourceManager/бэкенды не изменялись в этом проходе. Существующий dirty diff этих файлов относится к прошлым этапам. Сохранена смешанная исходная кодировка Application.cpp и кодировка RenderSystem.cpp, без массового перекодирования.

Команды в tests/README.md. Локальные артефакты: out/diagnostics/interpolation — build/CTest/runtime logs, CSV, controlled-render.tracy, visual-size-on/off.tracy, exports, controlled-summary.json и visual-summary.json. Git diff --check не обнаружил ошибок whitespace; предупреждения CRLF относятся к существующим файлам.

## Ограничения

Есть стандартная задержка интерполяции до одного tick. Teleport из callback требует явного NotifyTeleport. OFF сохраняет расходы ведения history. Storage/sweep зависят от peak ECS capacity, даже после удаления большей сцены. Global allocation instrumentation и GPU pixel assertions не выполнялись. Реальные тяжёлые кадры физики остаются; отдельно исправлять их в задаче render interpolation было бы изменением согласованного scope. Для Vulkan mesh scene и фактического multi-window runtime нужны отдельные проверки с учётом уже существующих возможностей бэкенда.
