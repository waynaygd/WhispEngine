# WhispEngine — Material System, DirectX 12

Проверено 10 октября 2026, Windows, MSVC x64 Release, NVIDIA GeForce RTX 4060 Laptop GPU, Tracy 0.14.1. Ветка `job_system`. До изменений выполнен `git status`; 31 существующий изменённый/новый файл сохранён в `out/diagnostics/materials/before-material-files.zip`, также сохранены `before-material.patch` и `before-material-status.txt`. Commit, push, merge, PR и смена ветки не выполнялись.

Финальная проверка продолжена 13 октября 2026. Save дополнительно проверяет существующего canonical ancestor до создания новых каталогов; добавлен regression для nested asset и отсутствия побочных каталогов после невалидного Save. Логи повторной сборки и тестов: `build-final.log`, `all-tests-final.log`. Для финального CTest потребовался запуск вне Windows tool sandbox: sandbox запрещал canonical query разрешённого каталога assets. Проверки путей в самой реализации сохранены.

## 1. Исходная архитектура

Изучены GPU_INSTANCING_REPORT, LIGHTING_SYSTEM_REPORT, SHADOW_MAPPING_REPORT и RENDER_INTERPOLATION_REPORT. MaterialResource, JSON MaterialLoader, кеш ResourceManager и частичный Material Editor уже существовали. ECS materialPath разрешался в asset; legacy shader/texture имели приоритет, tint умножался на asset baseColor. Editor «Load Asset Values» копировал baseColor в tint, после чего RenderSystem повторно умножал его на baseColor. Этот путь заменён отдельным редактированием asset и instance tint.

Assimp загружал position/normal/UV, объединял vertex/index buffers и сохранял submesh ranges/material name/texture name, но RenderSystem рисовал весь mesh одним материалом. Indices уже глобальные: baseVertex добавлен при импорте. Для submesh draws дополнительный BaseVertexLocation должен быть нулём. Ранее исправленные PreTransformVertices и нормали сохранены.

## 2. Архитектура Material Assets

MaterialResource — CPU-данные: стабильный относительный key в Resource<T>, name, shaderPath, texturePath, baseColor, surfaceMode, alphaCutoff, doubleSided, castShadows, receiveShadows. GPU handles остаются в ShaderResource/TextureResource. Нет второй системы загрузки или отдельной копии GPU-материала, которую нужно синхронизировать.

Импортированные материалы хранятся внутри общего MeshResource, с идентичностью `model-key#material/index`; standalone JSON назначается поверх них. Это общие CPU-данные кешированного mesh, а не автоматически созданные файлы на диске. Embedded texture имеет key `model-key#embedded/index` и проходит через существующие TextureLoader/ResourceManager.

## 3. ResourceManager integration

Использованы существующие Load/LoadAsync/Get/Reload/WatchForHotReload и main-thread finalization. Async material finalization уже запускает загрузку shader/texture dependencies. Scene preload теперь учитывает slotMaterials и явные shader/texture overrides. GPU upload остаётся на render thread и ограничен существующим бюджетом finalization.

Reload меняет данные существующего Resource handle и повышает version, поэтому все ссылающиеся entity видят новые значения. Ошибка reload сохраняет последнее пригодное содержимое и диагностируется. Material Asset не владеет GPU handles; изменение цвета/флагов не создаёт новые buffers/descriptors. Старые texture/shader cache entries живут до существующего ReleaseGpuResources/shutdown. Автоматическое вытеснение не добавлено.

Dirty registry ResourceManager блокирует файловый reload редактируемого незаписанного asset. Save снимает dirty, Revert требует явного подтверждения внутри ImGui и перечитывает файл. ClearAll очищает registry вместе с кешами.

## 4. MaterialComponent и overrides

Компонент хранит materialPath, slotMaterials по импортированному material slot, MaterialOverrides и прежние shaderPath/texturePath/tint. Optional<bool> отличает наследование от явного false; optional<string> texturePath позволяет явно убрать текстуру. Tint всегда индивидуален и умножается на baseColor.

