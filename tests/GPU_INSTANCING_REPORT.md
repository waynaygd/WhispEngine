# WhispEngine — GPU Instancing DX12

Проверка 10 октября 2026, Windows, MSVC x64 Release; DX12, Vulkan и Tracy включены. Ветка job_system. Прежние локальные изменения и отчёты сохранены; commit, push, merge и PR не выполнялись. Физика, Fixed Timestep, JobSystem, ResourceManager и RenderInterpolation в этом этапе не изменены. Итоговые GPU-проверки описаны отдельно от CPU regression.

## Аудит и выбранный путь

Исходный RenderSystem обходил ECS, получал готовые ресурсы через ResourceManager, разрешал интерполированный RenderPose и копировал его в snapshots. Prepare строил MVP и tint; Submit для каждого объекта устанавливал shader/texture/constant buffer и вызывал DrawMesh. DX12 уже использовал DrawIndexedInstanced, но с InstanceCount=1. HLSL читал gMVP/gTint из per-draw CB; instance input отсутствовал. Общие GPU mesh/texture/shader handles уже кешировались, поэтому повторная загрузка геометрии не была причиной отдельных draws.

Новая стадия RenderBatchBuild работает в этом же RenderSystem после Prepare. Нет специальной отрисовки StressScene. В IRenderAdapter добавлены capability, DrawMeshInstanced(span) и статистика; общий код не содержит DX12 API. Используется instance vertex buffer (slot 1, PER_INSTANCE_DATA, step rate 1): это соответствует существующим vertex/index buffers и не требует нового SRV binding или изменения root signature.

Пакет объединяет только **соседние** packets с одинаковыми фактическими GPU handles mesh, shader и texture. Material сейчас разрешается в shader/texture/tint; tint индивидуален. Названия материалов с одинаковым эффективным состоянием не требуют разных draws. Shader handle определяет PSO/root signature; blend/depth/rasterizer в текущем backend фиксированы. Каждый Update — отдельная camera/pass submission, batches не переносятся между Update. Если появятся изменяемые per-material blend/depth/rasterizer/pass states, их нужно включить в ключ до поддержки такого рендеринга. Сортировки нет: порядок объектов и draws сохранён, но разделённые другими объектами совместимые packets не объединяются.

## Instance data и HLSL

RenderInstanceData содержит model[16] (offset 0), подготовленный MVP[16] (64), tint[4] (128), stride 144 байта; размеры проверены static_assert. Prepare вычисляет model один раз и передаёт прежний готовый MVP. Интерполяция остаётся в общем RenderSystem, ECS Transform не меняется; backend не разрешает физические poses и не пересчитывает camera matrices.

Только dx12/textured.hlsl получил VSInstancedMain и маркер WHISP_INSTANCE_LAYOUT_V1. Четыре MVP columns поступают из instance input, transpose восстанавливает прежнюю матрицу для mul(MVP, position). Tint проходит в PS через nointerpolation COLOR0; UV, texture sampling и прежняя alpha=1 сохранены. VSMain передаёт обычный gTint тем же путём. Model присутствует в payload, но текущий unlit shader использует уже подготовленный MVP — результат model/view/projection общего Prepare. Это намеренные лишние 64 байта на instance ради общего контракта и точного совпадения преобразований; освещение/нормали не добавлены.

Instanced PSO создаётся только для opt-in shader и успешной компиляции VSInstancedMain. Reflection VS/PS дополнительно запрещает читаемые per-draw constant buffers в instanced варианте: нельзя подставить общий gTint вместо индивидуального. Ошибка optional variant сохраняет валидный обычный PSO. Старые shaders и одиночные packets рисуются обычным путём. DrawMeshInstanced возвращает false до записи instanced draw при неподдерживаемом state или неудаче allocation; RenderSystem выполняет fallback. Vulkan по умолчанию отвечает capability=false.

## GPU память, fences и lifetime

Два frame slots имеют собственные persistently mapped upload pages. Минимальная capacity страницы 512 instances, рост степенями двойки. В течение frame данные дописываются в свободный диапазон. Если места недостаточно, используется следующая страница: уже записанный draw продолжает ссылаться на прежнюю память. Заменять ресурс разрешено только для ещё не использованной страницы. VB address включает offset диапазона, SizeInBytes=count*144, StartInstance=0. Нет upload resource на объект и нет обязательного GPU allocation каждый frame; CPU vectors также переиспользуются.

