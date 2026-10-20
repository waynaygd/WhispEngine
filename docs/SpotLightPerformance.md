# Spot Light: устранение критической задержки редактора

## Причина

Проверена `D:/WhispEngineProjects/ProjectA/Scenes/Main.json`: `Untitled.json` в этом проекте пуст. Город — один GLB с 134 238 vertices, 278 901 indices (92 967 triangles), 111 submeshes.

`EditorLayer::CreateLight` сразу выделяет новый источник. При выбранном Spot `DrawLightGizmos` рисует две окружности по 48 сегментов и восемь образующих конуса. Каждый из 104 отрезков разбивается на четыре части. Для определения сплошной/пунктирной линии выполняется около 416 occlusion rays плюс проверки иконки/handles.

Раньше каждый луч вызывал `HitDistance`, который после общей AABB-проверки искал ближайшее пересечение полным перебором triangles. Для городской модели потенциально получалось около 38,7 млн triangle tests на один кадр. GPU instancing не ускоряет этот CPU-перебор. Расчёт выполнялся и для выключенного, но выделенного источника. При снятии выделения окружности не рисовались — это объясняет восстановление скорости без изменения освещения.

Нативные измерения подтвердили 486–506 мс в `lightGizmoMs`. Переключение `castShadows` не устраняло задержку, выключенный выделенный Spot оставался медленным, а снятие выделения уменьшало `lightGizmoMs` до 1,62 мс. Это CPU-регрессия визуализации инструмента редактора.

## Исправление

Для occlusion gizmo используется точное локальное дерево AABB с median split, листьями до восьми triangles и ранним выходом при первом действительном пересечении. Это не приблизительная проверка по bounding box: конечное решение по-прежнему принимает ray–triangle test.

Дерево строится один раз на CPU mesh resource и переиспользуется всеми его экземплярами. Кэш проверяет identity владельца и `Resource::GetVersion`; при reload перестраивается. Положение/вращение/масштаб экземпляра учитываются обратным преобразованием каждого луча. Локальное направление не нормализуется, поэтому предельная дистанция остаётся в мировых единицах, включая неравномерный масштаб. Истёкшие resource entries очищаются. GPU-ресурсы дереву не нужны.

Сплошные и пунктирные линии, X-Ray, handles, выбор источника и параметры конуса сохранены. `HitDistance`, используемый Viewport Picking и placement, не переписан. Lighting/shadow shaders, Job System, сцены и проекты не изменены.

## Метод измерения

- DX12 Release, NVIDIA GeForce RTX 4060 Laptop GPU, debug layer включён.
- Исходная сцена ProjectA, камера `(0, 1.7, 0)`, yaw/pitch `0`, FOV 60°, near/far `0.1/2000`, render viewport `1280×720`.
- Созданный Spot имеет исходные defaults: intensity `2`, range `5`, attenuation `0.1`, inner/outer half-angle `20°/30°`, направление `+Z`, позиция `(0, 1.7, 3)`.
- Исходные Directional Light, Fog, AA и тени `2048²` сохранены. GPU instancing включён. Во всех состояниях город даёт 111 main и 111 shadow draws.
- Нативный редактор: 24 прогревочных кадра; для каждой фазы два переходных и восемь измеряемых кадров. Управляемый runner вызывает `Application::Run` на один кадр, включая его обслуживание preferences. Таблицы — медианы коротких серий, не длительный benchmark.
- `CPU active` — wall time такого кадра за вычетом измеренных CPU-интервалов явного ожидания fence и вызова swapchain Present. Это не GPU time. `FPS` рассчитан по полной измеренной wall duration.
- GPU frame/shadow/main/output взяты из существующих DX12 timestamp queries; light culling и Fog имеют собственные GPU timestamps. Forward lighting совмещён с геометрией: `mainMs` включает shading, фон и Fog, а не изолированную стоимость одной функции освещения.
- Добавлены только CPU-интервалы begin-frame slot fence, swapchain Present и submitted-frame fence. Новые GPU queries не потребовались.

## Реальные результаты

| Конфигурация | CPU active, мс | GPU frame, мс | Wall frame, мс | FPS |
|---|---:|---:|---:|---:|
| Без Spot, до исправления | 191,84 | 10,84 | 203,19 | 4,92 |
| Spot без локальных теней, выбран, до | 712,92 | 19,59 | 732,59 | 1,37 |
| Spot `castShadows=true`, выбран, до | 685,61 | 20,34 | 726,25 | 1,38 |
| Spot `castShadows=true`, выбран, после | 208,13 | 10,88 | 219,72 | 4,55 |
| Spot выключен, Entity остаётся выбранной, до | 675,28 | 28,82 | 695,70 | 1,44 |
| Spot выключен, Entity остаётся выбранной, после | 206,97 | 8,42 | 215,62 | 4,64 |

Повторная фаза без Spot после исправления: CPU active 211,64 мс, GPU 20,29 мс, wall 229,90 мс, 4,35 FPS. Таким образом, выбранный Spot после исправления вернулся к измеренному уровню той же сцены без Spot. Короткие GPU-серии заметно колебались; весь выигрыш GPU-времени не приписывается этому CPU-исправлению.