Порядок: defaults → imported slot → entity material asset → slot-specific asset → explicit overrides → legacy nonempty mesh paths → legacy nonempty MaterialComponent paths. Legacy tint применяется последним как множитель. Назначение asset через редактор очищает прежние shader/texture paths, чтобы они не скрывали новый asset. В старой сцене сохранён приоритет её существующих полей.

## 5. Сериализация

Standalone JSON версия 2; прежние JSON без version и версия 1 читаются без автоматической перезаписи:

```json
{
  "version": 2,
  "name": "Paint",
  "shaderPath": "dx12/textured.hlsl",
  "texturePath": "textures/material_test.png",
  "baseColor": [1.0, 0.25, 0.125, 1.0],
  "surfaceMode": "Opaque",
  "alphaCutoff": 0.5,
  "doubleSided": false,
  "castShadows": true,
  "receiveShadows": true
}
```

Проверяются типы, целочисленная версия, четыре конечных color channels в [0,1], cutoff в [0,1], известный surfaceMode и относительные resource paths. Absolute paths, URI и выход за root отвергаются. Unknown fields игнорируются с diagnostics; Save не сохраняет неизвестные поля. Новые версии отвергаются. Shader path должен быть непустым.

Save разрешает только `.json`, проверяет canonical parent внутри asset root и запрещает symlink destination/temp. Запись и flush идут в соседний `.saving`, затем MoveFileExW с REPLACE_EXISTING/WRITE_THROUGH атомарно заменяет destination. Неуспешная валидация не трогает исходный файл. Save выполняется на главном потоке; конкурирующие writers одного файла не поддерживаются.

Scene v2 сохраняет materialPath, slotMaterials, materialOverrides и tint. Чтение принимает v1/v2 и старый массив entity; результат заменяется только после успешного разбора всех entity. ECS config читает те же новые поля.

## 6. Legacy compatibility

Старые сцены и `.material.json` не мигрированы автоматически. Обычные модели без MaterialComponent используют imported slots/defaults. Прежние shader/texture/tint overrides продолжают работать. Стресс-сцена теперь дополнительно ссылается на общий `materials/stress.material.json`: белый baseColor сохраняет существующие индивидуальные цвета. Ни один физический параметр не изменён.

Play/Stop сохраняют компоненты и runtime material cache. Clear удаляет entity, не общий asset. Restart создаёт новые компоненты со ссылкой на общий stress asset; asset edits продолжают действовать. Scene reload восстанавливает записанные ссылки/overrides, сохраняя ресурсный кеш. Незаписанные asset edits сохраняются в текущей сессии до Save/Revert/shutdown; между запусками сохраняется только файл.

## 7. OBJ/glTF import

OBJ/MTL: material name, diffuse color, diffuse texture, opacity и two-sided metadata. glTF: baseColorFactor/baseColorTexture, doubleSided, alpha mode/cutoff. Внешние пути разрешаются относительно модели и нормализуются против asset root. Embedded compressed images декодируются stb_image в существующем TextureLoader; uncompressed aiTexel копируется в RGBA. Для embedded image TextureLoader один раз импортирует исходную модель на CPU при загрузке texture, после чего используется общий texture cache.

DefaultMaterial без объявленного цвета сохраняет белый цвет. Импортированные factors приводятся к конечному диапазону [0,1]. Vertex stride остаётся 32; node transforms и inverse-transpose normals из предыдущего этапа сохранены.

Mesh binary cache версия 4 содержит material slot и metadata. Material-bearing OBJ с `mtllib` переимпортируется при resource load/reload, чтобы nested MTL edits не скрывались старым бинарным кешем. ResourceManager всё равно кеширует результат между кадрами. Для glTF учитываются source mtime и изменённые `.bin` в директории модели. Полный граф внешних glTF buffer dependencies в подкаталогах не добавлен: после их изменения нужно обновить сам glTF либо убрать его `.wmesh` перед reload. Автоматический watcher всех MTL dependencies также не добавлен; ручной mesh reload перечитывает MTL.

