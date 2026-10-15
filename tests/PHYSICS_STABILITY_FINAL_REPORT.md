# WhispEngine: финальная диагностика Job System и WhispPhysics

Проверка 9–10 октября 2026, Windows, MSVC x64 Release, Intel i5-13500H,
enkiTS v1.12, 16 task threads включая главный, Tracy 0.14.1.
Рабочая ветка `job_system`; прежние локальные изменения сохранены.
Commit, push, PR и merge не выполнялись. Этот отчёт дополняет, а не заменяет
`PHYSICS_STRESS_REPORT.md`.

## Результат диагностики

Полноценные CPU-профили **обычного DX12 WhispEngine** получены. В новых
запусках аварийное завершение `-1073740791` (`0xc0000409`) не воспроизведено.
Это не доказательство исправления старой аварии. Старый Parallel p99 144,422 мс
также не воспроизведён в трёх первичных и одном итоговом запуске прежнего
headless-бенчмарка.

При этом длинные кадры в настоящем движке остаются. Подтверждены расходы
последовательного Broadphase, Solver/Projection и усиление нагрузки при росте
адаптивных substeps. Нельзя объявить устранёнными все просадки или деградацию:
во время одного длительного теста времена существенно изменились при неизменном
числе тел и контактов. Причина изменения базовой скорости исполнения не установлена.

## Проверенная архитектура и lifetime

- JobSystem имеет постоянный scheduler. `m_InFlight` удерживает TaskSet до
  завершения даже при отброшенном внешнем handle. Перед удалением scheduler
  выполняется `WaitforAllAndShutdown`; список задач очищается после ожидания.
  В изученном enkiTS текущие TaskSet не имеют dependencies, и completion публикуется
  после выполнения диапазонов. Новая ошибка lifetime здесь не подтверждена.
- Dispatch/Execute/Retain используются владельцем scheduler, главным потоком.
  Это не API для конкурентной отправки из произвольных внешних потоков.
  Decode jobs ресурсов не меняют ECS; зависимые загрузки запускаются при
  main-thread finalization. Главный поток может выполнять jobs внутри Wait.
- Narrowphase читает тела и пишет различные слоты пары; Integrate/Pose пишут
  разные тела. Eager basis/inertia cache уже подготовлен до jobs. Wait отделяет
  стадии, Solver и Projection последовательны. Переключение режима и изменение
  World выполняются после завершения Update. Новая гонка статическим аудитом
  не обнаружена; динамический race detector не запускался.
- BodyRef не хранится между Update; callbacks отложены до конца substeps и
  проверяют Entity generation/alive. World::Clear сохраняет поколения слотов.
  Create/Restart/Clear сбрасывают контактный cache. Проверки устаревших handles
  и отсутствия накопления объектов сохранены.
- Shutdown: освобождение GPU handles при живом renderer → закрытие UI/backend
  и окон → ResourceManager::Shutdown (drain jobs и CPU finalizers) → JobSystem
  shutdown → удаление ECS systems/компонентов. RenderSystem освобождает свои
  resource maps до уничтожения renderer; обычный кадр сбрасывает render adapter
  в nullptr. Подписка Collision создаётся один раз, Play/Restart её не повторяют.
- ResourceRegression проверяет shutdown при pending decode. Нельзя из успешно
  пройденных сценариев делать вывод об отсутствии всех UAF/heap corruption:
  ASan, Application Verifier и исключение под отладчиком в этой проверке не получены.

## Подтверждённые узкие места

1. **Активные контакты:** в lifecycle capture при Resumed средняя физика
   38,503 мс, из неё Solver 20,444 и Projection 10,611 мс. Максимум 500
   manifolds/2000 points; до 12 substeps. Parallel ускоряет только часть pipeline.
2. **Broadphase после sleeping:** даже когда все 500 кубов спят, оба прохода
   spatial hash, контакты, подготовка solver, island/support/cache work сохраняются.
   Самый длинный кадр длинного capture: CSV frame 2595, кадр 108,442 мс,
   физика 93,712 мс, 501 тело, 500 sleeping, 500 пар/контактов, 2000 points,
   12 substeps. Tracy независимо подтверждает PhysicsSystem 93,716 мс,
   Broadphase 55,950, Solver 12,016, Projection 0,243, Dispatch 2,826,
   Wait 0,254 мс. EditorLayer 0,839, RenderGather 2,210, RenderPrepare 0,125,
   RenderSubmit 2,742, DX12Present 0,707 мс. Главный bottleneck этого кадра —
   CPU-физика; это не длительное ожидание worker или GPU fence.
