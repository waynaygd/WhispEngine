# WhispEngine — базовая система освещения

Проверка 10 октября 2026, Windows, MSVC x64 Release, DX12, Tracy 0.14.1. Ветка `job_system`; первоначальный `git status --short` был пустым. Предыдущие этапы находятся в HEAD и сохранены. Commit, push, merge, PR и смена ветки не выполнялись. JobSystem, WhispPhysics, Fixed Timestep и RenderInterpolation не изменены.

## 1. Исходное состояние и аудит

Изучены Physics Stress/Stability/Fixed Timestep, Render Interpolation и GPU Instancing reports и соответствующие реализации. `MeshVertex` уже содержал position[3], normal[3], uv[2], stride 32 байта. DX12 копировал их в VB и имел POSITION/NORMAL/TEXCOORD в input layout, но `textured.hlsl` игнорировал normal. Обычный VS читал column-major MVP и tint из b0; instanced VS — подготовленные MVP columns и индивидуальный tint из VB slot 1. PS возвращал texture.rgb × tint.rgb, alpha=1. Model присутствовал в instance payload, но не использовался шейдером.

OBJ/glTF загружает Assimp с Triangulate, GenSmoothNormals, JoinIdenticalVertices, ImproveCacheLocality и FlipUVs. Имеющиеся нормали копируются, отсутствующие Assimp обычно генерирует. До этого этапа fallback MeshVertex выдумывал normal=(0,0,1), а обход `scene->mMeshes` игнорировал transforms узлов glTF. Это давало неверную геометрию моделей с node transforms.

Сейчас добавлен Assimp PreTransformVertices: позиции и inverse-transpose нормали запекаются в пространство загруженного статического mesh. Проверена фактическая реализация Assimp в `PretransformVertices.cpp`: используется `aiMatrix3x3(mat).Inverse().Transpose()` с нормализацией. Версия `.wmesh` повышена с 2 до 3; прежний кеш не скрывает новые правила импорта. Binary layout mesh не менялся. ResourceManager и асинхронный протокол не переписаны.

## 2. Архитектура и источник истины

Освещение — общая **конфигурация render runtime** `LightingSettings`, владелец — Application. RenderSystem получает const pointer на эту конфигурацию и не хранит её изменяемую копию. Его собственный default используется только при автономном создании RenderSystem без Application, например в тестах. GPU constants — производный snapshot, а не дополнительный редактируемый источник истины.

Такой вариант выбран вместо отдельной ECS entity: текущий World::Clear удаляет scene entities, SceneSerializer сохраняет набор объектов, а глобальных scene render settings пока нет. Солнечный свет этого этапа должен сохраняться при Clear/Restart и пересоздании render runtime, подобно переключателю instancing. Создавать скрытую entity и независимо дублировать её настройки в редакторе было бы хуже. Структура DirectionalLight выделена отдельно; позднее Gather может разрешать ECS light components в тот же API передачи света. На этом этапе поддерживается ровно один directional light, без произвольного выбора между несколькими entities.

## 3. ECS → RenderSystem → DX12

World перечисляет живые Transform + MeshRenderer. Gather разрешает готовые GPU handles через прежний ResourceManager, material/texture/shader overrides и tint, получает RenderPose через прежний RenderInterpolation::Resolve и копирует snapshot. Prepare строит model и MVP из этой позы, затем normal matrix из **этого же model**. BatchBuild по-прежнему объединяет соседние packets с одинаковыми mesh/shader/texture handles. Tint и normal matrix индивидуальны и не разрывают совместимый batch.

Перед Gather RenderSystem один раз преобразует текущие LightingSettings в LightingConstants и передаёт их через API-независимый IRenderAdapter::SetLighting. Обычный Submit дополнительно вызывает SetNormalMatrix; instanced Submit передаёт массив расширенных RenderInstanceData. Backend не читает ECS и не разрешает interpolation poses. Transform, physics clock и история интерполяции не меняются.

## 4. Directional и Ambient

- DirectionalLight: world-space direction, RGB color, intensity, enabled. Direction означает **направление распространения лучей**. Defaults: (-0.4,-1,0.3), (1,0.95,0.85), intensity=0.85, enabled=true.
- LightingSettings: общий enabled=true, DirectionalLight, ambientColor=(1,1,1), ambientIntensity=0.25.
- LightingConstants: три float4, 48 байт: direction + global enabled, directional color + effective intensity, ambient color + intensity.

