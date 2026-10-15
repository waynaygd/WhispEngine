# WhispEngine: Broadphase и fixed timestep

Проверено 10 октября 2026 года, Windows, MSVC x64 Release, DX12, Tracy 0.14.1, enkiTS с 16 task threads (включая главный). Ветка `job_system`. Коммит, push, PR и merge не выполнялись. Предыдущие изменения сохранены. Этот отчёт дополняет PHYSICS_STRESS_REPORT.md и PHYSICS_STABILITY_FINAL_REPORT.md, не заменяя результаты прошлых проверок.

## Причины и границы исправления

В прежнем пути каждый вызов Broadphase заново создавал узлы unordered_map, векторы ячеек и unordered_set для удаления дубликатов. Broadphase вызывается дважды на внутренний substep, в том числе для projection. Даже неподвижная сцена повторяла построение сетки и вычисление повёрнутых AABB. Это измеренные расходы, а не доказательство дефекта enkiTS.

PhysicsSystem находился внутри рендерного пути и получал переменный dt. Длинный кадр увеличивал объём следующего физического обновления через адаптивные substeps: возникала обратная связь нагрузки. Старый аккумулятор fixed StateMachine не задавал fixed dt для ECS-физики. Теперь расписание физики находится вне циклов окон и рендера.

Исходный цикл: Time::Tick измеряет steady clock, сохраняет raw frame delta и отдельно ограничивает игровой dt до 0.1 s; затем camera/StateMachine, UpdateEcs (диагностика событий), editor и World::UpdateSystems внутри render-пути. Pipeline запускал PhysicsSystem и RenderSystem вместе. Изменения ECS через editor происходят на главном потоке; физические задачи синхронно ожидаются внутри Update, события публикуются после завершения стадий. Теперь только PhysicsSystem отмечен fixed, остальные системы остаются frame; весь World механически в fixed цикл не переносился.

Broadphase и расписание изменены отдельно. Гравитация, массы, трение, restitution, sleep/wake, поддержка островов, solver iterations, внутренние адаптивные substeps, Narrowphase и projection не упрощались. Solver и Broadphase остаются последовательными; Integrate/Narrowphase/Pose используют существующие enkiTS-задачи и ожидания зависимостей. Проблема общего времени активного Solver остаётся.

## Broadphase

`PhysicsBroadphaseStorage` принадлежит PhysicsSystem. Вместо узлов контейнеров используются переиспользуемые плоские массивы геометрии, занятых ячеек, больших тел, пар и открытая хеш-таблица ключей пар. Записи ячеек сортируются, пары канонизируются и сортируются как прежде. Сохранены размер ячейки 0.6, contact margin, фильтр двух неподвижных тел и overflow-путь для объектов, занимающих больше 256 ячеек. Сетка содержит только текущие занятые ячейки, история координат не накапливается.

AABB переиспользуется при неизменных entity/generation, форме, размерах и вращении. Для box используются уже вычисленные оси с тем же порядком арифметических операций. Полный список пар переиспользуется только при точном совпадении всей геометрии: состава и порядка тел, идентичности, центра с collider offset, rotation, размеров, формы и классификации InvMass==0. Проверяются также бодрствующие и статические тела. Сам признак sleeping не служит основанием для пропуска Broadphase.

Кеш содержит числовые значения и Entity, без сохранённых указателей на ECS-компоненты или BodyRef между ticks. Удаление, смена поколения, Clear, изменение collider и перемещение пола инвалидируют соответствующие данные. ResetState освобождает storage вместе с contact cache. Обычные перестроения сохраняют capacity до пикового размера текущей сцены; это не обещание постоянного размера при увеличении нагрузки.

Пары возвращаются по const reference без копирования. Jobs завершаются до следующего изменения массива. Narrowphase, Solver, острова, поддержка, sleep и события выполняются и при reuse. Главный поток остаётся владельцем ECS; схема записи worker-потоков в отдельные slots и ожидания между стадиями сохранена. Создание потоков на тело не добавлено. JobSystem и ResourceManager в этом проходе функционально не менялись; прежние исправления lifetime/shutdown сохранены.