BeginFrame ждёт последний submitted fence данного slot **до** reset allocator/pages.used и освобождения retired ресурсов. EndFrame отправляет command list и сохраняет fence slot. Существующее ожидание после Present оставлено: сейчас backend практически сериализует frames, полноценное перекрытие CPU/GPU не внедрено. Раздельные slots и явный fence защищают новый storage и при последующем изменении политики ожидания.

Обнаружена отдельная lifetime проблема: WaitForGpu не защищает draw, который записан, но ещё не отправлен. DestroyMesh/Texture/Shader во время recording теперь откладывает освобождение COM resources и reuse texture SRV descriptors до завершения fence frame. Вне recording используется прежнее ожидание GPU. Shutdown ждёт GPU, unmap и освобождает pages/retired resources; Resize не создаёт новые instance pages.

У обычного пути был unsafe clamp CB index к последнему slot: следующие draws перезаписывали одну и ту же память. Теперь при превышении старого лимита 2048 обычных draws frame draw пропускается с warning, а не портит ранее записанные команды. Instanced draws не расходуют этот CB pool. Сцены до 1000 кубов+пол укладываются и в OFF; лимит обычного пути не расширен.

## Переключатель и статистика

В существующей панели **Physics Stress Test** добавлен GPU Instancing (по умолчанию ON). Настройка сохраняется при пересоздании scene/render runtime; CLI --no-instancing включает прежние отдельные draws. Create/Clear/Restart, Play/Stop, Serial/Parallel и Render Interpolation используют прежние операции.

Visible ready objects означает видимые по ECS flag объекты с готовыми handles, а не результат frustum/occlusion culling. Draws и Instanced Draw Calls считаются в backend после записи настоящих команд; Rendered Instances — число mesh instances в них. Instance Batches равен реальному числу instanced commands, а не числу CPU groups. Capacity — суммарное количество entries всех allocated pages обоих slots; после Clear оно остаётся резервом, used data не сохраняются. Scene upload учитывает instance payload и обычные 256-byte CB записи.

Статистика относится к последнему RenderSystem Update: mesh и debug draws включены, ImGui и загрузки asset buffers исключены. Debug primitives считаются draws, но не mesh instances. Gather/Prepare/Submit — CPU wall time; BatchBuild также записан в CSV/Tracy. При нескольких windows/passes это последний Update, не агрегат всего приложения.

## Выполненные проверки

- Incremental Release build существующей конфигурации с ENABLE_DX12=ON, ENABLE_VULKAN=ON, ENABLE_TRACY=ON. Это не чистая сборка и не отдельная Vulkan-only конфигурация.
- Итоговый CTest: 4/4 passed, 22.72 s — PhysicsRegression 15.62 s, ResourceRegression 0.33 s, RenderInterpolationRegression 6.29 s, GpuInstancingRegression 0.46 s. Старые проверки не удалены и не ослаблены.
- Новый CPU regression использует настоящий RenderSystem и ResourceManager с записывающим adapter: 100/250/500/1000+пол, совместимые и различные handles, unsupported shader, failed instanced upload fallback, индивидуальный tint, model/MVP, interpolation ON/OFF, teleport/scale, pause/resume, camera projection change, deletion/generation reuse с проверкой replacement model, Clear/Restart, pending resources и неизменность ECS Transform.
- Отдельный **настоящий DX12** regression --dx12: 500 кубов+пол, checker texture/UV, разные tint, вращение и масштабы, интерполированные matrices. OFF/ON GPU readback RGBA побитово совпал; проверяется, что изображение содержит геометрию, а не только clear. Реальные counters 501 draws/501 instances и 1 instanced draw/501 instances. Untagged shader fallback также побитово совпал; tagged shader с per-draw CB был отвергнут для instancing, обычный PSO сохранился.
- DX12 test повторяет slots, изменяет окно 320x240→400x280, проверяет неизменную capacity, записывает два draws с ростом следующей страницы (501 и 1200 instances), удаляет mesh/texture/shader до submit и завершает несколько frames/Shutdown. Это проверка выполнения/lifetime, не доказательство всех возможных GPU interleavings.
- Обычный WhispEngine: четыре paused comparison runs по 1300 frames с подключённым Tracy; четыре active physics smoke по 200 frames (100/250 Serial, 500/1000 Parallel); 500 с interpolation OFF 200 frames; lifecycle 744 frames (Play/Stop/Resume, Serial/Parallel, Restart, Clear, Recreate). Все engine и capture exit codes 0; 501 entities после Create/Restart/Recreate, 0 после Clear. Это использует production UI callbacks, но не ручные нажатия ImGui.
- После финальной проверки shader reflection ещё один ordinary engine smoke (--render-benchmark 500 без capture) завершился code 0: все 1200 samples подтвердили 501 objects/instances, 600 OFF с 501 draws и 600 ON с одним instanced draw. Этот smoke не включён в performance tables; CSV/log paused-500-final-smoke.*.
- ResourceRegression проверяет существующую асинхронную загрузку, cancellation/shutdown; ordinary DX12 runs дополнительно используют async finalization. ResourceManager/JobSystem не переписаны.