Direction нормализуется при подготовке constants, длина считается в double. Нулевой, слишком малый или non-finite вектор заменяется на (0,-1,0). Выключенный directional даёт intensity=0, не отключая Ambient. Отрицательные/non-finite цвета и интенсивности ограничиваются до 0. Выключение общего Lighting возвращает прежний unlit цвет. Ambient-only, directional-only и полностью нулевое освещение имеют определённый результат; при нулевых обоих вкладах mesh с валидными нормалями чёрный.

## 5. Преобразование нормалей

`BuildNormalMatrix` вычисляет inverse-transpose верхнего model3×3 через cofactor/determinant в double и сохраняет **три padded rows** в 12 floats. Translation не участвует. Этот метод учитывает rotation, non-uniform и отрицательный scale. Передаются rows, поэтому HLSL использует три dot(row, normal), без неоднозначности packing float3x3. Singular/non-finite матрица даёт нули и unlit fallback. Сильно экстремальные normal magnitudes тоже безопасно дают fallback, вместо NaN или бесконечности.

VS нормализует преобразованный normal с проверкой длины; PS повторно нормализует после интерполяции. Нормали нулевой длины дают unlit fallback. У MeshVertex default normal теперь (0,0,0), поэтому mesh без данных не получает искусственное освещение +Z. Debug primitives используют прежний unlit shader.

CPU regression независимо проверяет ортогональность normal к двум преобразованным tangents при rotation и неравномерном/отрицательном scale. glTF fixture дополнительно проверяет позиции и normals после импортированного node transform: translation=(2,3,4), Ry(0.4), scale=(2,0.5,1.5).

## 6. Lambert и color space

В PS:

```hlsl
diffuse = max(dot(normalize(worldNormal), -direction), 0.0);
lighting = ambientColor * ambientIntensity
         + directionalColor * directionalIntensity * diffuse;
finalRGB = textureRGB * tintRGB * lighting;
```

Минус перед direction переводит направление лучей в вектор от поверхности к свету. DX12 texture SRV, swapchain и viewport используют R8G8B8A8_UNORM, без существующего sRGB decode/encode. Поэтому gamma correction и tone mapping не добавлены; результат остаётся в прежнем числовом цветовом пространстве, UNORM ограничивает выход до [0,1]. Это базовое визуальное освещение, не физически корректный linear-light pipeline.

## 7. Обычный и instanced HLSL / PSO

Обычный `textured.hlsl::VSMain` сохраняет b0 MVP/tint, читает normal rows из b2 и передаёт worldNormal в TEXCOORD1. `VSInstancedMain` читает normal rows из INSTANCE_NORMAL0/1/2. Оба используют один PSMain. Структура RenderInstanceData расширена 144→192 байта: model offset 0, MVP 64, tint 128, normal rows 144/160/176. Size/offset проверены static_assert; существующий prefix layout V1 сохранён для старого unlit instance shader.

Root signature mesh PSO расширена: b1 — 12 root constants, visibility PIXEL; b2 — 12 root constants, visibility VERTEX. Всего 27 DWORD root cost. b0 и texture t0/s0 сохранены. Параметры света и обычные normal rows копируются непосредственно в command stream, а не в общий изменяемый upload buffer. Каждый draw получает свои values, включая изменение света между draws.

Reflection instanced shader по-прежнему отвергает per-draw b0/b2. Разрешён только общий LightingCB b1, space 0, size=48. Ошибка optional instanced variant сохраняет обычный PSO; caller выполняет прежний fallback. Untagged shader не объявляет instancing. Старый shader сохранён целиком как `textured_unlit.hlsl`; `triangle.hlsl` не менялся. MaterialShaderSources копирует оба HLSL source assets независимо от backend, в том числе при incremental edits без перелинковки. Vulkan-only CPU recorder tests используют эти файлы как данные, не как Vulkan GPU shaders.

Instance upload pages, allocation/growth, frame slots, fences и delayed resource retirement не изменены. Новый stride используется через sizeof(RenderInstanceData) для capacity, адресов, VB views и upload bytes. Общая normal matrix для 500 кубов не применяется. Root constants не расходуют CB pool и не создают отдельный ресурс на объект.

## 8. Texture, tint и material