Счётчики rebuild/reuse, AABB computations и buffer growth показывают работу этих пяти буферов. В испытаниях 100–1000 кубов было пять первоначальных увеличений capacity на режим и затем ни одного за 600 обновлений. Это не глобальная трассировка malloc: другие части физики всё ещё могут выделять память.

## Fixed timestep и интеграция

`FixedStepClock` использует double accumulator и реальное неклампированное время кадра из Time. PhysicsSystem получает строго `1/60` секунды. Максимум — четыре внешних ticks за кадр. Полные ticks сверх лимита отбрасываются, дробный остаток сохраняется меньше одного tick. Отброшенное время отображается явно. При постоянной перегрузке симуляция отстаёт от реального времени; алгоритм не гарантирует 60 физических ticks/сек на недостаточно быстром CPU.

Внешний tick и внутренний adaptive substep — разные уровни. В измеренной сцене fixed tick содержит четыре внутренних substeps, поэтому четыре catch-up ticks дают 16 внутренних substeps за кадр. Верхний внутренний лимит движка не уменьшался ради FPS.

ISystem получает признак fixed update, World/SystemPipeline — отдельные вызовы fixed и frame фаз. Старый UpdateSystems сохранён для существующих callers. Application вызывает fixed ECS только во время Play. В fixed-режиме StateMachine использует тот же clock и tick; отдельного конкурирующего аккумулятора больше нет. В variable-режиме StateMachine и камера продолжают получать frame dt. Editor и Render работают один раз за свой кадр, вне физического tick.

Play/Stop/resume, смена update mode, создание/очистка/перезапуск сцены сбрасывают clock epoch: accumulator, accumulated dropped time и simulation seconds. Первый последующий frame delta игнорируется, чтобы время паузы и загрузки сцены не превращалось в catch-up. Статистика уже выполненного кадра не стирается кнопкой Stop в конце кадра. При переходе состояния внутри tick цикл прерывается. Collision callbacks выполняются после ожидания задач; отдельный regression очищает World из callback и безопасно выполняет следующий пустой tick.

`PhysicsFrameStatistics` суммирует времена стадий по реально выполненным ticks кадра; отдельно хранит last tick time, ticks/frame, сумму внутренних substeps и clock counters. Пары/контакты — максимум за ticks, тела/active/sleeping — последняя выборка. Active excludes static bodies. Если tick не выполнялся, times/pairs/contacts равны нулю, а counts-only обновление с dt=0 сохраняет актуальные числа тел, без интеграции. Поэтому медиана physics/frame при быстром renderer может быть нулевой; это нельзя выдавать за стоимость физического tick.

Существующая панель Physics Stress Test сохранена: Create, Clear, Restart, 100/250/500/1000, Serial/Parallel, Play/Stop. Добавлены fixed counters, frame/tick time, dropped time и Broadphase rebuild/reuse. Сцена по умолчанию содержит 500 видимых кубов с общими render resources и статическим видимым полом; камера, размеры, отсутствие начального массового пересечения и очистка из предыдущего прохода сохранены. Стресс-сцена запускается по кнопке или opt-in CLI, а не создаётся автоматически при обычном запуске.

## Интерполяция

Render сейчас использует последний физический Transform. При renderer 120/144 Hz часть кадров повторяет pose: визуальная ступенчатость возможна, хотя траектория и события от частоты рендера не зависят. Интерполяция не внедрена в этом проходе: текущий RenderSystem читает ECS Transform, и временная запись интерполированного Transform могла бы изменить физику или collider/debug/editor данные.

Безопасный следующий шаг: отдельные previous/current value snapshots поз по Entity+generation, alpha=accumulator/tick, интерполяция только render matrices (rotation через quaternion slerp), без записи в ECS Transform. Сброс snapshots при teleport, Create/Clear/Restart и Play transitions; новые entities начинают с previous=current. Это отдельное изменение renderer, требующее проверки multi-window и debug colliders.

## Сборка и regression