| Этап, выбранный Spot с `castShadows=true` | До, мс | После, мс |
|---|---:|---:|
| CPU Light Gizmo | 486,62 | 3,67 |
| CPU UI всего | 490,53 | 8,16 |
| CPU material resolve | 180,45 | 190,57 |
| CPU swapchain Present | 0,36 | 0,39 |
| CPU submitted-frame fence wait | 21,08 | 11,50 |
| GPU shadow | 4,90 | 2,59 |
| GPU main | 10,26 | 4,50 |
| GPU light cull | 0,069 | 0,035 |
| GPU Fog | 1,37 | 0,48 |

Gizmo ускорен примерно в **133 раза**. Стабильные draw counts/resolution, сохранение задержки при выключенном Spot и непосредственный CPU timing исключают создание дополнительных Spot shadow passes как причину обнаруженной регрессии. Shadow resources и PSO уже кэшировались; после прогрева повторных allocation/compile сообщений в логах фаз нет. Directional caster submission пока грубый (111 submeshes); добавление Spot его не увеличивает, и в рамках этого исправления culling не переписывался.

**Ограничение абсолютных значений:** исходные 110–130 FPS в инструментальном запуске не воспроизвелись. Даже без Spot наблюдается отдельный большой CPU-расход material/resource path resolution. Он остался виден в отчёте и не исправлялся как несвязанный bottleneck. Таблица не является обещанием абсолютного FPS на пользовательском запуске. Подтверждённый результат — устранение дополнительных примерно 483 мс в выбранном Spot gizmo и возврат к измеренной baseline этой сцены.

**О тенях Spot:** текущий renderer поддерживает shadow map первичного Directional Light, но не отдельные shadow maps Point/Spot. Spot `castShadows` не передаётся в local-light shadow pipeline; такого pipeline сейчас нет. Во всех приведённых строках существующие Directional shadows включены. Поддержка локальных теней не удалялась и не добавлялась.

## Проверки

- Собраны `WhispEngine` и диагностическая цель в `out/build/shadows-dx12` и `out/build/x64-release`.
- `SpotLightRegression`: 8 580 сравнений с исходным brute-force triangle oracle, границы дистанции, обратное направление, ненормализованный локальный луч, отсечение без triangle tests, перестроение геометрии и empty mesh.
- Прошли `LightSystemRegression`, `LightSystemDx12Regression`, `ShadowMappingDx12Regression`, `FogDx12Regression`: Point/Spot/Directional, cone falloff, instancing, тени, обычный и volumetric Fog.
- На реальной ProjectA прошли: одинаковое изображение после resource-version rebuild; Picking; одинаковое изображение при переключении instancing; Play/Pause/Stop с точным восстановлением authored World; F11; DX12 Validation Errors = 0.
- Визуально проверен F11 editor capture: город, освещение, Fog, конус, handles и пунктир на перекрытых участках видны.
- Descriptor, Main.json, GLB и его существующий mesh cache побайтово совпали с исходным snapshot. Изменения света оставались только в памяти тестового редактора. Пользовательские near/far и незакоммиченные файлы сохранены.

Артефакты относительно `out/build/shadows-dx12/diagnostics/`:

- `spot-editor-before.json`, `spot-editor-after.json` — все фазы и исходные медианы.
- `spot-before.json` — дополнительная изоляция RenderSystem без editor gizmo; shadow/main/light-cull timings.
- `spot-qa.log`, `spot-qa.png`, `spot-qa-scene.png` — native QA и изображения.
- `../spot-checks.log` — пять целевых CTest-проверок.

## Повторный запуск

Из build directory:

```powershell
./SpotLightRegression.exe
./SpotLightRegression.exe --editor 'D:/WhispEngineProjects/ProjectA/ProjectA.whispproject' 'diagnostics/spot-editor.json'
./SpotLightRegression.exe --editor 'D:/WhispEngineProjects/ProjectA/ProjectA.whispproject' 'diagnostics/spot-qa' --qa
```

`--editor` загружает сцену, меняет только in-memory World и сохраняет прежние editor preferences после завершения. Как и обычные загрузчики движка, resource load может создать/обновить бинарный asset cache, если его нет или он устарел; для замеров на read-only источнике следует использовать копию проекта. В использованном ProjectA кэш был актуален, его байты не изменились.

## Изменённые файлы

- `engine/editor/LightGizmos.cpp`: подключение кэшируемого точного occlusion.
- `engine/editor/MeshOcclusion.h/.cpp`: локальное дерево triangles для editor overlay.
- `engine/render/backends/dx12/Dx12RenderAdapter.h/.cpp`: разделение CPU Present/fence wait timings.
- `tests/SpotLightRegression.cpp`: целевая регрессия и короткая диагностика указанной сцены.
- `CMakeLists.txt`, `engine/CMakeLists.txt`: сборка и CTest.
- Этот отчёт. ЛР №3, ResourceManager, RenderSystem lighting, shaders, scene/project formats не менялись.