## 8. Multi-submesh rendering

Gather создаёт snapshot для каждого готового submesh; visibleObjects считает entity, visibleSubmeshes — snapshots. Pose разрешается один раз на entity, mesh handle также разрешается один раз. Index range следует за snapshot/packet во всех main/shadow ordinary/instanced draws. Global indices используют BaseVertexLocation=0. Пустые/выходящие за buffer ranges не отправляются.

Все поддерживаемые поверхности Opaque, поэтому packets стабильно сортируются по совместимому состоянию. Models с несколькими submesh группируются по range между разными entity, а не превращаются в отдельные mesh uploads.

## 9. Surface Modes

Opaque реализован: depth test/write, прежний texture sampling, Ambient + Lambert, shadow visibility и tint. Alpha baseColor хранится, но Opaque PS по-прежнему возвращает alpha=1.

Masked и Transparent **не реализованы**. Их metadata сохраняется, UI явно показывает unsupported, renderer выдаёт diagnostic и исключает поверхность из main и shadow passes. Alpha test, blending и alpha-aware shadows не имитируются. Alpha Cutoff пока только metadata для будущего согласованного masked main/depth path.

## 10. Shadow flags

castShadows=false исключает packet из shadow preparation, сохраняя main rendering. receiveShadows=false через MaterialCB b4 даёт visibility=1; Ambient не меняется. Double Sided выбирает CullNone PSO в ordinary/instanced main и depth-only shadow paths. Нормали сохраняют свою геометрическую ориентацию; отдельная двухсторонняя модель BRDF/переворот normal для обратной стороны не добавлены.

Старый unlit shader сохранён. Пользовательский shader должен поддерживать MaterialCB/lighting contract, чтобы участвовать в receive-shadow shading; unlit естественно не затеняется. Reflection допускает MaterialCB b4 только с ожидаемым размером, сохраняя проверку отсутствия per-draw CB в instanced variant.

## 11. GPU Instancing

Batch key: mesh, shader, texture, firstIndex/indexCount, doubleSided, receiveShadows. BaseColor × tint остаётся в per-instance tint, поэтому разные цвета не дробят batch. Cast flag не дробит main batch; shadow preparation фильтрует casters и группирует совместимые непрерывные диапазоны.

RenderInstanceData остаётся 192 байта: Model/MVP/Tint/normal matrix. Frame upload pages, fences, retirement и reuse immutable spans между shadow/main сохранены. Новые PSO variants также проходят retirement. 500 кубов + пол: **1 main draw + 1 shadow draw**, 501 instances, 96 192 upload bytes при instancing; ordinary: 501+501 draws и 128 256 bytes.

## 12. ImGui Material Editor

Create/Open/Edit/Save/Revert общего asset; name, shader, base texture, base color, surface mode, cutoff и три флага. Asset редактируется непосредственно в общем CPU Resource, отдельной редактируемой копии нет. Dirty marker и guard запрещают незаметную смену asset; Revert подтверждается modal. Ошибки файла и texture fallback/loading показываются.

Instance Tint, наследуемые/явные флаги и material slots редактируются отдельно с existing undo snapshots. Add Material Overrides доступен для импортированной модели без MaterialComponent. Clear Overrides не переписывает asset. Есть кнопка Material slots demo.

## 13. Asset Browser

Прежний список materials используется для Open Material и Use Material. Assign добавляет компонент при необходимости и очищает конкурирующие legacy shader/texture paths. Новый файл появляется после обновления списка. Через slot combo можно назначить asset отдельному импортированному slot. Imported CPU material metadata не сохраняется автоматически как standalone JSON; для редактируемого внешнего asset нужно Create и назначение slot.