Release-сборка всех целей после изменений успешна. В этом проходе выполнялась incremental build, не новая clean-first сборка. Последний CTest: PhysicsRegression 9.98 s, ResourceRegression 0.33 s; 2/2 passed, всего 10.32 s (после усиления проверки полного порядка collision events). Старые regression-сценарии не удалены.

Новые проверки:

- Независимый O(N²) AABB oracle со старой прямой формулой сравнивает полный отсортированный список пар на rebuild и reuse. Проверены box/sphere, отрицательные координаты, плотная сцена, большие overflow-тела, рост таблицы дубликатов, offset/rotation/dimensions/type, движение статического пола, wake, уничтожение и повторное использование entity, World.Clear без ResetState и пустая сцена. Oracle отключён в обычном движке и измерениях.
- Production FixedStepClock: 300 кубов и пол, 4 simulated seconds, Serial и Parallel при render cadence 30/60/120/144 Hz. Все восемь комбинаций выполнили 240 ticks; position/rotation/velocity/angular velocity/sleep совпали с допуском 1e-5, последовательности collision events совпали (227972 events).
- Stall 2 s выполняет ровно четыре ticks, отбрасывает 1.933333333 s; следующий Advance(0) не догоняет старый долг. Pause/resume/reset не сохраняют backlog. Проверено разделение frame/fixed фаз и Clear из collision callback.
- Существующие стресс-проверки create/restart/clear, чужие entities, stale handles, равенство Serial/Parallel и finite/floor bounds прошли для 100/250/500/1000.
- ResourceRegression проверил async cache reuse, финализацию, fallback отсутствующего ресурса и shutdown с pending decode. Закрытия реального движка ниже также завершились с кодом 0.

## Сопоставимые headless измерения

Перед редактированием сохранены baseline executables. Одинаковые настройки и layouts; каждый size — 600 обновлений при dt=1/60, 12 solver iterations, четыре внутренних substeps. Время — ms, percentiles nearest rank. Число тел включает ещё один статический пол. Истории bodies/sleeping/substeps/pairs/contacts/points совпали во всех 4800 соответствующих строках до/после. Это проверка контактной истории; прямые before/after snapshots всех pose/velocity не сохранялись.

| Кубы | Режим | Median до → после | p95 до → после | p99 до → после | Max до → после | Broadphase mean до → после |
|---:|---|---:|---:|---:|---:|---:|
| 100 | Serial | 0.651 → 0.364 | 2.786 → 2.437 | 3.012 → 2.546 | 3.474 → 4.001 | 0.262 → 0.015 |
| 100 | Parallel | 0.652 → 0.364 | 2.760 → 2.439 | 3.121 → 2.617 | 3.838 → 2.923 | 0.263 → 0.017 |
| 250 | Serial | 1.873 → 1.014 | 8.137 → 7.942 | 8.872 → 8.837 | 10.088 → 10.115 | 0.780 → 0.113 |
| 250 | Parallel | 1.908 → 1.013 | 8.212 → 7.885 | 8.773 → 8.876 | 10.151 → 11.269 | 0.788 → 0.113 |
| 500 | Serial | 4.492 → 2.354 | 22.573 → 18.109 | 28.634 → 19.965 | 34.921 → 24.437 | 2.079 → 0.801 |
| 500 | Parallel | 4.669 → 2.037 | 25.694 → 17.937 | 32.450 → 18.941 | 34.716 → 21.155 | 2.433 → 0.814 |
| 1000 | Serial | 35.710 → 33.471 | 52.744 → 43.642 | 60.866 → 51.116 | 72.157 → 58.392 | 5.203 → 5.481 |
| 1000 | Parallel | 35.729 → 32.789 | 53.560 → 42.721 | 60.579 → 47.387 | 66.319 → 74.118 | 5.692 → 5.539 |

100/250 используют прежний fallback Serial для маленьких batch. При 1000 почти всё время нужна перестройка; сортировка плоской сетки не всегда выигрывает. У Serial Broadphase mean ухудшился, у Parallel вырос единичный max. Универсального улучшения всех percentiles нет.