3. **Обратная связь dt:** World/Physics обновляются с временем кадра; физика
   ограничивает dt величиной 0,05 с и шаг максимумом 1/240 с. Поэтому более
   медленный кадр требует больше substeps, при 50 мс уже 12, и повторяет
   дорогие последовательные проходы. Это происходит и при sleeping.
   Fixed update режима StateMachine не делает PhysicsSystem fixed timestep.
   Политика timestep и число итераций в этой работе не изменялись.
4. **Выделения памяти остаются:** BodyRef/scratch создаются на каждый Update;
   `large`, `seen`, `pairs`, island vectors и `nextCache` — на проход/substep.
   Предыдущий reuse действует внутри Update. Это подтверждённые места allocation
   в исходниках, но доля allocator в конкретном пике не измерена: Tracy memory
   hooks/allocator stacks не добавлялись. Нельзя приписать весь Broadphase allocator.
5. Renderer использует общий mesh/shader/texture, но отдельный draw на объект.
   Outer `Render` zone **включает WorldSystems/Physics**, поэтому её время нельзя
   выдавать за чистую GPU/render стоимость. CPU GPU-fence waits измерены отдельно;
   GPU timestamps и контекст переключений ОС в capture отсутствуют.

## p95/p99: повтор прежнего headless-бенчмарка

Та же сцена: 576 dynamic boxes + ground, 5 повторов × 10 warmup + 30 измеряемых
кадров, 150 samples/режим на запуск. Три независимых запуска, без одновременно
работающего визуального движка. Percentile: nearest rank, `ceil(q*N)-1`, как в
исходном benchmark. Здесь время кадра — wall time физического Update, без DX12/UI.

| Запуск | Режим | Median, мс | p95 | p99 | Max |
|---|---|---:|---:|---:|---:|
| 1 | Serial | 15,199 | 16,964 | 19,355 | 19,579 |
| 1 | Parallel | 15,140 | 17,046 | 18,291 | 19,196 |
| 2 | Serial | 15,365 | 17,235 | 22,555 | 26,464 |
| 2 | Parallel | 15,216 | 17,016 | 18,534 | 22,140 |
| 3 | Serial | 15,250 | 16,818 | 18,929 | 20,291 |
| 3 | Parallel | 15,073 | 16,059 | 17,834 | 20,975 |

Все samples: 4 substeps, 576 pairs/manifolds, 2304 points, 12 velocity iterations.
По 450 samples/режим: Serial median/p95/p99/max = 15,308/16,964/19,579/26,464;
Parallel = 15,149/16,491/18,291/22,140 мс. Median speedup около 1,01×.

Средние времена стадий по этим 450 samples (мс/Update):

| Режим | Integrate | Broadphase | Narrowphase | Solver | Pose | Projection | Dispatch | Wait |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Serial | 0,066 | 0,737 | 0,849 | 8,771 | 0,017 | 4,640 | 0 | 0 |
| Parallel | 0,120 | 0,765 | 0,556 | 8,705 | 0,112 | 4,612 | 0,318 | 0,407 |

Максимальный суммарный Wait на Update 0,806 мс, Dispatch 1,162 мс. В самом
медленном Parallel sample (22,140 мс) Broadphase 2,126, Solver 10,138,
Projection 7,080, Wait 0,806 мс. Дополнительных substeps/контактов там нет.

Старые показатели после оптимизации: Serial median/p99 25,834/33,790,
Parallel 25,168/144,422 мс. Новая медиана меньше, но в этом проходе алгоритмы
не оптимизировались: нельзя приписывать разницу диагностическим зонам или
объявлять старый spike исправленным. Desktop, CPU частоты/температура, affinity
и фоновые приложения не изолированы; влияние ОС остаётся гипотезой.

