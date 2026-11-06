# ЛР №3, вариант Г: воспроизводимые performance evidence

Дата: 5 ноября 2026. Этот отчёт различает архитектурные проверки, timing samples и GPU captures. Историческое ускорение Material System **NOT MEASURED**. Сравнение 1/10/100/500 материалов относится к **текущей системе**, а не к историческому before/after.

## 1. Цель и оригинальное ТЗ

Измерить цену разрешения материалов и steady-state рендера при одинаковой геометрии; показать подготовку и повторное использование PSO, смену параметра и успешный shader reload. Оригинал: `D:\Downloads\Telegram Desktop\ЛР 3 Рендер ТЗ.pdf`, SHA-256 `0dea69cc1f73568ed82e0810d8ecfa6b5c15c535ef82d8c404aa5394fd047c90`. Извлечённый текст: [original-assignment.txt](../out/lab3-evidence/original-assignment.txt). ТЗ требует Release, прогрев, N≥3, median/p95/p99 и DX12 RenderDoc до/после; ускорение не является обязательным.

## 2. Hardware/software и исходники

- Windows 11, build 26200 (полная строка платформы в source-identification.json).
- CPU Intel Core i5-13500H; GPU NVIDIA GeForce RTX 4060 Laptop GPU, выбранный адаптер подтверждается строкой `DX12 GPU` каждого запуска.
- NVIDIA Windows driver `32.0.15.8157` из registry, [gpu-driver.json](../out/lab3-evidence/gpu-driver.json). Точная частота GPU, питание и температура **NOT MEASURED**.
- MSVC x64 Release, compiler `19.44.35225.0`, toolset directory `14.44.35207`, Windows SDK `10.0.26100.0`, CMake 3.31.6, Ninja; DX12. DX12/Vulkan/Tracy включены, Tracy on-demand без подключённого клиента в timing runs.
- RenderDoc x64 1.40, commit `6ab25b2d01b44149baf4551158398f9357610a60`. Встроенный Python 3.6.4 и `renderdoc.OpenCaptureFile` проверены: [renderdoc-python.json](../out/lab3-evidence/renderdoc-python.json). Standalone `renderdoc.pyd` не найден; используется официальный встроенный API через `qrenderdoc --python` ([исходник CLI](https://github.com/baldurk/renderdoc/blob/v1.x/qrenderdoc/Code/qrenderdoc.cpp)).
- Ветка `rendering_lab`, HEAD `7d879e96324447968f0ff072a9e34228c326952a`, tree `04cb1d7f73a604eca45f7fbd27270de94e0712b9`. Начальный Git status чистый. Измеряется архив HEAD плюс отдельный `MaterialPerformanceProbe`, добавленный в CMake **только копии исходников**. Production renderer не изменялся.

Полная идентификация и хеши исходной RenderingLab3: [source-identification.json](../out/lab3-evidence/source-identification.json). Исходники: `out/lab3-evidence/current-source`; сборка: `out/lab3-evidence/local-build`; рабочий каталог: `out/lab3-evidence/runtime`. Копии зависимостей находятся в `current-source/vendor`.

Хеши 6004 файлов snapshot и 627 runtime inputs, dependency commits/status и PE machine `0x8664` (x64): [build-runtime-hashes.json](../out/lab3-evidence/build-runtime-hashes.json). Хеши фиксировались после timing series, до RenderDoc и correctness-generated runtime changes.

## 3. Что означает исторический baseline

`git log --all -- engine/resources/MaterialResource.h tests/MaterialSystemRegression.cpp` выявляет два этапа:

1. `f4e7b9df3df56b9fb8f368e2afbec228ba2fdd03` впервые добавил MaterialResource. Его родитель **`4e4cf0f37f220c5ee632ffbb6a14a401893ad9e8`** — состояние до первого MaterialResource; этот путь в нём отсутствует.
2. `5240060eee628158378d8b5460ecd579f08cb017` добавил расширенную Material System и regression. Его родитель **`bbba558d8b39fffefc9b000762ec4a8f0cad1983`** содержит старый MaterialResource только с name/sourcePath/shaderPath/texturePath/baseColor/placeholder. Legacy materialPath реально используется в RenderSystem; это не версия без материалов вообще.

Подтверждения: [material-history.txt](../out/lab3-evidence/material-history.txt), [pre-first-material-source-evidence.txt](../out/lab3-evidence/pre-first-material-source-evidence.txt), [pre-expanded-source-evidence.txt](../out/lab3-evidence/pre-expanded-source-evidence.txt). Проверка исторического состояния выполнена по настоящим Git blobs. Исторический executable/replay **NOT VERIFIED**; замеры старого executable **NOT MEASURED**. Совместимость современных v4/v5 assets с прежним набором полей не заявляется.

Коммит `5240060` одновременно добавляет PBR, lighting, shadows, IBL и другие изменения (108 файлов). Между ним и HEAD также менялись renderer и editor. Полностью контролируемая Git-пара, отличающаяся только Material System, **не установлена**. Поэтому таблица ниже не называется историческим before/after и не доказывает FPS gain. При необходимости исторического эксперимента следует отдельно собрать оба commit и согласовать legacy-сцену одинаковой нагрузки, сохранив различия поддерживаемых данных.

Исторические `out/diagnostics/materials/{before,after}/shadow-500-1024.csv` содержат 501 instance (500 cubes + floor), шесть меняющихся режимов; это один прежний запуск каждой стороны, не три независимых. Before создавался при 31 незакоммиченном файле, что задокументировано в MATERIAL_SYSTEM_REPORT. Архив, patch, CSV и Tracy имеют [отдельный manifest](../out/lab3-evidence/historical-artifacts.json); их нельзя автоматически приравнять к чистому Git commit.

## 4. Повторное использование существующих средств

`RunMaterialDiagnostics.ps1`, `RunShadowDiagnostics.ps1`, `SummarizeMaterialDiagnostics.py`, `RenderDiagnosticSample.h`, MaterialSystem/GpuInstancing regression и Tracy integration изучены. Старый summarizer выбирает 270 кадров после 30 переходных кадров, но считает только медианы; не разделяет три процесса, не считает p95/p99. Shadow benchmark переключает instancing/shadows/PCF каждые 300 кадров. Multi-material benchmark использует 200 двухслотовых моделей. Они полезны для correctness и предыдущей диагностики, но не выполняют методику этой сцены.

Новый небольшой probe использует **существующие** ResourceManager, RenderSystem, DX12 adapter, таймеры materialResolveMs и PSO diagnostics, без изменения профилировщика. Внешний steady_clock вокруг RenderSystem::Update даёт CPU Render Time; frame timer охватывает PollEvents/BeginFrame/viewport/render/EndFrame/Present. Tracy traces остаются отдельными артефактами предыдущих запусков; новые timing series не требуют подключённого Tracy и не выполняются под RenderDoc.

## 5. Benchmark scene и фиксированные настройки

[scene/geometry.json](../out/lab3-evidence/scene/geometry.json) описывает отдельную сцену из ровно 500 одинаковых `validation_cube.obj`, без пола. Probe создаёт её непосредственно в своём World; JSON является manifest, а не файлом SceneSerializer для открытия в Editor. Сетка 25×20: `(i%25-12)*0.65`, `(i/25-9.5)*0.65`, z=20, scale=0.5; целочисленное деление для ряда. Камера (0,0,0), yaw=pitch=0, FOV=1.04719755 rad, near=0.1/far=100. Все 500 instances должны отправляться в каждом measured frame.

Viewport строго 1920×1080; native client window может быть меньше из-за рамки/рабочей области. Измеряется фиксированный offscreen viewport плюс обычный Present окна; это не headless/offscreen regression. Editor отсутствует: его CPU/GPU накладные расходы сюда не входят; переносить цифры на Editor нельзя.

HDR/tone mapping, AO, shadows выключены, directional выключен, ambient white=1, одна shader program `dx12/textured.hlsl`, один mesh, одинаковые параметры материалов. `defaults/texture` выбирает существующий initialized fallback texture; предупреждение загрузчика отражено в raw logs. Все варианты используют одинаковый fallback. Нет physics update, анимаций, случайных переключений, UI ввода. Число ECS material instances всегда 500; изменяется число **отдельных material assets**: 1/10/100/500, assignment `i % materialCount`. Это важное различие для интерпретации.

AA по умолчанию adapter — Off, sample count 1. В исходнике этого HEAD используется `Present(0,0)`: приложение не запрашивает vsync. Driver overrides и поведение compositor отдельно не измерялись.

Instancing OFF и ON хранятся отдельно в [scene](../out/lab3-evidence/scene); сравнение числа материалов проводится **внутри одного режима instancing**. OFF ожидает 500 main draws, ON — 1 main instanced draw, renderedInstances=500. Postprocess/output draws могут дополнительно присутствовать в RenderDoc; mesh counters адаптера не равны общему числу событий capture.

## 6. Методика и статистика

На каждый вариант запускаются три отдельных процесса. Порядок: run 1/2/3 → instancing 0/1 → materials 1/10/100/500. Между процессами нет общего живого PSO cache; системные/driver caches не очищаются. Это process startup, а не гарантированно холодный driver/disk cache.

Каждый процесс записывает все 340 кадров: 0–29 warmup, 30–299 measured (270 samples), 300–309 parameter-edit, 310–339 shader-reload. Последние 40 кадров исключены из performance summary. Время setup выводится отдельно в log. Первый успешный PSO compilation находится в warmup, а не steady-state. Чистое время вызова драйвера CreateGraphicsPipelineState отдельно **NOT MEASURED**: startup frame time включает и другие работы.

Внутри каждого запуска: median (для чётного N среднее двух центральных), p95/p99 = nearest rank `sorted[ceil(p*N)-1]`, min/max. Единицы всех таймеров — **ms**. Правило сводки зафиксировано до измерений: **медиана трёх run-level median/p95/p99 соответственно**. Кадры трёх процессов не объединяются для выдачи одного percentile. Хранятся девять десятичных знаков CSV; точность отображения не означает такую точность таймера.

Raw samples: `out/lab3-evidence/raw/`. Отдельные прогоны: [summary.csv](../out/lab3-evidence/summary.csv), сводка: [comparison.csv](../out/lab3-evidence/comparison.csv). Сгенерированные таблицы приведены в конце отчёта.

## 7. PSO evidence и ограничения измерений

Ограничение полноты варианта Г: текущий MaterialRenderState предоставляет doubleSided/receiveShadows/reverseWinding; произвольные blend/depth-test overrides в material assets не реализованы. Masked/Transparent сохраняются в schema с диагностикой, полноценный alpha pipeline не заявляется. Хотя PSO key учитывает blend/depth состояния, это не доказывает возможность редактировать их через материал. Требование ТЗ о состояниях blend/depth нельзя считать полностью подтверждённым этим benchmark; функциональные доработки в рамках этой задачи не выполняются.

Raw CSV содержит hits/misses/creations/cached на каждом кадре. Summary фиксирует creations после подготовки, начало/конец steady-state, после изменения roughness и после shader reload. Рост hits при неизменных creations показывает reuse, **не ускорение рендера**. Material cache hits/misses — другой cache; materialResolveMs — CPU timer resolver, а не PSO compilation/GPU time.

Для успешного результата ожидаются: стабильные creations/misses во всех measured frames; новые constants без новых PSO; после успешного reload увеличиваются creations и публикуется новый shader handle в log. Отдельный correctness regression дополнительно проверяет last-good при сломанном shader, pointers/normalized keys и retirement. Его readback не используется как timing sample.

Benchmark меняет roughness при Lambert shading: эта проверка показывает отсутствие новых PSO при обновлении material generation, но не демонстрирует визуальный эффект roughness. Видимое изменение instance tint без PSO подтверждено отдельно MaterialSystemDx12Regression; RenderingLab3 verifier проверяет живые Base Color edits. Эти correctness-проверки отделены от timing series.

GPU timestamps берутся из существующего adapter API; они асинхронны и могут относиться к предыдущему GPU frame. `gpuValid` сохраняется. Frame Time — CPU wall time с ожиданиями Present/fence, CPU Render Time включает CPU work/возможные ожидания внутри Update; это не GPU execution duration. Частоты, thermal throttling, фоновые приложения и vsync/display pacing не контролируются программой. Без подтверждения режима Present нельзя интерпретировать frame time как чистый throughput GPU. Startup и reload frames хранятся, но не смешиваются со steady-state.

Сериализация строки CSV и чтение diagnostics выполняются после остановки frame timer. Поэтому Frame Time — длительность функции кадра, а не полная wall-clock разница между началами соседних итераций; межкадровое логирование может влиять на заполнение GPU queue. На графиках видны ступени CPU времени и выбросы; они сохранены, не отфильтрованы. Нестационарность и возможное планирование на разных CPU cores ограничивают причинную интерпретацию небольших различий. N=3 не даёт надёжной оценки доверительного интервала.

## 8. RenderDoc

Исторические `material-before.rdc` и `material-after.rdc`: **NOT CAPTURED**. Их имена зарезервированы для подтверждённой исторической пары; разные конфигурации текущего HEAD ими не подменяются.

Автоматизированный сравнительный capture текущей системы: `current-1_frame*.rdc` и `current-500_frame*.rdc`, instancing OFF. Capture на стабильном frame 100 через официальный `RENDERDOC_GetAPI` + `TriggerCapture`; CSV внутри `captures/excluded-timing-*` не входит в performance summary. Запуск через `renderdoccmd capture`; replay через официальный [Python API](https://github.com/baldurk/renderdoc/blob/v1.40/docs/python_api/examples/renderdoc_intro.py).

Фактические capture paths, SHA-256, replay, draw events, shaders/textures/PSO и screenshots: см. итоговый раздел ниже. Пока результат не подтверждён, статус **NOT CAPTURED / NOT VERIFIED**. Обычные framebuffer readbacks и JSON не являются RDC.

## 9. Воспроизведение и сохранность

Из корня репозитория, x64 VS Developer shell:

```powershell
# Изолированная сборка, исходники HEAD уже извлечены в current-source:
cmd /c out\lab3-evidence\build-local.cmd
# Для полностью новой копии (сохраняет существующие evidence):
python tests/PrepareMaterialPerformance.py --root out/lab3-evidence-repro
# Затем RunMaterialPerformance.ps1 -Root out/lab3-evidence-repro
# Только после завершения сборки и без других GPU тестов:
.\tests\RunMaterialPerformance.ps1
# Python с matplotlib в изолированном python-packages:
$env:PYTHONPATH="$PWD/out/lab3-evidence/python-packages"
python tests/SummarizeMaterialPerformance.py out/lab3-evidence
# Только ПОСЛЕ завершения timing measurements:
.\tests\RunMaterialPerformance.ps1 -Capture
& 'C:/Program Files/RenderDoc/qrenderdoc.exe' --python "$PWD/tests/ReplayMaterialCaptures.py"
& 'C:/Program Files/RenderDoc/qrenderdoc.exe' --python "$PWD/tests/ScreenshotMaterialCaptures.py"
```

Скрипт не переключает ветки, не создаёт commit и не выполняет reset/clean. До повторного прогона сохраняйте предыдущий evidence directory; не перезаписывайте полученные raw данные. Изолированный CMake и copied vendor зафиксированы в out; first-run подготовка требует архива HEAD, добавления диагностического target из хвоста CMake копии и доступных dependencies. Сборка по одним только новым tracked test files автоматически не включается в production CMake.

Сохранены неудачные configure/build logs: `build-attempt1/2/3.log`; причины — Unicode path в Assimp response file, затем конфликт Windows SDK macros в новом probe. Неудачные runtime attempts до measured samples: `failed-attempt1-*` (кодировка пути CSV), `failed-attempt2-*` (проверка draws до готовности GPU ресурсов). Ни один такой запуск не выдан за успешный. Все exit codes и времена: [attempts.csv](../out/lab3-evidence/attempts.csv).

Сохранность Material System/PSO cache/Hot Reload/Inheritance/RenderingLab3/ordinary/instanced/HDR/PBR и DX12 validation подтверждается только фактическими логами итогового раздела. Общий production код не менялся; Vulkan smoke указывается отдельно. Git status и хеши файлов проверяются после всех запусков.

<!-- GENERATED RESULTS -->
## 10. Фактические результаты

Завершённых независимых запусков в summary: **24/24**. Число измеряемых кадров: **6480**. Все значения ниже в ms.

| Материалы | Instancing | Frame median | Frame p95 | Frame p99 | CPU Render median | CPU p95 | CPU p99 | Resolve median |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0 | 7.561100 | 8.937800 | 15.147200 | 6.816650 | 7.894700 | 8.319000 | 2.906100 |
| 1 | 1 | 7.112950 | 8.516500 | 17.548900 | 6.410400 | 7.267300 | 8.058500 | 2.950400 |
| 10 | 0 | 7.533050 | 9.521900 | 15.871700 | 6.555700 | 7.693200 | 8.870200 | 3.039100 |
| 10 | 1 | 7.341800 | 11.270500 | 14.655200 | 6.080050 | 6.905800 | 8.071600 | 3.117500 |
| 100 | 0 | 7.554450 | 9.094400 | 11.117900 | 6.812900 | 8.182200 | 9.449500 | 3.126750 |
| 100 | 1 | 7.282400 | 8.904300 | 19.291600 | 5.882600 | 7.022600 | 7.660000 | 2.997450 |
| 500 | 0 | 8.975550 | 13.319200 | 16.615800 | 8.167300 | 9.970800 | 11.194900 | 3.877200 |
| 500 | 1 | 7.417800 | 9.223600 | 10.728800 | 6.506500 | 8.188100 | 9.289000 | 3.286200 |

Интерпретация внутри фиксированного instancing (500 assets минус 1 asset; это разница текущих конфигураций):

- Instancing 0: Frame median Δ=+1.414450 ms; CPU Render Δ=+1.350650 ms; Material Resolve Δ=+0.971100 ms. Число mesh draws в этом сравнении неизменно.
- Instancing 1: Frame median Δ=+0.304850 ms; CPU Render Δ=+0.096100 ms; Material Resolve Δ=+0.335800 ms. Число mesh draws в этом сравнении неизменно.

Колебания трёх run-level значений видны в следующей таблице; доверительный интервал при N=3 не оценивается. Эквивалентные параметры материалов допускают одинаковую batching-группу при разных asset keys. Число PSO определяется shader/state, поэтому 500 material assets не означает 500 PSO. Эти данные не доказывают исторического ускорения.

### Каждый независимый запуск

| Run | Frame median / p95 / p99 | CPU Render median / p95 / p99 | Resolve median | Draws | GPU valid |
|---|---:|---:|---:|---:|---:|
| m1-i0-run1 | 7.561100 / 8.937800 / 13.404000 | 6.816650 / 7.894700 / 8.319000 | 3.020850 | 500 | 270/270 |
| m1-i0-run2 | 6.896850 / 7.124100 / 20.048000 | 3.072300 / 6.297800 / 7.565000 | 1.451350 | 500 | 270/270 |
| m1-i0-run3 | 7.767850 / 9.586200 / 15.147200 | 6.936450 / 8.121600 / 8.742000 | 2.906100 | 500 | 270/270 |
| m1-i1-run1 | 7.112950 / 8.516500 / 12.851600 | 6.422250 / 7.267300 / 8.058500 | 2.960450 | 1 | 270/270 |
| m1-i1-run2 | 6.864150 / 7.174900 / 17.548900 | 2.936500 / 6.062000 / 6.509100 | 1.478450 | 1 | 270/270 |
| m1-i1-run3 | 7.343500 / 10.494000 / 19.880100 | 6.410400 / 7.388400 / 8.948500 | 2.950400 | 1 | 270/270 |
| m10-i0-run1 | 7.533050 / 9.521900 / 20.762900 | 6.810850 / 8.239100 / 10.570900 | 3.107450 | 500 | 270/270 |
| m10-i0-run2 | 7.257400 / 8.500200 / 10.217200 | 6.555700 / 7.693200 / 8.870200 | 3.039100 | 500 | 270/270 |
| m10-i0-run3 | 8.430900 / 12.056100 / 15.871700 | 6.422550 / 7.393200 / 7.800900 | 2.956500 | 500 | 270/270 |
| m10-i1-run1 | 7.341800 / 10.431900 / 12.151300 | 5.890950 / 6.747300 / 7.402800 | 2.991650 | 1 | 270/270 |
| m10-i1-run2 | 8.071650 / 12.259600 / 14.655200 | 6.328600 / 6.905800 / 8.071600 | 3.231050 | 1 | 270/270 |
| m10-i1-run3 | 7.114900 / 11.270500 / 16.551200 | 6.080050 / 7.130000 / 9.362700 | 3.117500 | 1 | 270/270 |
| m100-i0-run1 | 9.521900 / 15.340700 / 23.096600 | 8.556450 / 11.536900 / 16.835100 | 4.018300 | 500 | 270/270 |
| m100-i0-run2 | 7.554450 / 9.094400 / 11.117900 | 6.812900 / 8.182200 / 9.449500 | 3.126750 | 500 | 270/270 |
| m100-i0-run3 | 7.330100 / 8.785100 / 10.385600 | 6.655600 / 7.847200 / 9.239200 | 3.061950 | 500 | 270/270 |
| m100-i1-run1 | 6.881250 / 8.493000 / 20.812100 | 5.882600 / 7.022600 / 7.660000 | 2.927100 | 1 | 270/270 |
| m100-i1-run2 | 7.294100 / 10.924500 / 13.301900 | 5.736350 / 6.994100 / 7.657100 | 2.997450 | 1 | 270/270 |
| m100-i1-run3 | 7.282400 / 8.904300 / 19.291600 | 6.604650 / 7.883100 / 9.030600 | 3.419100 | 1 | 270/270 |
| m500-i0-run1 | 8.975550 / 13.319200 / 16.615800 | 8.167300 / 9.970800 / 11.194900 | 3.877200 | 500 | 270/270 |
| m500-i0-run2 | 10.171400 / 14.225500 / 20.877700 | 8.498650 / 10.541300 / 16.327900 | 3.932900 | 500 | 270/270 |
| m500-i0-run3 | 7.942900 / 9.977700 / 11.724100 | 7.357400 / 9.264900 / 9.911200 | 3.483850 | 500 | 270/270 |
| m500-i1-run1 | 7.556300 / 9.223600 / 10.274000 | 6.895800 / 8.445000 / 9.507400 | 3.480050 | 1 | 270/270 |
| m500-i1-run2 | 6.915400 / 9.027700 / 10.728800 | 6.230500 / 8.188100 / 9.289000 | 3.203800 | 1 | 270/270 |
| m500-i1-run3 | 7.417800 / 11.693800 / 15.722000 | 6.506500 / 7.874600 / 8.360600 | 3.286200 | 1 | 270/270 |

Min/max всех метрик сохранены в summary.csv. Raw CSV содержат также warmup, edits и reload, исключённые из таблицы.

### PSO и startup

| Run | Warmup creations | Steady begin → end | Parameter edit | Shader reload | Hits begin → end | Misses begin → end | Setup ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| m1-i0-run1 | 12 | 12 → 12 | 12 | 24 | 14000 → 148500 | 12 → 12 | 1045.9 |
| m1-i0-run2 | 12 | 12 → 12 | 12 | 24 | 14000 → 148500 | 12 → 12 | 1026.02 |
| m1-i0-run3 | 12 | 12 → 12 | 12 | 24 | 14000 → 148500 | 12 → 12 | 863.401 |
| m1-i1-run1 | 12 | 12 → 12 | 12 | 24 | 28 → 297 | 12 → 12 | 937.097 |
| m1-i1-run2 | 12 | 12 → 12 | 12 | 24 | 28 → 297 | 12 → 12 | 839.65 |
| m1-i1-run3 | 12 | 12 → 12 | 12 | 24 | 28 → 297 | 12 → 12 | 985.529 |
| m10-i0-run1 | 12 | 12 → 12 | 12 | 24 | 14000 → 148500 | 12 → 12 | 1218.34 |
| m10-i0-run2 | 12 | 12 → 12 | 12 | 24 | 14000 → 148500 | 12 → 12 | 860.89 |
| m10-i0-run3 | 12 | 12 → 12 | 12 | 24 | 14000 → 148500 | 12 → 12 | 936.87 |
| m10-i1-run1 | 12 | 12 → 12 | 12 | 24 | 28 → 297 | 12 → 12 | 1346.38 |
| m10-i1-run2 | 12 | 12 → 12 | 12 | 24 | 28 → 297 | 12 → 12 | 1020.22 |
| m10-i1-run3 | 12 | 12 → 12 | 12 | 24 | 28 → 297 | 12 → 12 | 927.513 |
| m100-i0-run1 | 12 | 12 → 12 | 12 | 24 | 14000 → 148500 | 12 → 12 | 1428.39 |
| m100-i0-run2 | 12 | 12 → 12 | 12 | 24 | 14000 → 148500 | 12 → 12 | 859.264 |
| m100-i0-run3 | 12 | 12 → 12 | 12 | 24 | 14000 → 148500 | 12 → 12 | 1319.78 |
| m100-i1-run1 | 12 | 12 → 12 | 12 | 24 | 28 → 297 | 12 → 12 | 906.436 |
| m100-i1-run2 | 12 | 12 → 12 | 12 | 24 | 28 → 297 | 12 → 12 | 1066.86 |
| m100-i1-run3 | 12 | 12 → 12 | 12 | 24 | 28 → 297 | 12 → 12 | 848.246 |
| m500-i0-run1 | 12 | 12 → 12 | 12 | 24 | 14000 → 148500 | 12 → 12 | 3892.72 |
| m500-i0-run2 | 12 | 12 → 12 | 12 | 24 | 14000 → 148500 | 12 → 12 | 1054.3 |
| m500-i0-run3 | 12 | 12 → 12 | 12 | 24 | 14000 → 148500 | 12 → 12 | 1012.96 |
| m500-i1-run1 | 12 | 12 → 12 | 12 | 24 | 28 → 297 | 12 → 12 | 930.28 |
| m500-i1-run2 | 12 | 12 → 12 | 12 | 24 | 28 → 297 | 12 → 12 | 964.837 |
| m500-i1-run3 | 12 | 12 → 12 | 12 | 24 | 28 → 297 | 12 → 12 | 949.495 |

Setup охватывает window/device, CPU assets и создание ECS; PSO compilation происходит при GPU shader preparation в warmup. Setup не выдаётся за отдельное время PSO compilation. Material assets различны по key, но их параметры эквивалентны; эксперимент показывает стоимость количества asset identities при постоянных 500 instances. Нельзя экстраполировать его на 500 разных shader pipelines.

### Графики

![Frame Time](../out/lab3-evidence/frameMs.png)

![CPU Render Time](../out/lab3-evidence/cpuRenderMs.png)

![Material Resolution](../out/lab3-evidence/materialResolveMs.png)

### Capture/replay

- [current-1_frame101.rdc](../out/lab3-evidence/captures/current-1_frame101.rdc), 4281550 bytes, SHA-256 `9c6da75cab04655ea54dd26f6aeeca05f730f35ab7e1b6ccbdaae92e5520da16`.
- [current-500_frame101.rdc](../out/lab3-evidence/captures/current-500_frame101.rdc), 4283317 bytes, SHA-256 `eb36f258710beb8a2ea213e8ba2d333b11341a1d927af333fa1bf293783763ed`.
- `current-1_frame101.rdc`: OpenFile=<Result: 'Success'>, replay=<Result: 'Success'>, total draw actions=500, mesh event=560, PSO=ResourceId::4425, shaders={'ShaderStage.Vertex': 'ResourceId::1000000000000000994', 'ShaderStage.Pixel': 'ResourceId::1000000000000000995'}. Текстуры, output targets, rasterizer и depth/blend/root state: [replay.json](../out/lab3-evidence/captures/replay.json). ResourceId локальны для каждого capture; совпадение номера не доказывает общий GPU object между процессами.
- `current-500_frame101.rdc`: OpenFile=<Result: 'Success'>, replay=<Result: 'Success'>, total draw actions=500, mesh event=560, PSO=ResourceId::4425, shaders={'ShaderStage.Vertex': 'ResourceId::1000000000000001846', 'ShaderStage.Pixel': 'ResourceId::1000000000000001847'}. Текстуры, output targets, rasterizer и depth/blend/root state: [replay.json](../out/lab3-evidence/captures/replay.json). ResourceId локальны для каждого capture; совпадение номера не доказывает общий GPU object между процессами.

![current-1_frame101-pipeline](../out/lab3-evidence/captures/current-1_frame101-pipeline.png)

![current-500_frame101-pipeline](../out/lab3-evidence/captures/current-500_frame101-pipeline.png)

![current-1_frame101-events](../out/lab3-evidence/captures/current-1_frame101-events.png)

![current-500_frame101-events](../out/lab3-evidence/captures/current-500_frame101-events.png)

Viewport export после последнего draw: **побитно одинаковые PNG**, SHA-256 `914b299135040bc90ff03318fe05de097e0a50efa4269470032030c9bda1835f`, `914b299135040bc90ff03318fe05de097e0a50efa4269470032030c9bda1835f`. Эти exports — дополнительные изображения; RDC подтверждены отдельно.

![current-1_frame101-viewport](../out/lab3-evidence/captures/current-1_frame101-viewport.png)

Исторические material-before.rdc / material-after.rdc: **NOT CAPTURED**. Полученные current captures нельзя предъявлять как историческое before/after. Тайминги из captures исключены.

### Проверки сохранности

```text
{"ctest":0,"instancingDx12":0}
Internal ctest changing into directory: C:/Users/Тимофей/source/repos/WhispEngine/out/lab3-evidence/local-build
Test project C:/Users/Тимофей/source/repos/WhispEngine/out/lab3-evidence/local-build
      Start  36: PrefabInheritanceRegression
 1/10 Test  #36: PrefabInheritanceRegression .........   Passed    0.82 sec
      Start  37: PrefabInheritanceDx12Regression
 2/10 Test  #37: PrefabInheritanceDx12Regression .....   Passed   24.93 sec
      Start  88: GpuInstancingRegression
 3/10 Test  #88: GpuInstancingRegression .............   Passed    0.75 sec
      Start  90: MaterialSystemRegression
 4/10 Test  #90: MaterialSystemRegression ............   Passed    0.73 sec
      Start  91: MaterialSystemDx12Regression
 5/10 Test  #91: MaterialSystemDx12Regression ........   Passed   82.83 sec
      Start  96: PbrCoreRegression
 6/10 Test  #96: PbrCoreRegression ...................   Passed    0.53 sec
      Start  99: PbrCoreDx12Regression
 7/10 Test  #99: PbrCoreDx12Regression ...............   Passed   32.09 sec
      Start 106: HdrOutputDx12Regression
 8/10 Test #106: HdrOutputDx12Regression .............   Passed    9.90 sec
      Start 133: MaterialEditorUXRegression
 9/10 Test #133: MaterialEditorUXRegression ..........   Passed   57.39 sec
      Start 134: MaterialEditorUXRestartRegression
10/10 Test #134: MaterialEditorUXRestartRegression ...   Passed   11.58 sec

100% tests passed, 0 tests failed out of 10

Total Test time (real) = 221.57 sec
riant unavailable; normal fallback: instance variant still reads per-draw or incompatible constant buffer
[INFO] DX12 CreateShaderProgram: handle=3 name=textured
[INFO] DX12 DestroyShader: handle=3
[INFO] DX12 DestroyMesh: handle=1
[INFO] DX12 DestroyTexture: handle=1
[INFO] DX12 DestroyShader: handle=1
[INFO] DX12 adapter shutdown
DX12 identical GPU pixels, real 501 -> 1 draws, tint/rotation/scale, slot reuse, resize, growth and retirement passed
[INFO] ResourceManager: async shutdown complete
file unchanged: Assets\shaders\demo_surface.hlsl
381: source demo file unchanged: Assets\textures\demo_surface.png
382: source demo file unchanged: Assets\ui\demo_legend.ui.json
383: source demo file unchanged: Assets\ui\inheritance_legend.ui.json
384: source demo file unchanged: DemoActions.ps1
385: source demo file unchanged: README.md
386: source demo file unchanged: Recovery\MaterialSystem.json
387: source demo file unchanged: Recovery\demo_alternate.material.json
388: source demo file unchanged: Recovery\demo_base.material.json
389: source demo file unchanged: Recovery\demo_surface.hlsl.good
390: source demo file unchanged: Recovery\inheritance_child.material.json
391: source demo file unchanged: Recovery\inheritance_grandchild.material.json
392: source demo file unchanged: RenderingLab3.whispproject
393: source demo file unchanged: Scenes\Inheritance.json
394: source demo file unchanged: Scenes\MaterialSystem.json
395: source demo file unchanged: Verification\VerifyDemo.cpp
396: source demo file unchanged: Verification\VerifyDemo.py
RenderingLab3: 396 checks passed; artifacts=C:\Users\�������\source\repos\WhispEngine\out\lab3-evidence\local-build\renderinglab3-verification
Internal ctest changing into directory: C:/Users/Тимофей/source/repos/WhispEngine/out/lab3-evidence/local-build
Test project C:/Users/Тимофей/source/repos/WhispEngine/out/lab3-evidence/local-build
    Start 35: ViewmodelRenderingVulkanRegression
1/1 Test #35: ViewmodelRenderingVulkanRegression ...   Passed    1.24 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   1.25 sec
```

RenderingLab3 SHA-256 verification: **UNCHANGED**, все исходные файлы и набор путей сохранены.

396 проверок RenderingLab3 verifier пройдены; в verification.log явно записано DX12 validation errors = 0. Отдельно прошли 10/10 CTest, standalone instancing DX12 и 1/1 Vulkan smoke. Это correctness evidence, не performance measurements.

### Статус требований и рекомендации для защиты

| Требование | Фактический статус |
|---|---|
| Release DX12, три процесса на вариант, 30+270, raw и p95/p99 | DONE |
| Fixed 500 instances / 1920×1080 / separate instancing | DONE (CSV invariants) |
| Steady PSO reuse / constants without PSO / reload | DONE (summary assertions + raw counters) |
| DX12 validation в benchmark logs | 0 reported; debug layer отдельно в correctness |
| Genuine current-configuration RDCs | 2/2; replay status выше |
| Историческое timing before/after и два исторических capture | NOT MEASURED / NOT CAPTURED |
| Чистое driver PSO compilation time | NOT MEASURED |
| Изолирование Editor overhead | DONE: benchmark без Editor; Editor performance NOT MEASURED |

На защите показывать архитектуру и данные из MaterialPipeline/ExtraFeatures, затем raw → run-level summary → aggregate, а PSO counters объяснять отдельно. Не заявлять FPS gain. Историческое требование ТЗ остаётся открытым: нужно согласовать с преподавателем альтернативный эксперимент текущей системы либо отдельно восстановить совместимую legacy-сцену на исторических сборках. Проверки корректности не заменяют performance evidence.

Если автоматические current capture/screenshots отсутствуют, минимальное ручное действие: открыть RenderDoc, запустить runtime/MaterialPerformanceProbe.exe с arguments `1 0 ../captures/manual-excluded.csv capture` через Launch Application, working directory runtime; повторить с `500`. Приложение само вызывает TriggerCapture после прогрева. Открыть настоящий RDC, выбрать draw с 36 indices, сохранить Event Browser/Pipeline State. Не выполнять timing-замеры в этом запуске. Для исторического capture требуется сначала отдельный проверенный исторический executable.

### Git status

```text
M .gitignore
?? docs/RenderingLab3-Performance.md
?? tests/MaterialPerformanceProbe.cpp
?? tests/PrepareMaterialPerformance.py
?? tests/ReplayMaterialCaptures.py
?? tests/RunMaterialPerformance.ps1
?? tests/ScreenshotMaterialCaptures.py
?? tests/SummarizeMaterialPerformance.py
?? tests/WriteMaterialPerformanceReport.py
```
Ветка и HEAD сохранены; commit/merge/reset/clean не выполнялись. Новые tests — диагностические helpers; единственная правка существующего tracked файла — whitelist отчёта в .gitignore.


## 11. Историческое before/after — подтверждённый этап 2

Дополнение от 5 ноября 2026. Предыдущие разделы сохранены побайтно как отчёт первого этапа. Их статусы NOT MEASURED / NOT CAPTURED для исторической пары **заменяются результатами этого раздела**. Чистый причинный эффект только Material System по-прежнему не выделен.

### Baseline и неизменность исходников

Исторический baseline: `bbba558d8b39fffefc9b000762ec4a8f0cad1983`, tree `5bb475c8cc2eb4fba7bc58ff7da0ce5dc5eccfa0`, commit «optimization» от 10 октября 2026. Это непосредственный родитель `5240060eee628158378d8b5460ecd579f08cb017`, впервые добавившего расширенную Material System regression. В baseline уже существуют MaterialResource, legacy materialPath и prelinked ordinary/instanced shader PSOs; это не версия без материалов или без какого-либо reuse.

Current: `7d879e96324447968f0ff072a9e34228c326952a`, ветка rendering_lab. Baseline извлечён `git archive` в новую baseline-source, сборка baseline-build. Собраны настоящий WhispEngine.exe и отдельный diagnostic consumer того же Engine. Все исторические production-файлы совпадают с новым архивом данного commit; добавлены только harness и append-only CMake target. Не было backport текущего resolver/PSO cache/PBR в baseline. Git archive применяет CRLF attributes к некоторым vendor Visual Studio project files; они совпадают с архивом и не были изменены сборкой.

Для current выполнена новая compile/link общего harness против существующих Release Engine.lib/Tracy/dependencies из предыдущей изолированной сборки. Команды взяты через `ninja -t commands` только для чтения; все object/exe/pdb/manifest outputs перенаправлены в новый каталог. Старые build/results не пересобирались и не изменялись. Измеряется RenderSystem/adapter без Editor, а не frame loop редактора.

[История и commit diff](../out/lab3-historical-evidence/history.txt), [исходные SHA-256 baseline](../out/lab3-historical-evidence/baseline-source-hashes.json), [CMake diagnostic addition](../out/lab3-historical-evidence/baseline-cmake-append.txt), [current compile/link commands](../out/lab3-historical-evidence/current-commands.json).

### Hardware/software и общая сцена

Windows 11 build 26200; Intel Core i5-13500H; NVIDIA GeForce RTX 4060 Laptop GPU, vendor 4318/device 10400, dedicated VRAM 8343519232 bytes; Windows driver 32.0.15.8157. GPU audit воспроизводит одинаковый в обеих версиях выбор первого non-software DXGI adapter с FL12_0: ordinal 0 — NVIDIA. Исторический adapter не печатает имя GPU, поэтому это подтверждение selection policy и фактической enumeration, а не добавленная в production логировка. Current дополнительно печатает выбранную NVIDIA в каждом log.

MSVC x64 Release 19.44.35225.0, toolset directory 14.44.35207, Windows SDK 10.0.26100.0; CMake 3.31.6/Ninja; DX12/Vulkan/Tracy ON, /O2 /DNDEBUG -MD. Tracy 0.14.1 on-demand без клиента. RenderDoc 1.40 (`6ab25b2d01b44149baf4551158398f9357610a60`). Хеши executable, shaders и точные input files: [metadata](../out/lab3-historical-evidence/metadata.json), [runtime manifest](../out/lab3-historical-evidence/runtime-manifest.json), [GPU driver](../out/lab3-historical-evidence/gpu-driver.json).

Общий [scene manifest](../out/lab3-historical-evidence/scene.json): ровно 500 одинаковых validation_cube.obj, сетка 25×20, позиции `(i%25-12)*0.65, (i/25-9.5)*0.65, 20`, scale 0.5. Камера (0,0,0), yaw/pitch=0, FOV 1.04719755 rad, near/far 0.1/100. Viewport 1920×1080, clear black. Cube OBJ, физический white PNG 1×1 и legacy JSON material byte-identical в обеих runtime. Один активный материал, один mesh и один shader program; у GPU program несколько prelinked variants.

Материал имеет только совместимые name/shaderPath/texturePath/baseColor=[1,1,1,1], version=1. Современные v4/v5 inheritance/PBR assets не выдаются за поддерживаемые baseline. Белые texels и baseColor=1 позволяют одинаковый результат при разных color contracts: baseline UNORM sampling, current SRGB sampling. Shader/root layout намеренно оставлены собственными для каждого historical state; они различаются, что видно в replay.

Baseline unlit; HDR/AO/shadows/lighting этих стадий в нём нет. Current: toneMapping/HDR/AO/shadows/AA OFF, directional OFF, ambient white=1, Lambert. Нет физики, анимации, интерактивного ввода или Editor UI. Instancing OFF/ON измеряются раздельно: соответственно 500/1 mesh draws при 500 rendered instances. C++ harness создаёт отдельный World; scene.json — manifest, не формат для открытия в Editor.

Actual replay render targets: **1920×1080 R8G8B8A8_UNORM** у обеих версий; native window/backbuffer **1920×1061** у обеих. Это фиксированный offscreen viewport плюс Present окна. В raw baseline width/height отмечены как requested, поскольку старый adapter не имеет getter размеров; фактический размер подтверждён replay capture и реализацией BeginViewportRender.

### Методика

Три отдельных процесса на каждую версию и каждый instancing mode: **12 процессов**, по 300 frames (0–29 warmup, 30–299 measured), **3240 measured samples**. Порядок run 1/2/3 → instancing OFF/ON → before/after. Pilot — отдельные два процесса, сохранён в pilot/, в статистику не включён. Все 12 основных процессов завершились exit 0; 500 objects/instances и ожидаемые draw calls проверены во всех measured frames.

Frame Time — steady_clock вокруг PollEvents/BeginFrame/clear/BeginViewportRender/RenderSystem::Update/EndViewportRender/EndFrame/Present; CPU Render Time — steady_clock непосредственно вокруг Update. CSV serialization и чтение diagnostics исключены из этих интервалов. Startup/PSO preparation внутри первых 30 кадров исключены из steady-state, setupMs сохранён отдельно в logs. Pure PSO compilation time **NOT MEASURED**. GPU execution time **NOT MEASURED**: baseline не предоставляет сопоставимые GPU timestamps.

Внутри каждого процесса median — обычная медиана (среднее двух центральных значений при чётном N); p95/p99 — nearest rank, `sorted[ceil(p*N)-1]`; min/max сохранены в summary.csv. Все таймеры в **ms**. Правило сводки зафиксировано до обработки: медиана трёх run-level median/p95/p99, без pooling 810 кадров. Нет удаления выбросов или неудачных результатов. Все captures и API validation выполнялись после timing series, отдельно от замеров.

[Raw per-frame CSV/logs](../out/lab3-historical-evidence/raw/), [каждый процесс и min/max](../out/lab3-historical-evidence/summary.csv), [aggregate comparison](../out/lab3-historical-evidence/comparison.csv), [exit codes](../out/lab3-historical-evidence/attempts.csv).

### Сводные результаты, ms

| State | Instancing | Frame median | Frame p95 | Frame p99 | CPU Render median | CPU p95 | CPU p99 |
|---|---|---:|---:|---:|---:|---:|---:|
| before | OFF | 6.923900 | 7.051900 | 7.129300 | 1.095000 | 1.220400 | 1.314900 |
| before | ON | 6.922250 | 7.054500 | 7.197100 | 1.039550 | 1.144900 | 1.288000 |
| after | OFF | 7.285600 | 10.806100 | 16.341100 | 3.392000 | 7.290000 | 8.114300 |
| after | ON | 3.262900 | 3.938900 | 10.288900 | 2.884900 | 3.219500 | 3.436700 |

### Три независимых запуска каждого варианта, ms

| State / instancing / run | Frame median / p95 / p99 | CPU Render median / p95 / p99 | Frame min / max | Draws |
|---|---:|---:|---:|---:|
| after / 0 / 1 | 7.285600 / 10.806100 / 16.341100 | 3.392000 / 7.290000 / 8.114300 | 4.174300 / 18.087300 | 500 |
| after / 0 / 2 | 6.900500 / 7.149200 / 11.275700 | 3.080500 / 3.417700 / 3.751200 | 3.205900 / 18.379700 | 500 |
| after / 0 / 3 | 7.714450 / 11.725000 / 17.124400 | 5.968050 / 7.531000 / 8.887800 | 4.322300 / 18.390000 | 500 |
| after / 1 / 1 | 3.262850 / 3.682000 / 6.796400 | 2.884900 / 3.219500 / 3.322400 | 2.976300 / 10.348900 | 1 |
| after / 1 / 2 | 3.262900 / 3.938900 / 10.288900 | 2.883750 / 3.204100 / 3.436700 | 2.957000 / 12.599800 | 1 |
| after / 1 / 3 | 7.936600 / 13.251600 / 20.454200 | 5.578500 / 7.055200 / 8.120900 | 4.366700 / 21.431100 | 1 |
| before / 0 / 1 | 6.927200 / 7.034600 / 7.129300 | 1.095000 / 1.220400 / 1.314900 | 6.714300 / 7.762100 | 500 |
| before / 0 / 2 | 6.923900 / 7.051900 / 7.110500 | 1.094000 / 1.204800 / 1.282200 | 6.589700 / 7.205700 | 500 |
| before / 0 / 3 | 6.911900 / 7.134300 / 9.213100 | 2.115650 / 2.498000 / 2.888200 | 2.874400 / 10.901000 | 500 |
| before / 1 / 1 | 6.922250 / 7.054500 / 7.166800 | 1.028700 / 1.144900 / 1.238600 | 5.648200 / 8.314900 | 1 |
| before / 1 / 2 | 6.924850 / 7.047100 / 7.197100 | 1.039550 / 1.133400 / 1.288000 | 6.443900 / 7.618600 | 1 |
| before / 1 / 3 | 6.915050 / 7.200100 / 8.960100 | 1.109550 / 2.264100 / 3.240300 | 2.874700 / 10.603500 | 1 |

![Historical Frame Time](../out/lab3-historical-evidence/frameMs.png)

![Historical CPU Render Time](../out/lab3-historical-evidence/cpuRenderMs.png)

### Интерпретация и границы сопоставимости

CPU Render median current − baseline: **+2.297000 ms OFF**, **+1.845350 ms ON**. Frame median: **+0.361700 ms OFF**, **−3.659350 ms ON**. Это наблюдаемые различия целых версий. Нельзя приписать ни увеличение CPU стоимости, ни уменьшение Frame Time при ON только Material System или PSO Cache.

Коммит 5240060 объединяет Material System с PBR, lighting, shadows, IBL и другими изменениями; до HEAD менялись ECS, ResourceManager, RenderSystem, DX12 submission, frame synchronization и shaders. Отключение новых визуальных features не возвращает их инфраструктурную CPU стоимость к baseline. Старый код уже переиспользует подготовленные shader PSOs. На GPU исполняется различный bytecode/root contract при одинаковой геометрии и итоговых pixels.

Особенно заметна нестационарность: after/ON run 1/2 Frame median ~3.2629 ms, run 3 — 7.936600 ms; CPU run 3 также выше. Before/OFF CPU median run 3 = 2.115650 ms против 1.095000/1.094000. Все эти samples и процессы оставлены. CPU affinity, P/E-core placement, частоты, температура, питание и фоновые приложения не фиксировались. Это не установленная причина перепадов, а ограничение эксперимента. N=3 не позволяет оценить надёжный confidence interval.

Обе версии вызывают Present(0,0), но Frame Time содержит Present/fence waits и зависим от compositor/driver/presentation path. Поэтому он не равен чистому GPU throughput. Изменение ON Frame median не выдаётся за FPS gain Material System. Новая after-серия снята на общей legacy-сцене и **не объединяется** с прежними 24 запусками (там другая окраска, material keys и texture fallback).

### Настоящие исторические RenderDoc artifacts

Официальный renderdoccmd launch + RENDERDOC_GetAPI/TriggerCapture на стабильном frame 100 (в RDC Frame #101), ordinary mode OFF. After — настоящий current renderer, before — настоящий Engine baseline. Исходные RenderingLab3 и прежние RDC/CSV не использовались для записи новых outputs. Тайминги captures сохранены как excluded-*.csv и не включены в summary.

- [material-after.rdc](../out/lab3-historical-evidence/captures/material-after.rdc), 4303537 bytes, SHA-256 `c1429c81ac8ec260704e97401ae6b0a72c442a8088cd2cecb790431bfd6c6605`.
- [material-before.rdc](../out/lab3-historical-evidence/captures/material-before.rdc), 3832936 bytes, SHA-256 `656cc2ee01b06a79b86fc7cb5eb17cb0bd6e885d7e7d8b6a8952cb7c7e37327f`.
- `material-after.rdc`: OpenFile/replay **Success**, draw actions **500**, mesh event **560** (36 indices, 1 instance), PSO `ResourceId::4425`. ResourceId имеет смысл только внутри своего capture.
- `material-before.rdc`: OpenFile/replay **Success**, draw actions **500**, mesh event **524** (36 indices, 1 instance), PSO `ResourceId::4410`. ResourceId имеет смысл только внутри своего capture.

[Полный replay: shaders bytecode hashes, bound textures, root signature, rasterizer, output/depth state](../out/lab3-historical-evidence/captures/replay.json). Legacy shader hashes отличаются от current; это ожидаемое различие исторических renderer states, не подмена shaders.

Экспорт viewport после последнего draw: **PNG побитно совпадают**, SHA-256 `4d828cf065171227e78099ed6cca64d84a13377389aee6a812057bfbff856fd5`. Геометрия, visibility и итоговый white-on-black результат сохранены. PNG — дополнительные изображения, а не замена RDC.

![material-before-events](../out/lab3-historical-evidence/captures/material-before-events.png)

![material-before-pipeline](../out/lab3-historical-evidence/captures/material-before-pipeline.png)

![material-after-events](../out/lab3-historical-evidence/captures/material-after-events.png)

![material-after-pipeline](../out/lab3-historical-evidence/captures/material-after-pipeline.png)

![Одинаковый viewport](../out/lab3-historical-evidence/captures/material-before-viewport.png)

Дополнительный baseline capture с `--opt-api-validation`: OpenFile/replay Success, 500 draws, **0 error messages**, полный список debug messages пуст. [Validation JSON](../out/lab3-historical-evidence/validation/validation.json). Это проверка captured stable frame, не validation instrumentation всех performance processes. Счётчик старого adapter отсутствует; его отсутствие не заменено выдуманным нулём в raw CSV.

### Что теперь закрыто

| Требование evidence | Статус |
|---|---|
| Реальная историческая Release x64 DX12 сборка до expanded Material System | DONE: bbba558d, без изменения renderer |
| Общая legacy scene, camera, 1920×1080, 500 instances | DONE: assets hashes, raw invariants, replay RT dimensions, equal PNG |
| N≥3, 30+270, Frame/CPU Render median/p95/p99, raw и отдельные runs | DONE для обеих версий и ON/OFF |
| Исторический material-before.rdc и current material-after.rdc | DONE: оба настоящие, replay Success |
| Draw event, Pipeline State, shaders/textures, screenshots, SHA-256 | DONE |
| Чистый причинный эффект только Material System | NOT ISOLATED: другие изменения между commit |
| Историческая поддержка современных v4/v5 material assets | NOT SUPPORTED / NOT CLAIMED |
| Pure GPU timing / чистое PSO compile time | NOT MEASURED |
| Полнота blend/depth overrides и остальные функциональные требования | Не изменялась этой задачей; ограничения этапа 1 сохраняются |

Таким образом, исторические timing и DX12 captures как артефакты ТЗ получены. Это не объявление полного зачёта ЛР: оценку и допустимость выбранной общей сцены определяет преподаватель. На защите показывать commit provenance → matching assets/viewport → три отдельных runs → replay/shaders → границы причинной интерпретации.

### Команды воспроизведения

Сохранённый baseline-build можно повторить из x64 Visual Studio Developer shell; новые timing/capture outputs не перезаписываются Run.ps1. Для повторения серии сначала подготовить новую отдельную папку/runtime и скорректировать paths скриптов; существующие evidence не удалять. prepare.py требует отсутствующий baseline-source и не является командой очистки.

```powershell
cmd /c out\lab3-historical-evidence\build-baseline.cmd
cmd /c out\lab3-historical-evidence\build-current.cmd
# В новой подготовленной evidence папке, с пустым raw/:
.\out\lab3-historical-evidence\Run.ps1
$env:PYTHONPATH="$PWD/out/lab3-evidence/python-packages"
python out/lab3-historical-evidence/Summarize.py
.\out\lab3-historical-evidence\Run.ps1 -Capture
$env:WHISP_MATERIAL_EVIDENCE="$PWD/out/lab3-historical-evidence"
& "C:/Program Files/RenderDoc/qrenderdoc.exe" --python "$PWD/out/lab3-historical-evidence/Replay.py"
& "C:/Program Files/RenderDoc/qrenderdoc.exe" --python "$PWD/out/lab3-historical-evidence/Screenshots.py"
```

Использованный Python: `C:/Users/Тимофей/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`; alias python в Windows может быть недоступен. Все CMake paths, compiler flags и link libraries сохранены в build-baseline.cmd/current-commands.json. Первая compile current harness завершилась до запуска из-за отсутствия нового object-подкаталога; log сохранён как current-compile-attempt1.log. После создания каталога сборка прошла. Производственные fixes для старой сборки не потребовались.

### Сохранность и ручные действия

До дополнения отчёта проверены SHA-256 **12700 существовавших файлов**: изменений нет; новых файлов внутри старого out/lab3-evidence нет. Исторические production sources совпадают с Git archive. Проверка: [preservation-before-report.json](../out/lab3-historical-evidence/preservation-before-report.json). Единственное разрешённое изменение прежнего файла этим этапом — append этого раздела; старая часть сохранена в RenderingLab3-Performance.before.md. Итоговая проверка: [preservation-final.json](../out/lab3-historical-evidence/preservation-final.json).

Ручных действий для замеров, captures, replay и screenshots **не требуется**. Для защиты остаётся корректно объяснить, что это историческое сравнение версий, а не изолированная оценка только Material System. Ветка/HEAD сохранены; checkout/switch/reset/clean, commit и merge не выполнялись. Existing dirty/untracked files сохранены.

Git status в начале и перед append одинаков; перечисленные ниже изменения уже существовали до нового этапа:

```text
 M .gitignore
?? docs/RenderingLab3-Performance.md
?? tests/MaterialPerformanceProbe.cpp
?? tests/PrepareMaterialPerformance.py
?? tests/ReplayMaterialCaptures.py
?? tests/RunMaterialPerformance.ps1
?? tests/ScreenshotMaterialCaptures.py
?? tests/SummarizeMaterialPerformance.py
?? tests/WriteMaterialPerformanceReport.py
```