Последний GPU log: out/diagnostics/instancing/dx12-checker-final.log. Build/CTest: build-final.log, ctest-final.log. Остальные логи/CSV/captures находятся там же. Артефакты локальные и не добавлены в git.

## Сопоставимые измерения OFF/ON

Каждый размер измерен в одном engine process на **неподвижной одинаковой сцене**, с одинаковыми camera/assets/tints, без physics ticks. Только opt-in --render-benchmark ставит Play=false; обычная симуляция сохраняется. После 100 warmup frames идут OFF/ON/OFF/ON по 300 frames: 600 наблюдений на режим, 4800 всего. Процентили nearest-rank. Tracy включён и подключён во всех сравнениях; сохранены .tracy и агрегаты *-zones.csv. Первое создание pages включено в ON samples; два роста на весь run, затем reuse без роста.

| Кубы (+пол) | Draws OFF→ON | Instanced OFF→ON | Instances | Scene upload OFF→ON, bytes | Итоговая capacity entries |
|---:|---:|---:|---:|---:|---:|
| 100 | 101→1 | 0→1 | 101 | 25856→14544 | 1024 |
| 250 | 251→1 | 0→1 | 251 | 64256→36144 | 1024 |
| 500 | 501→1 | 0→1 | 501 | 128256→72144 | 1024 |
| 1000 | 1001→1 | 0→1 | 1001 | 256256→144144 | 2048 |

Пол использует тот же mesh/shader/texture, отличается model/tint и поэтому в этой сцене совместим с кубами. Это не обещание одного draw для любой сцены. Все samples подтвердили указанное количество objects/instances/draws.

CPU stage times, **мс**:

| Кубы | Режим | Gather p50 | Prepare p50 | Batch p50 | Submit p50 | Submit p95 | Submit p99 |
|---:|:---:|---:|---:|---:|---:|---:|---:|
| 100 | OFF | 0.1766 | 0.0071 | 0.0007 | 0.0222 | 0.0333 | 0.0511 |
| 100 | ON | 0.1728 | 0.0071 | 0.0006 | 0.0077 | 0.0218 | 0.0434 |
| 250 | OFF | 0.4482 | 0.0174 | 0.0019 | 0.0452 | 0.0633 | 0.1280 |
| 250 | ON | 0.4784 | 0.0177 | 0.0023 | 0.0141 | 0.0260 | 0.0542 |
| 500 | OFF | 0.8541 | 0.0374 | 0.0049 | 0.0831 | 0.0955 | 0.1415 |
| 500 | ON | 0.8471 | 0.0372 | 0.0044 | 0.0146 | 0.0275 | 0.0388 |
| 1000 | OFF | 1.7011 | 0.0777 | 0.0091 | 0.1329 | 0.2421 | 0.3239 |
| 1000 | ON | 1.7208 | 0.0779 | 0.0095 | 0.0191 | 0.0338 | 0.0833 |

CPU frame wall time, **мс**, включая editor/Present/fence waits:

| Кубы | Режим | p50 | p95 | p99 | mean |
|---:|:---:|---:|---:|---:|---:|
| 100 | OFF | 1.4618 | 2.3202 | 2.7269 | 1.5700 |
| 100 | ON | 1.3841 | 3.2335 | 4.2337 | 1.6378 |
| 250 | OFF | 1.9213 | 7.0207 | 7.2090 | 3.5046 |
| 250 | ON | 4.2161 | 7.8179 | 8.9160 | 4.5708 |
| 500 | OFF | 5.0841 | 7.0817 | 7.9539 | 4.8416 |
| 500 | ON | 2.6976 | 6.6829 | 7.0715 | 3.0969 |
| 1000 | OFF | 3.6887 | 6.0264 | 7.6737 | 4.0013 |
| 1000 | ON | 3.8661 | 6.7860 | 8.1795 | 4.2324 |

Submit p50 снизился на всех размерах (~3–7 раз); для 500: 0.0831→0.0146 ms. Gather остаётся намного дороже submission и в этом этапе не оптимизирован. Frame time на 250 и 1000 стал хуже по p50/mean, на 100 ухудшились tails/mean. На 500 он улучшился в данном прогоне. Чередование блоков исключает разные состояния physics, но не системный шум, работу драйвера/editor и waits. Эти измерения **не доказывают универсальный рост FPS или ускорение GPU**. Начальная allocation даёт максимальный ON submit примерно 0.5–0.58 ms; steady percentiles существенно ниже.