Дополнительно выполнены три пары legacy benchmark (576 dynamic bodies + floor, 150 samples/mode/run, одинаковый warmup). Порядок before/after чередовался. Совокупные 450 samples на строку:

| Режим | Версия | Median | p95 | p99 | Max | Broadphase mean |
|---|---|---:|---:|---:|---:|---:|
| Serial | До | 15.502 | 18.968 | 21.955 | 23.567 | 0.784 |
| Serial | После | 14.639 | 16.300 | 18.525 | 19.981 | 0.035 |
| Parallel | До | 15.290 | 17.520 | 18.599 | 19.326 | 0.778 |
| Parallel | После | 14.513 | 16.271 | 17.734 | 26.229 | 0.044 |

Median общего update улучшился примерно на 5–6%; Broadphase mean — на 94–96% в этом преимущественно неподвижном layout. Ускорение Parallel относительно Serial остаётся небольшим. В первой отдельной паре Parallel p99 ухудшился 17.520 → 19.330 ms. Ранее выполненные три не чередовавшихся after-прогона показали заметную вариативность (Serial median 23.923, 16.364, 14.587 ms); среда не изолирована от других приложений и частот CPU. Поэтому основные выводы опираются на чередующиеся пары и не обещают гарантированный FPS.

## Длительный тест 500

7200 ticks на каждый режим, 120 simulated seconds, оба exit 0:

| Режим | Median | p95 | p99 | Max | Broadphase mean |
|---|---:|---:|---:|---:|---:|
| Serial | 2.179 | 2.974 | 17.469 | 22.686 | 0.0905 |
| Parallel | 1.884 | 2.521 | 17.715 | 20.693 | 0.0926 |

После tick 600: rebuild=0, AABB computations=0, buffer growth=0; все 500 кубов sleeping. Контакты продолжают обрабатываться. В последних окнах 80–120 s Parallel median 1.873–1.911 ms; монотонной деградации не обнаружено. Первоначальные пять buffer growth на режим не продолжались. Это 120 simulated seconds, а не многочасовой soak.

Средние стадии Parallel за весь тест: Integrate 0.050, Broadphase 0.093, Narrowphase 0.388, Solver 1.255, Pose 0.051, Projection 0.136 ms. Остальное — сбор, острова/поддержка/sleep/cache/callbacks и обвязка. Dispatch/Wait уже входят в stage wall times и повторно не суммируются.

## Реальный движок: DX12, CSV и Tracy

Выполнены восемь запусков новой сборки и один baseline, все exit 0: baseline capture 1500 frames; новый capture 5000 frames; lifecycle 744 frames в variable и fixed StateMachine; искусственный медленный кадр в Serial/Parallel; smoke 300 frames для 100/250/1000. Проверено реальное создание окна, render path, GPU resource upload, frame/present/fence zones и штатное закрытие. Lifecycle использует те же Application API, что кнопки панели; ручные нажатия всех кнопок ImGui и отдельная визуальная проверка интерполяции не выполнялись.

Tracy captures получены matching CLI 0.14.1. В новой записи есть PhysicsFixedFrame (5000 scopes), PhysicsFixedTick (2304 scopes), PhysicsSystem, все старые stages Serial/Parallel/Job, JobDispatch/JobWait, RenderGather и EditorLayer. Physics job events присутствуют на всех 16 task thread IDs, включая главный поток: worker activity реально записана. PhysicsFixedFrame находится вне Render. PhysicsSystem scopes включают dt=0 counts-only вызовы, поэтому их среднее нельзя читать как среднее полноценного tick. GPU timestamp timings не измерялись.

Все 2304 PhysicsFixedTick из Tracy: median 2.483, p95 17.790, p99 23.398, max 29.562 ms. CSV physics stages не включают весь внешний scope. BroadphaseSerial mean одного вызова 0.0347 ms; вызовов 18432 (два на внутренний substep). EditorLayer mean 0.207, RenderGather mean 0.979 ms; редактор в этой записи не основной источник расходов.