## Длительная headless-симуляция 500 кубов

7200 кадров на режим, fixed dt=1/60, 120 simulated seconds на режим.
Порядок Serial/Parallel чередуется каждый кадр. Проверены равенство position,
velocity, rotation, angularVelocity, конечность координат, нахождение над полом,
Stop/Play и очистка World. Запуск завершился с кодом 0.

| Режим | Median, мс | p95 | p99 | Max |
|---|---:|---:|---:|---:|
| Serial | 4,614 | 11,872 | 18,381 | 33,696 |
| Parallel | 4,499 | 11,382 | 18,150 | 34,163 |

501 тело, максимум 500 contacts/2000 points; 4 substeps. После укладки 500
sleeping. Parallel окна 20–80 simulated seconds: median 3,945–4,233 мс;
90–120 seconds: 9,200–9,404 мс. Serial одновременно изменился примерно
4,05–4,34 → 9,20–9,68 мс. Накопления сущностей/контактов и роста substeps нет,
но **рост времени реально измерен**; причина изменения скорости исполнения
не подтверждена. Ни «утечки физики», ни «только ОС» эти данные не доказывают.

Средние стадии мс/Update:

| Режим | Integrate | Broadphase | Narrowphase | Solver | Pose | Projection | Dispatch | Wait |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Serial | 0,006 | 3,155 | 1,122 | 1,623 | 0,009 | 0,140 | 0 | 0 |
| Parallel | 0,086 | 3,266 | 0,633 | 1,635 | 0,088 | 0,140 | 0,444 | 0,287 |

Самый медленный Parallel Update: 34,163 мс, Broadphase 24,993 мс,
Wait 0,140 мс, прежние 4 substeps/500 sleeping. Старый длительный тест имел
median 10,444/9,840 и p99 32,904/33,058; изменчивость между запусками велика.

## Настоящий визуальный DX12 запуск

500 кубов + видимый пол, shared cube mesh, shader/default texture, штатный
editor. Обычный старт без CLI остаётся без стресс-сцены. В конце каждого
5000-frame запуска все 500 кубов sleeping, 501 физическое тело, 500 contacts.
Физика использует variable dt, минимум 3 substeps из app.json, 12 iterations.
Ниже время **всего кадра**, включая editor/render/present; отдельная таблица —
PhysicsSystem. Первоначальное падение и startup входят в выборку.

| Запуск (5000 кадров) | Frame median, мс | p95 | p99 | Max |
|---|---:|---:|---:|---:|
| Serial, CSV | 20,991 | 49,842 | 64,838 | 235,956 |
| Parallel, CSV | 8,614 | 16,304 | 48,490 | 77,441 |
| Parallel + Tracy | 17,626 | 31,709 | 53,383 | 108,442 |

| Запуск | Physics median, мс | p95 | p99 | Max |
|---|---:|---:|---:|---:|
| Serial, CSV | 14,310 | 40,566 | 55,088 | 132,406 |
| Parallel, CSV | 3,557 | 8,893 | 43,984 | 73,337 |
| Parallel + Tracy | 11,239 | 21,416 | 48,358 | 93,712 |

Последние 1000 кадров: physics median/p99 Serial 14,339/41,555;
Parallel 3,609/10,605; Parallel+Tracy 13,204/37,392 мс. Substeps в этих
запусках меняются от 3 до 12, включая sleeping. Это разные wall-time histories
и условия desktop/профилирования; **отношение этих медиан не является speedup**.
Для одинаковой истории корректнее headless сравнение выше. В Serial max-frame
235,956 мс физика заняла 110,834 мс; оставшиеся расходы этого CSV-only кадра
не имеют Tracy breakdown и не приписываются конкретной подсистеме.

Средние стадии мс/Update:

| Запуск | Integrate | Broadphase | Narrowphase | Solver | Pose | Projection | Dispatch | Wait |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Serial | 0,017 | 8,059 | 2,344 | 3,608 | 0,029 | 0,598 | 0 | 0 |
| Parallel | 0,060 | 2,113 | 0,472 | 1,421 | 0,062 | 0,229 | 0,197 | 0,321 |
| Parallel+Tracy | 0,147 | 6,057 | 0,966 | 2,769 | 0,162 | 0,315 | 0,873 | 0,271 |