## 14. Regression results

Финальный CTest: **9/9 passed**, включая PhysicsRegression, ResourceRegression, RenderInterpolationRegression, GpuInstancingRegression, LightingRegression, MaterialSystemRegression, MaterialSystemDx12Regression, ShadowMappingRegression и ShadowMappingDx12Regression.

Новый MaterialSystemRegression проверяет defaults/legacy, JSON types/ranges/version/unknown fields, unsafe paths, create/save/load, atomic replacement и failed save, shared cache identity, reload и last-good fallback, dirty guard/revert, explicit empty/false overrides, scene roundtrip, async material/texture dependencies, OBJ/MTL/glTF slots и external/embedded textures, 500 batching, instancing ON/OFF, cast/receive/double-sided state, asset×tint, runtime reference replacement/cache metrics, unsupported alpha modes, interpolated nonuniform normals, instance fallback, create/delete/clear/restart и grouping двух submesh между entity.

Дополнительно запущены все три прежних executable с `--dx12`, engine lifecycle scenario и 500 cubes interpolation OFF. GpuInstancingRegression подтверждает resize, upload growth, descriptor/resource retirement и identical ordinary/instanced pixels. Lifecycle содержит Play/Stop/Resume/Restart/Clear/Recreate. DX12 ERROR/CORRUPTION assertions и engine validation checks прошли.

## 15. DX12 GPU readback

MaterialSystemDx12Regression проходит через **ECS → ResourceManager → RenderSystem → DX12**, а не только напрямую вызывает shader. Два entity с двумя submesh дают два instanced draws для одного mesh. OBJ и glTF ordinary/instanced RGBA совпадают точно.

Texture RGB=(128,192,64), instance tint=(0.5,0.8,0.6), ambient-only=1. Для red slot factor=(1,0.25,0.125) oracle RGB≈(64,38,5); green=(0.125,1,0.25) → (8,154,10), допуск 2 UNORM единицы. Проверены slot reassignment, live shared color edit, отражённый scale/culling, Double Sided, delete и clear.

Отдельный main/depth readback проверяет cast/receive, неизменность depth map при receive=false, Ambient в тени RGB≈(20,27,8), обычный/instanced shadow equivalence, CullNone depth для отражённой плоскости и retirement до исполнения записанных команд. GPU flag checks используют backend state; его связь с ECS flags отдельно проверена recorder regression.

Артефакты: `out/build/shadows-dx12/material-readback/{obj,gltf}.ppm`, `out/diagnostics/materials/{obj-readback,gltf-readback,material-editor}.png`. Editor screenshot визуально проверен: оба импортированных mesh показывают собственные red/green slots.

## 16. CPU/GPU performance

Before снят **до реализации**, after — с окончательным material resolver и общим stress asset. Одинаковые paused stress scene, camera, 1024 shadow map, lighting, PCF, viewport и физические настройки. По 1800 samples на count: shadows OFF/Hard/PCF × instancing OFF/ON, 300 frames на режим; первые 30 frames каждого режима исключены. Сравнение использует отключённый Tracy capture в обоих запусках. GPU значения получены timestamp queries, не оценены из CPU времени.

Все значения ниже — реальные медианы в ms для PCF/instancing ON:

| Cubes | CPU frame before/after | Gather before/after | Prepare before/after | Batch before/after | Submit before/after | Shadow GPU before/after | Main GPU before/after | GPU frame before/after |
|---:|---|---|---|---|---|---|---|---|
| 100 | 1.457 / 1.468 | .169 / .244 | .009 / .009 | .001 / .004 | .004 / .004 | .012 / .011 | .023 / .009 | .091 / .067 |
| 250 | 2.735 / 2.335 | .448 / .612 | .023 / .023 | .004 / .011 | .006 / .005 | .031 / .023 | .037 / .035 | .166 / .130 |
| 500 | 3.787 / 5.370 | .978 / 1.446 | .051 / .051 | .008 / .032 | .007 / .007 | .052 / .042 | .052 / .040 | .254 / .243 |
| 1000 | 4.996 / 6.348 | 1.756 / 2.639 | .106 / .106 | .015 / .053 | .006 / .007 | .076 / .073 | .053 / .052 | .234 / .315 |