Новый capture CSV: frame median 6.960, p95 10.403, p99 20.018, max 105.095 ms. В 2812/5000 кадров нет физического tick. Simulation seconds 38.4, cumulative dropped 0.6333 s в текущем clock epoch, maximum ticks/frame=4. Последние 500 кадров: frame median 6.961, max 14.081 ms.

Худший кадр 154 всё ещё тяжёлый: frame 105.095 ms, physics 99.030 ms, четыре ticks/16 internal substeps, Solver 52.358, Projection 29.897, Broadphase 8.197 ms. Это оставшаяся стоимость активных контактов и catch-up, а не исчезнувшая проблема. Fixed timestep ограничивает количество ticks, но не длительность каждого tick.

Baseline capture: frame median 8.230, p99 65.178 ms; physics/update median 3.728, p99 58.791 ms, maximum internal substeps=12. Эти визуальные истории различаются по dt, числу frames и длительности; их нельзя использовать как строгий before/after FPS benchmark. Сопоставимые результаты находятся в headless таблицах выше.

Injected sleep на frame 50 по 1000 ms: реальные frame times 1016.041 ms Serial / 1012.136 ms Parallel. Выполнено по четыре ticks, 16 internal substeps, отброшено по 0.95 s; accumulator меньше 1/60. Следующий кадр — 0 ticks Serial / 1 tick Parallel, дополнительного догоняния старого долга нет. Последующая нагрузка может порождать новое dropped time.

Smoke 100/250: counts 101/251, максимальный ticks/frame=1, dropped=0. Smoke 1000: counts 1001, максимум четыре ticks/frame, cumulative dropped=10.0667 s за 300 кадров; simulation seconds=11.75. Это демонстрирует недостаточную производительность для real-time 60 Hz на такой нагрузке, а не успешный тест стабильных 60 FPS. Выход штатный, runaway backlog не накапливается.

## Артефакты, воспроизведение и файлы

Локальные исходные CSV, логи сборок/CTest, paired/count/long summaries и captures: `out/diagnostics/fixed/`. Captures: `visual-before/engine-settled.tracy`, `visual-after/engine-settled.tracy`; новый export отдельных событий — `visual-after/events.csv`. Не включены в git. Первые headless after-counts/after-long CSV созданы до исправления конструктора диагностического sample: добавленные outer-clock колонки там нулевые и не используются для оценки ticks; stage/Broadphase/count columns корректны. Paired-after и все CSV реального движка используют окончательный формат.

Команды приведены в tests/README.md; запуск engine из `out/build/x64-release` обязателен для поиска DXIL/assets. `--slow-frame 50 1000`, `--fixed-state`, `--diagnostics`, `--stability-scenario` являются opt-in. CapturePhysicsTracy.ps1 поддерживает `-EngineExecutable` и `-Frames` для saved baseline.

Добавлены: engine/core/FixedStepClock.h, engine/core/PhysicsFrameStatistics.h, engine/ecs/systems/PhysicsBroadphaseStorage.h, этот отчёт. В данном проходе изменены Application.h/.cpp, ISystem.h, World.h/.cpp, SystemPipeline.h/.cpp, PhysicsSystem.h/.cpp, EditorLayer.cpp, WhispEngine.cpp, PhysicsDiagnosticSample.h, PhysicsRegression.cpp, CapturePhysicsTracy.ps1 и README.md. Остальные dirty files относятся к предыдущим этапам и сохранены; полная совокупность изменений не выдаётся за новый независимый diff.

## Что осталось

Solver, projection и обработка контактов sleeping bodies остаются существенными. Контакты/события не отключены ради цифр. Active 1000 требует частой сортировки сетки; Broadphase не всегда быстрее baseline. Лимит четырёх ticks не предотвращает отдельные 100 ms кадры и явно замедляет симуляцию при перегрузке. Render interpolation отсутствует. Выполненный soak ограничен 120 simulated seconds. Старый единичный crash 0xc0000409 и старый p99 около 144 ms не воспроизведены в этом проходе; нельзя заявлять доказанное устранение их причины. Новые запуски и regression проходят, но это не доказательство отсутствия всех гонок, редких GPU/driver ошибок или длительных сценариев.