Lifecycle capture (переключение режима происходит по ходу одной симуляции,
не является сравнением одинакового состояния):

| Фаза | Кадров | Frame median/p99/max, мс | Physics median/p99/max, мс |
|---|---:|---|---|
| InitialDrop | 120 | 6,945 / 19,986 / 59,956 | 1,485 / 7,004 / 9,274 |
| Resumed, активные столкновения | 108 | 51,768 / 60,955 / 60,999 | 48,427 / 56,692 / 56,862 |
| Serial, укладка | 120 | 8,128 / 32,449 / 33,300 | 3,876 / 26,018 / 28,823 |
| Parallel, почти покой | 120 | 6,936 / 8,375 / 8,424 | 3,472 / 4,035 / 4,064 |
| Restart, новое падение | 120 | 6,943 / 10,326 / 14,907 | 1,468 / 6,991 / 11,132 |

## Tracy: полученные артефакты

- `out/diagnostics/engine-lifecycle.tracy`: 525827 bytes, 747 Frame marks,
  10,86 с, 81562 CPU zones; renderer/editor, падение, столкновения,
  Serial/Parallel, Restart/Clear/recreate и shutdown.
- `out/diagnostics/engine-settled.tracy`: 4995994 bytes, 5003 Frame marks,
  92,4 с, 806487 CPU zones; падение → укладка → длительный sleeping.
- CLI capture и оба движка вернули 0. Оба файла прочитаны matching 0.14.1
  `tracy-csvexport`, экспортированы aggregate и individual CPU events.
- Проверены зоны PhysicsSystem, BroadphaseSerial, NarrowphaseParallel,
  IntegrateParallel, PoseParallel, SolverSerial, PositionProjectionSerial,
  EditorLayer, RenderGather/Prepare/Submit, Dispatch/Wait и DX12 fence/present.
  TRACY_ENABLE/TRACY_ON_DEMAND включены на Engine, а не только тестах.
- Physics jobs присутствуют на главном и всех 15 worker threads.
  В коротком capture worker job scopes суммарно 17,9–36,6 мс/worker,
  самый длинный worker scope 0,253 мс; main scope до 0,470 мс.
  Main также выполняет serial fallback; его суммарную долю нельзя считать
  показателем только параллельной балансировки. Зависание worker не наблюдалось.
- GUI профайлера не нужен для записи: `tests/CapturePhysicsTracy.ps1` использует
  официальный Windows CLI Tracy 0.14.1. Файлы остаются локальными под `out/`.

Dispatch/Wait уже включены в parallel stage times: их нельзя повторно прибавлять
к стадиям. Broadphase включает оба прохода; Projection второй Broadphase не
включает. Bodies/sleeping измеряются в начале Update; пары/контакты/points —
пиковые количества по substeps. Остаток total включает gather, islands, sleep,
cache и callbacks. CPU fence wall time не является GPU timestamp.

## Shutdown и повторяемые циклы

**17 новых запусков обычного движка, все exit=0:**

- 8 полных lifecycle запусков: один с Tracy, шесть по скрипту и один после
  чистой итоговой сборки. Каждый делает
  Create/Play → Stop → Play → Serial → Parallel → Restart → Stop → Clear →
  Create/Play → GLFW close request. Проверки: 501 stress entity после трёх
  созданий, ноль после Clear. Stop оставляет тела и обнуляет physics time.
- 6 коротких активных запусков: Serial/Parallel × 5/30/120 кадров, затем штатный
  выход из Run и полный Shutdown; включая незавершённые resource loads.
- 3 запуска по 5000 кадров: Serial, Parallel, Parallel с Tracy.

Сводка 14 запусков скрипта: `out/diagnostics/engine-exits.csv`; два capture
запуска подтверждены возвращёнными exit codes и логами capture, последний
запуск — `lifecycle-final.log`/CSV и exit=0. GLFW close происходит между
обновлениями, когда jobs предыдущего шага уже завершены. Принудительное убийство
процесса/закрытие изнутри worker не тестировалось и не является штатным путём.
Клики ImGui не автоматизировались: сценарий вызывает те же Application API;
это проверка этих операций и настоящего renderer, не UI-click coverage.