При 500/PCF ordinary after: CPU frame 4.538, Gather 1.414, Prepare .051, Batch .031, Submit .121, shadow GPU .038, main GPU .028, GPU frame .138. Материал resolve при instancing ON: .172/.432/1.014/1.819 ms для 100/250/500/1000; hits=101/251/501/1001, misses=0 после warmup. Дополнительные CPU lookup/copy/sort измеримы; оптимизация resolver cache по resource versions остаётся возможным дальнейшим шагом. Ускорение всей CPU frame от instancing не гарантируется: driver/present/editor/scheduling заметны в этих коротких запусках.

Отдельная сцена: 200 models (100 OBJ + 100 glTF), 400 submesh instances. PCF ordinary/instanced: 400/4 main и 400/4 shadow draws; upload 102400/76800 bytes; Gather .627/.635, Prepare .039/.040, Batch .033/.034, Submit .094/.008; shadow GPU .031/.008, main GPU .053/.057; CPU frame 6.934/6.953. Imported materials используют mesh cache, поэтому standalone material cache hits/misses здесь 0/0.

Полная таблица OFF/ON, upload bytes и cache metrics: `out/diagnostics/materials/performance.md`, исходные CSV в `before/`, `after/`, `multi-material.csv`. Это один последовательный прогон, без статистического доказательства малых GPU дельт. GPU frame включает editor и остальные GPU commands. Изменения CPU frame не приписываются pixel shader.

Отдельный валидный Tracy capture `material-500.tracy`: 1903 frames, 14.26 s, 1 807 111 zones. MaterialResolve: 951399 events, mean 1881 ns; RenderGather mean 1.284 ms, RenderPrepare .051 ms, RenderSubmit .083 ms по смешанным режимам. Экспорт в `tracy-material.csv`/`tracy-render.csv`. Первый ошибочно подключившийся к regression process capture не используется; окончательный trace записан после завершения тестов.

## 17. Изменённые файлы

Материальный этап: MaterialResource.h, MaterialComponent.h, новый MaterialSerialization.h; MaterialLoader.h/.cpp, MeshData.h, MeshLoader.cpp, TextureLoader.cpp, ResourceManager.h/.cpp; RenderSystem.h/.cpp; IRenderAdapter.h, новый MaterialState.h; Dx12RenderAdapter.h/.cpp, Dx12Shadows.cpp, textured.hlsl; ConfigLoader.h/.cpp, SceneSerializer.cpp, Application.h/.cpp, RenderDiagnosticSample.h, PhysicsStressScene.h, EditorLayer.h/.cpp, WhispEngine.cpp; CMakeLists.txt, engine/CMakeLists.txt; MaterialSystemRegression.cpp, RunMaterialDiagnostics.ps1, SummarizeMaterialDiagnostics.py, этот отчёт; preload stress material в прежних GpuInstancingRegression/ShadowMappingRegression.

Fixtures: material_slots.obj/.mtl/.gltf, material_test.png, material_slots.scene.json, material_red/material_green/stress.material.json. Ранее созданные Lighting/Shadows файлы и отчёты сохранены. Job System, WhispPhysics, fixed timestep и RenderInterpolation implementation не менялись.

## 18. Команды воспроизведения

В x64 Developer Command Prompt VS 2022, из корня репозитория:

```bat
chcp 65001
cmake -S . -B out/build/materials-dx12 -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_DX12=ON -DENABLE_VULKAN=OFF -DENABLE_TRACY=ON -DWHISP_BUILD_PHYSICS_TESTS=ON -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DASSIMP_INSTALL_PDB=OFF
cmake --build out/build/materials-dx12 --parallel 8
ctest --test-dir out/build/materials-dx12 --output-on-failure
```