Прежние material priorities сохранены: explicit MaterialComponent texture/shader overrides MeshRenderer; material resource заполняет пустые paths и умножает baseColor на индивидуальный tint. PS сохраняет texture sampling, UV, индивидуальный tint и прежнюю alpha=1. Texture/mesh/shader handles остаются batch key. Blend/depth/rasterizer всё ещё фиксированы backend.

Импорт хранит submesh material metadata, но прежний DrawMesh рисует весь index buffer с одним эффективным material/texture на ECS объект. Автоматические отдельные submesh materials, skinning и анимации glTF не реализованы этим этапом. Для модели в редакторе используется существующий MaterialComponent/материал, а не новое ограничение на кубы.

## 9. ImGui и lifecycle

Добавлена компактная отдельная панель Lighting с общим Lighting enabled, Directional Enabled/Direction/Color/Intensity и Ambient Color/Intensity. Изменения применяются в следующем render submission того же frame, без компиляции shader. Подпись объясняет world-space rays и downward fallback нулевого вектора.

Настройки хранятся весь срок жизни Application: Play/Stop/Resume, stress Create/Clear/Restart, Load Scene и пересоздание RenderSystem их не сбрасывают. Clear удаляет объекты, но не солнце редактора. После нового запуска приложения возвращаются defaults. Scene JSON и app.json **не сохраняют lighting на диск**: это выбранная семантика конфигурации runtime, а не обещание scene serialization. Автоматические lifecycle runs используют production callbacks; ручные нажатия каждого ImGui control не автоматизированы.

## 10. Визуальное сравнение

500 кубов + пол используют прежний Physics Stress Test и общий textured shader. Разные face normals и вращения меняют Lambert; Ambient оставляет тёмные стороны различимыми. Общий mesh/shader/texture сохраняет один batch, индивидуальный tint/scale/rotation не теряется.

GPU readback проверяет реальную `african_head.obj` (1339 vertices, 7476 indices) с её DDS-текстурой и glTF `lighting_sphere.gltf` (151 импортированных vertices, 672 indices). Последняя — самостоятельная sphere fixture со встроенным buffer и node transform, доступная также через Asset Browser. Lit/unlit изображения головы сохранены локально в `out/diagnostics/lighting/lighting-head-lit.png` и `lighting-head-unlit.png`; изображения визуально просмотрены.

Имеющийся `caveman.gltf` ссылается на отсутствующий `scene.bin`; он не использован как доказательство работоспособности glTF. Это ограничение исходного asset bundle, а не замаскированный успешный импорт fallback mesh.

## 11. DX12 GPU readback

`LightingRegression --dx12` создаёт настоящий DX12 device/window/PSOs и читает backbuffer через существующий ReadBackFramePixels. Для плоской геометрии проверяются ожидаемые RGB в центральном pixel при цветной texture и tint, tolerance 2 UNORM steps. Проверены несколько направлений, ambient-only, directional-only, нулевая интенсивность, directional enabled=false, общий unlit, rotation, non-uniform scale, oblique normal, singular normal matrix и zero normal data. Обычный/instanced readback во всех парах совпадает **побитово по всему RGBA изображению**.

Для OBJ/glTF изображения содержат геометрию (не только clear), lit отличается от unlit, ordinary совпадает с instanced. Отдельно проверяются 501 lit instances и сохранённый explicit unlit shader при включённом Lighting. `GpuInstancingRegression --dx12` расширен lit-проверками 500 кубов+пол с различными rotations/non-uniform scales, checker texture и tint. Реальные counters: 501→1 draws, 0→1 instanced draws, 501 instances. Resize, frame reuse, page growth, unsupported shader и per-draw CB rejection, destroy-before-submit остались в тесте.

## 12. Regression и обычные engine runs

Итоговый основной CTest: **5/5 passed**, 16.21 s: Physics 10.36, Resource 0.33, RenderInterpolation 4.77, GpuInstancing 0.40, Lighting 0.33 s. DX12 тесты дополнительно запущены отдельно и завершились exit code 0; они не выдаются за headless CTest.