Старый crash не воспроизведён: нового exception/stack trace нет. Его причина
и исправление не установлены. Исходный числовой код сам по себе не определяет
конкретный ошибочный участок; при повторении нужен dump с symbols.

## Изменения этого диагностического прохода

Новые функциональные исправления shutdown/физических алгоритмов не вносились:
подтверждённой новой ошибки, объясняющей crash или старый p99, не найдено.
Сохранены все предыдущие исправления, сцена и компактная панель.

| Файлы | Добавлено |
|---|---|
| engine/jobs/JobSystem.cpp | Tracy JobDispatch/JobWait |
| engine/ecs/systems/PhysicsSystem.h/.cpp | Суммарные Dispatch/Wait timers без изменения waits/диапазонов |
| engine/core/PhysicsDiagnosticSample.h | Общий CSV writer, buffered samples после симуляции |
| engine/core/Application.h/.cpp | opt-in CSV, ожидание Tracy, lifecycle сценарий/count checks |
| WhispEngine.cpp | --diagnostics, --stability-scenario, --wait-tracy |
| engine/render/backends/dx12/Dx12RenderAdapter.cpp | CPU zones begin/present/frame fence/shutdown fence; guard TRACY_ENABLE |
| tests/PhysicsRegression.cpp | Max, opt-in individual samples, stress rotation/angularVelocity equivalence |
| tests/RunPhysicsStability.ps1 | Повторные настоящие DX12 запуски и exit summary |
| tests/CapturePhysicsTracy.ps1 | Воспроизводимая локальная запись обычного движка |
| tests/README.md | Команды и границы интерпретации новых измерений |
| tests/PHYSICS_STABILITY_FINAL_REPORT.md | Этот отчёт |

CMakeLists, World, Time, RenderSystem, EditorLayer, PhysicsStressScene и
ResourceRegression сохраняют изменения предыдущей работы. Они не откатывались
и не переписывались заново; полный список прежних изменений — в старом отчёте.

## Итоговая сборка и повторная проверка

Чистая Release-сборка выполнена (`cmake --build out/build/x64-release
--clean-first --parallel 8`). Первый sandbox запуск остановился на Assimp с
MSVC C1902 при создании PDB; продолжение вне sandbox успешно скомпилировало
оставшиеся targets и вернуло 0. Настройки сборки/физики для обхода ошибки не
менялись. Логи: `release-clean-build.log`, `release-clean-build-retry.log`.

CTest итоговой сборки: PhysicsRegression 2,45 с и ResourceRegression 0,34 с,
2/2 passed, exit=0 (`final-ctest.log`). Сохранены прежние regression-сценарии,
включая stress lifecycle, stale handles, dropped jobs, scheduler reinitialization,
sleeping/support removal и serial/parallel equivalence. Async resource cache,
decode/finalization, missing fallback, ClearAll/reload и pending shutdown прошли.

Дополнительный итоговый legacy benchmark, 150 samples/режим, exit=0:

| Режим | Median, мс | p95 | p99 | Max |
|---|---:|---:|---:|---:|
| Serial | 15,205 | 18,421 | 20,766 | 21,109 |
| Parallel | 15,148 | 20,425 | 22,188 | 23,839 |

Итого четыре запуска прежнего benchmark; старый Parallel 144 мс не воспроизведён.

Итоговый `--stress`, 600 frames/режим для каждой размерности, exit=0:

| Кубов | Serial median/p95/p99/max, мс | Parallel median/p95/p99/max, мс | Sleeping в конце |
|---:|---|---|---:|
| 100 | 0,649 / 2,780 / 3,238 / 9,383 | 0,649 / 2,773 / 3,257 / 6,224 | 100 |
| 250 | 1,830 / 8,193 / 8,866 / 10,169 | 1,831 / 8,338 / 8,934 / 9,422 | 250 |
| 500 | 4,506 / 23,330 / 30,747 / 47,296 | 4,452 / 23,780 / 29,988 / 35,115 | 500 |
| 1000 | 35,860 / 83,317 / 94,267 / 123,909 | 35,292 / 80,335 / 92,032 / 106,609 | 890 |