Фактический проверенный build directory — `out/build/shadows-dx12`, существующий DX12-only cache с локальными FetchContent dependencies. Для его повторной сборки заменить `materials-dx12` на `shadows-dx12`. Новая чистая configure загружает зависимости; offline использовать уже имеющийся cache.

В PowerShell из корня репозитория:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/RunMaterialDiagnostics.ps1 -BuildDirectory out/build/shadows-dx12 -ArtifactDirectory out/diagnostics/materials -SkipCapture
# С Tracy capture.exe, отдельно после завершения regression-процессов:
powershell -NoProfile -ExecutionPolicy Bypass -File tests/RunMaterialDiagnostics.ps1 -BuildDirectory out/build/shadows-dx12 -ArtifactDirectory out/diagnostics/materials -SkipStress -SkipRegression
& out/diagnostics/tracy-tools/tracy-csvexport.exe -f MaterialResolve out/diagnostics/materials/material-500.tracy
& out/diagnostics/tracy-tools/tracy-csvexport.exe -f Render out/diagnostics/materials/material-500.tracy
python tests/SummarizeMaterialDiagnostics.py out/diagnostics/materials
```

Tracy tools берутся из уже собранного `out/diagnostics/tracy-tools`; другой capture.exe задаётся параметром CaptureExecutable. Для новой build directory нужно заменить соответствующий параметр scripts. Перед сравнением baseline повторить RunShadowDiagnostics с `-SkipRegression -SkipActive -SkipCapture` на сохранённом исходном состоянии; текущий before уже находится в архивированных diagnostics.

Прямой readback и демо, из build directory:

```powershell
$env:WHISP_DX12_DEBUG='1'
./MaterialSystemRegression.exe --dx12
./LightingRegression.exe --dx12
./GpuInstancingRegression.exe --dx12
./ShadowMappingRegression.exe --dx12
./WhispEngine.exe --material-demo
./WhispEngine.exe --material-benchmark 100 multi-material.csv
./WhispEngine.exe --shadow-benchmark 500 stress-500.csv
```

В редакторе: Material Editor → Material slots demo; Asset Browser → material_red/material_green → Open Material; выбрать entity → Use Material либо Add Material Overrides → slot combo. Изменение asset Base Color действует на все ссылающиеся entity. Instance Tint и Override flags действуют только на выбранную entity. Save и Revert проверяют выбранную семантику shared asset.

## 19. Ограничения

Основная и проверенная реализация — DX12. Vulkan в этой DX12-only задаче не собирался и не менялся; новые optional adapter методы имеют no-op defaults, но полноценный multi-submesh/state rendering в Vulkan этим не подтверждён.

Masked/Transparent отсутствуют; texture transforms/additional UV sets, normal maps, glTF extensions, animation/skinning и автоматическая material extraction в JSON не добавлены. Imported colors/textures используют текущий UNORM color space без sRGB/gamma/tone mapping. Image decode/reimport dependencies и отсутствие общего buffer dependency graph описаны выше. Одновременные external edits одного dirty asset требуют Save/Revert; полноценный merge изменений файлов не реализован. ImGui controls собраны и demo screenshot проверен, но автоматизированный UI click-through каждого control не проводился; underlying material operations покрыты regression.

## 20. Подготовка к PBR

CPU asset/instance/backend state разделены; versioned schema, slots, dependency cache, range batching и shader contract позволяют добавить следующие поля отдельным этапом. Здесь **нет** metallic/roughness, GGX, Fresnel, Smith, normal mapping, linear/sRGB pipeline или IBL. Следующий шаг должен сначала согласовать color space и material/shader schema, затем реализовать и проверить PBR main path. Masked потребует одновременно main/depth alpha test и совместимого texture binding в shadow pass. Автоматический переход к PBR не выполнялся.