GPU timestamps отсутствуют; GPU frame time не измерен. Readback сравнивает изображения, не время выполнения GPU. Tracy зоны — CPU timings. В каждом ordinary paused capture присутствуют RenderBatchBuild, InstanceUpload, InstancedDraw, RenderFallbackDraw, RenderGather/Prepare/Submit; InstancedDraw и InstanceUpload имеют по 600 событий. Новых зон на отдельный instance нет. Существующий RenderInterpolationResolve по объектам сохранён; его overhead включён в оба режима. Счётчик growth отслеживает pages, не глобальные malloc/VRAM peak.

## Команды воспроизведения

Из корня репозитория, PowerShell (существующий build directory):

~~~powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out/build/x64-release --parallel 8'
Push-Location out/build/x64-release
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --output-on-failure
./GpuInstancingRegression.exe --dx12
./WhispEngine.exe --render-benchmark 500 render-500.csv
./WhispEngine.exe --stress 500 --frames 200 --no-instancing
./WhispEngine.exe --stress 500 --frames 200 --no-interpolation
./WhispEngine.exe --stability-scenario lifecycle.csv
Pop-Location
./tests/RunGpuInstancingDiagnostics.ps1
~~~

--render-benchmark автоматически чередует режимы; --no-instancing предназначен для обычных runs. Script требует matching Tracy 0.14.1 capture CLI по out/diagnostics/tracy-tools/tracy-capture.exe и запускает --wait-tracy. Без capture сравнение CSV возможно, но несопоставимо по profiling overhead с приведённым. Закройте другие engine/capture процессы перед script. Script перезаписывает собственные артефакты. Для экспорта zones из корня:

~~~powershell
./out/diagnostics/tracy-tools/tracy-csvexport.exe out/diagnostics/instancing/paused-500.tracy | Out-File -Encoding utf8 out/diagnostics/instancing/paused-500-zones.csv
~~~

## Vulkan, изменённые файлы и ограничения

Vulkan backend собрался вместе с DX12, общая abstraction сохраняет default capability=false/DrawMeshInstanced=false. Vulkan instancing не реализован; runtime Vulkan не проверен. Его прежний primitive path не получает DX12 вызовов, resource-driven mesh rendering остаётся имеющимся ограничением Vulkan.

Изменения этого этапа:

- engine/ecs/systems/RenderSystem.h/.cpp — общий payload, batches, fallback, CPU/submission stats.
- engine/render/IRenderAdapter.h, новый RenderInstanceData.h — optional API/контракт.
- engine/render/backends/dx12/Dx12RenderAdapter.h/.cpp — instanced PSO, upload pages, fences, retirement, real counters, diagnostic readback.
- engine/shaders/dx12/textured.hlsl — optional instance input/индивидуальный tint.
- engine/core/Application.h/.cpp, новый RenderDiagnosticSample.h; WhispEngine.cpp — preference, opt-in paused benchmark и CSV.
- engine/editor/EditorLayer.cpp — checkbox и stats в существующей панели.
- CMakeLists.txt, новый tests/GpuInstancingRegression.cpp, RunGpuInstancingDiagnostics.ps1, tests/README.md, этот отчёт.

Остальные dirty файлы относятся к предыдущим этапам. Не изменены physics settings/algorithms, clock, interpolation history, jobs и manager. Сохранена исходная смешанная кодировка старых .cpp.

Ручная оценка картинки и нажатия UI, несколько OS windows/camera submissions одновременно, Vulkan runtime, D3D12 validation-layer capture, GPU timestamps и длительный soak нового renderer не выполнялись. Проверка камеры в common regression и actual DX12 resize выполнены. CPU counters означают записанные команды, а не аппаратные GPU performance counters. Текущая прозрачность отсутствует (opaque PSO/alpha=1); instancing не вводит transparency и сохраняет порядок существующих packets. При её добавлении потребуются соответствующие pass/state key и shader contract.

Buffers сохраняют peak capacity до Shutdown, ordinary CB pool остаётся прежним и allocation-heavy при Initialize. При большом числе уникальных batches эффективность ниже; обычные draws ограничены 2048/frame. Upload через CPU-visible heap и дублирование model+MVP могут потребовать последующей оптимизации после GPU timestamp измерений. Эти ограничения не скрыты сокращением числа draw calls.