Все размеры: 4 substeps, peak contacts соответствует числу кубов, 12 solver
iterations. На 100/250 выбранный Parallel использует существующий serial fallback
малых диапазонов; это не измерение масштабирования на workers. Проверены конечные
position/velocity/rotation/angularVelocity, finite state, пол, по 10 Stop/Play
циклов и очистка. `StressLifecycle` дополнительно повторяет Create/Restart/Clear
по 10 раз на каждую размерность, сохраняя постороннюю entity. На 1000 средние
Parallel Solver/Projection 17,006/9,113 мс: тяжёлая активная укладка сохраняется.

Сырые CSV/логи и summary JSON находятся в `out/diagnostics` относительно корня
репозитория. Первые capture CSV имеют схему без последнего поля solverIterations;
итоговая версия writer добавляет его (12 при Play, 0 при Stop).

Второй длительный `--stress-long` на итоговой сборке также прошёл, exit=0:
7200 frames/режим, те же проверки состояний, паузы и очистки.

| Режим | Median, мс | p95 | p99 | Max |
|---|---:|---:|---:|---:|
| Serial | 9,509 | 13,709 | 30,937 | 43,325 |
| Parallel | 9,247 | 13,030 | 30,862 | 48,238 |

Средние стадии мс/Update:

| Режим | Integrate | Broadphase | Narrowphase | Solver | Pose | Projection | Dispatch | Wait |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Serial | 0,009 | 4,787 | 1,428 | 2,098 | 0,014 | 0,201 | 0 | 0 |
| Parallel | 0,118 | 4,907 | 0,752 | 2,107 | 0,122 | 0,201 | 0,701 | 0,195 |

500 sleeping после укладки, 501 тело, 500 contacts/2000 points, 4 substeps,
12 iterations. В этом повторении время **снизилось** без пересоздания сцены:
Parallel median окна 90 с 9,115 → 100 с 4,635 → 110 с 4,587 → 120 с 6,481 мс;
Serial 9,113 → 4,728 → 4,563 → 6,237 мс. Непрерывная неизбежная деградация
от времени жизни сцены этим повтором не подтверждена, но стабильная скорость
исполнения также не обеспечена. Нельзя доказать конкретную причину ОС/CPU без
соответствующей телеметрии.

Здесь был отдельный Parallel выброс 48,238 мс (frame 1772): Broadphase 26,038,
Integrate 8,025, Dispatch 10,531, Wait 0,273 мс при прежних counters. Значит,
Dispatch тоже способен давать выбросы, хотя средняя стоимость невелика; его
wall time включает выделение TaskSet, pruning и scheduler enqueue, а также
возможное вытеснение ОС. Без профиля этого headless кадра причину его 10,531 мс
не разделить. Это не старый p99 144 мс и не подтверждение ошибки enkiTS.

Последний реальный lifecycle после чистой сборки прошёл с exit=0: 744 measured
frames, все девять фаз присутствуют, 501 тело после Create/Restart/recreate,
ноль тел во всех 12 кадрах Cleared; затем полный штатный shutdown.

## Ограничения и дальнейшая работа

- Старый crash и Parallel 144 мс не воспроизведены; считать их исправленными нельзя.
- Новые длинные кадры воспроизведены, включая после sleeping. Broadphase и dt
  feedback измерены; scheduler imbalance/allocator/OS как первопричина старого
  единичного spike не подтверждены. Тесты не обеспечивают стабильный FPS.
- Нужен отдельный контролируемый проход с фиксированной CPU нагрузкой/частотами,
  OS context switches и allocator instrumentation. Сравнивать состояния при одном
  dt и одинаковых контактах, а не FPS разных по времени визуальных запусков.
- Следующий отдельный план: переиспользование Broadphase/cache storage между
  Update без сохранения ECS pointers; затем обоснованная политика dt/budget и
  обработка sleeping islands с проверками waking, support removal и callbacks.
  Эти более широкие изменения здесь автоматически не реализованы.
- На 1000 кубах активный solver/projection остаётся тяжёлым. Instancing,
  GPU timestamps, освещение/тени и новый physics backend в эту задачу не добавлялись.