| Требование | Проверка |
|---|---|
| Lambert / Ambient / Directional / zero intensity | Ожидаемые GPU RGB, несколько направлений и режимов |
| Direction normalization / zero vector | CPU constants, GPU свет с ненормализованными направлениями |
| Rotation / non-uniform scale / inverse transpose | Независимый tangent oracle, GPU цвета, импортированный glTF node |
| Texture / tint | Цветная texture + ожидаемые RGB, checker stress и DDS head |
| Instancing OFF/ON / одинаковое освещение | Побитовый RGBA, индивидуальные matrices и tint |
| 500 batching | CPU общий RenderSystem и GPU реальные counters, 501→1 draws |
| No normal data / singular matrix | GPU unlit fallback без неинициализированных значений |
| Shader fallback | Explicit unlit, untagged variant, читающий b0 rejected instance variant |
| Create/delete/generation/Clear/Restart | Прежний SharedPath расширен normal matrix и lighting checks |
| Play/Stop и interpolation | CPU Resolve + prepared normal comparisons, recreated RenderSystem с общими runtime settings, production lifecycle callbacks |

Обычный WhispEngine: paused benchmarks для 100/250/500/1000, по 2500 frames; active physics smoke каждого размера по 200 frames, 500 с interpolation OFF, 500 unlit/ordinary, lifecycle 744 frames (Stop/Resume, Serial/Parallel, Restart, Clear/Recreate). Все итоговые engine/capture exit codes 0. Physics settings не менялись. SharedPath проверяет сохранение ненулевого ambient runtime setting при Clear/последующем Create и передачу той же конфигурации после уничтожения и создания RenderSystem.

## 13. Производительность

Каждый размер — один engine process, неподвижная сцена, 100 warmup frames, затем OFF ordinary / OFF instanced / ON ordinary / ON instanced по 300 frames, цикл повторяется дважды. По 600 samples на режим, 9600 samples суммарно. Переключатели diagnostic benchmark закреплены после editor input; script проверяет каждую строку CSV. Обычная интерактивная работа редактора этим не ограничена. Tracy подключён во всех итоговых сравнениях; сохранены четыре `.tracy` и zone CSV.

Все times ниже — **CPU wall ms**, nearest-rank percentiles. Lighting 0/1 = OFF/ON, Instancing 0/1 = OFF/ON. CPU normal preparation остаётся включённой в обоих lighting modes; сравнение не измеряет стоимость её удаления или отдельного старого unlit PSO.

| Cubes | Lighting | Instancing | Draws | Gather p50 | Prepare p50 | Batch p50 | Submit p50 | CPU frame p50 / p95 / p99 |
|---:|:---:|:---:|---:|---:|---:|---:|---:|:---|
| 100 | 0 | 0 | 101 | 0.2976 | 0.0158 | 0.0016 | 0.0445 | 2.7703 / 4.7519 / 6.1422 |
| 100 | 0 | 1 | 1 | 0.2874 | 0.0156 | 0.0015 | 0.0137 | 2.2512 / 4.7791 / 5.8037 |
| 100 | 1 | 0 | 101 | 0.2872 | 0.0138 | 0.0013 | 0.0408 | 2.5095 / 4.8691 / 6.3160 |
| 100 | 1 | 1 | 1 | 0.2879 | 0.0155 | 0.0014 | 0.0129 | 2.5675 / 4.0693 / 5.6761 |
| 250 | 0 | 0 | 251 | 0.4294 | 0.0231 | 0.0029 | 0.0627 | 1.9913 / 6.7409 / 12.9454 |
| 250 | 0 | 1 | 1 | 0.4275 | 0.0230 | 0.0029 | 0.0126 | 1.9069 / 4.6430 / 6.0581 |
| 250 | 1 | 0 | 251 | 0.6008 | 0.0263 | 0.0034 | 0.0833 | 2.7759 / 4.8909 / 6.5148 |
| 250 | 1 | 1 | 1 | 0.7417 | 0.0387 | 0.0042 | 0.0180 | 3.8441 / 5.8724 / 6.8594 |
| 500 | 0 | 0 | 501 | 0.8861 | 0.0506 | 0.0064 | 0.1166 | 2.6089 / 7.6016 / 9.1919 |
| 500 | 0 | 1 | 1 | 1.1450 | 0.0760 | 0.0083 | 0.0217 | 3.7312 / 7.2504 / 9.0333 |
| 500 | 1 | 0 | 501 | 1.5656 | 0.0772 | 0.0102 | 0.1767 | 5.3919 / 8.7061 / 11.9834 |
| 500 | 1 | 1 | 1 | 1.5892 | 0.0778 | 0.0102 | 0.0249 | 5.0057 / 8.3103 / 14.5636 |
| 1000 | 0 | 0 | 1001 | 1.7053 | 0.1024 | 0.0142 | 0.1806 | 5.4800 / 7.5033 / 8.4576 |
| 1000 | 0 | 1 | 1 | 1.7160 | 0.1034 | 0.0151 | 0.0267 | 6.9268 / 7.8801 / 9.2417 |
| 1000 | 1 | 0 | 1001 | 1.6984 | 0.1031 | 0.0150 | 0.1841 | 6.9391 / 8.3271 / 9.3633 |
| 1000 | 1 | 1 | 1 | 1.7064 | 0.1051 | 0.0160 | 0.0299 | 6.9271 / 8.6751 / 9.4011 |


В lit режиме Submit p50 выигрывает от instancing на всех размерах; для 500 — 0.1767→0.0249 ms. Изменения Gather/Prepare между Lighting modes показывают системную вариативность: их алгоритм одинаков. Frame tails и medians не дают универсального улучшения FPS. Например, для 500 lit instancing p99 хуже ordinary. Эти цифры не приписываются работе pixel shader.

GPU timestamps в текущем backend отсутствуют; GPU time **не измерен**. Readback доказывает цветовые результаты, не GPU duration. Tracy CPU zones RenderGather/Prepare/BatchBuild/Submit, InstanceUpload/InstancedDraw присутствуют; для 500 в capture 2500 Gather/Prepare/Submit и 1200 InstanceUpload/InstancedDraw events.

Draws ordinary→instanced: 101/251/501/1001→1. Instanced draws: 0→1. Instance stride вырос на 48 bytes (+33.3%); upload bytes instanced: 19392/48192/96192/192192. Ordinary CB upload: 25856/64256/128256/256256. Capacity entries: 1024/1024/1024/2048. Root constants записываются в commands и не включены в uploadBytes, который измеряет upload buffers.

Первый capture в sandbox не подключился к локальному TCP Tracy; engine честно завершил ожидание с timeout. Итоговые измерения выполнены вне sandbox с доступом к localhost. Первый provisional run с несколькими изменёнными UI переключателями исключён; таблица построена только по повторному строгому сценарию.

## 14. Vulkan

Основная mixed DX12+Vulkan Release сборка успешна. Отдельная Vulkan-only конфигурация (`ENABLE_DX12=OFF`, `ENABLE_VULKAN=ON`) также успешно собрана, включая приложение и все regression executables. Её итоговый CTest: **5/5 passed**, 21.95 s (CPU regressions, без DX12 GPU tests).

Vulkan shader/backend не переписан. Он сохраняет прежние unlit debug primitives и default capability=false для instancing. Его прежний backend не реализует UploadMesh/CreateTexture2D/CreateShaderProgram для resource mesh path. Поэтому обычные загруженные модели и lighting **не поддерживаются Vulkan в этом этапе**; общий RenderSystem сохраняет прежний debug fallback. Наличие Vulkan окна в DX12 engine runs не доказывает Vulkan lighting. Интерфейсы общего render/ECS слоя не содержат DX12 типов.

## 15. Изменённые файлы и воспроизведение

Новые: `engine/render/Lighting.h`, `engine/shaders/dx12/textured_unlit.hlsl`, `test_projects/legacy_workspace/Assets/models/lighting_sphere.gltf`, `tests/LightingRegression.cpp`, `tests/RunLightingDiagnostics.ps1`, `tests/SummarizeLightingDiagnostics.py`, этот отчёт.

Изменены: `CMakeLists.txt`, `WhispEngine.cpp`, `engine/core/Application.h/.cpp`, `engine/core/RenderDiagnosticSample.h`, `engine/ecs/systems/RenderSystem.h/.cpp`, `engine/editor/EditorLayer.cpp`, `engine/render/IRenderAdapter.h`, `engine/render/RenderInstanceData.h`, `engine/render/backends/dx12/Dx12RenderAdapter.h/.cpp`, `engine/resources/MeshData.h`, `engine/resources/loaders/MeshLoader.cpp`, `engine/shaders/dx12/textured.hlsl`, `tests/GpuInstancingRegression.cpp`.

Из корня repository, PowerShell, существующая configured Release сборка с tests ON:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out/build/x64-release --parallel 8'
Push-Location out/build/x64-release
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --output-on-failure
./LightingRegression.exe --dx12
./GpuInstancingRegression.exe --dx12
./WhispEngine.exe --stress 500
./WhispEngine.exe --stress 500 --frames 200 --no-lighting --no-instancing
./WhispEngine.exe --stress 500 --frames 200 --no-interpolation
./WhispEngine.exe --lighting-benchmark 500 lighting-500.csv
./WhispEngine.exe --stability-scenario lifecycle.csv
Pop-Location
./tests/RunLightingDiagnostics.ps1
& "$env:USERPROFILE\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" tests/SummarizeLightingDiagnostics.py
foreach($count in @(100,250,500,1000)) {
    & out/diagnostics/tracy-tools/tracy-csvexport.exe "out/diagnostics/lighting/lighting-$count.tracy" > "out/diagnostics/lighting/lighting-$count-zones.csv"
}
git diff --check
```

Script по умолчанию требует matching Tracy 0.14.1 capture CLI по `out/diagnostics/tracy-tools/tracy-capture.exe`; пути можно переопределить параметрами. Нужен доступ к localhost:8086. `-SkipCapture` разрешает smoke/CSV без capture, но profiling overhead отличается от таблицы. Перед сравнением закрыть другие engine/capture процессы. Script перезаписывает только свои artifacts. `--lighting-benchmark` чередует четыре режима автоматически, `--render-benchmark` сохраняет прежний сценарий instancing; CSV расширен последней колонкой lighting.

Для нового build включить `WHISP_BUILD_PHYSICS_TESTS=ON`. Vulkan-only воспроизводимая конфигурация с уже загруженными dependency sources (Developer PowerShell, VsDevCmd environment):

```powershell
$cmake='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$deps=(Resolve-Path out/build/x64-release/_deps).Path
& $cmake -S . -B out/build/lighting-vulkan -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DASSIMP_INSTALL_PDB=OFF -DENABLE_DX12=OFF -DENABLE_VULKAN=ON -DENABLE_TRACY=ON -DWHISP_BUILD_PHYSICS_TESTS=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON "-DFETCHCONTENT_SOURCE_DIR_GLFW=$deps/glfw-src" "-DFETCHCONTENT_SOURCE_DIR_ASSIMP=$deps/assimp-src" "-DFETCHCONTENT_SOURCE_DIR_STB=$deps/stb-src" "-DFETCHCONTENT_SOURCE_DIR_TRACY=$deps/tracy-src" "-DFETCHCONTENT_SOURCE_DIR_ENKITS=$deps/enkits-src"
& $cmake --build out/build/lighting-vulkan --parallel 8
& (Join-Path (Split-Path $cmake) "ctest.exe") --test-dir out/build/lighting-vulkan --output-on-failure
```

`CMAKE_TRY_COMPILE_CONFIGURATION=Release` нужен в этом окружении: первоначальная Debug compiler probe с PDB дала C1902; Release probe и ASSIMP_INSTALL_PDB=OFF устранили обращение к PDB server без изменения исходников проекта (Assimp по умолчанию добавляет /Zi даже к Release). Vulkan SDK/VULKAN_SDK требуются прежним Shaders.cmake. Итоговые логи: build-final.log, build-assets-final.log, ctest-final.log, vulkan-build-final.log, ctest-vulkan-final.log, LightingRegression-dx12.log и dx12-instancing-final.log. Artifacts/logs/CSV/captures находятся локально в `out/diagnostics/lighting`, не включены в git.

## 16. Ограничения и дальнейшее расширение

Это Ambient + один Directional Lambert light без PBR, shadows, deferred rendering, point/spot lights, SSAO, bloom и tone mapping. Настройки — runtime/session, без disk serialization. Static glTF import запекает node transforms; полноценный animated scene graph/skinning не появился. Очень вырожденные transforms используют unlit fallback. Mixed-quality normals внутри одной поверхности могут давать переход между lit/unlit — импорт должен поставлять нормальные данные; корректные OBJ/glTF проверены.

Для будущего Shadow Mapping уже имеются world-space DirectionalLight, model matrices и корректные per-instance normals. Минимальное продолжение: определить scene light identity/serialization, добавить отдельный shadow pass/light view-projection и depth texture binding, расширить render pass/batch key при появлении изменяемого состояния. Эти возможности сейчас не реализованы и не включены скрыто в shader.
