# ЛР №3, вариант Г: Material Pipeline

## Область изменений

Проверка начата в ветке `rendering_lab`, HEAD
`186215f090dc880a266c8cd755859857d44608f2`, с чистым рабочим деревом.
Ветки не переключались, коммиты/reset/clean не выполнялись.
MaterialResource schema v4 и Scene/Prefab serialization сохранены.
Пользовательские проекты DOOM67/RenderLab не изменялись. Новая демонстрационная
сцена не создавалась. Performance before/after, три запуска и RenderDoc capture
этим этапом не заявляются.

## Архитектура

MaterialResource — общие CPU-данные. MaterialComponent содержит ссылку на asset,
slot assignments, optional overrides и tint. GPU shader/texture allocations
принадлежат соответствующим Resource, а не отдельной GPU-копии материала.

Основной путь:

```
MeshRendererComponent + MaterialComponent
  → RenderSystem::TryBuildRenderSnapshot
  → PrepareRenderPacket → batching
  → SubmitRenderPacket / instanced submitBatch
  → Dx12RenderAdapter::DrawMesh / DrawMeshInstanced
  → BindMeshMaterial → DrawIndexedInstanced
```

Resolver: defaults/imported slot → выбранный materialPath или slotMaterials →
optional overrides → legacy mesh shader/texture → legacy component shader/texture.
Tint применяется индивидуально. Asset Material inheritance chains не добавлены.

## Фактический PSO cache key

`engine/render/backends/dx12/Dx12PipelineKey.h`, `Dx12PipelineKey` формирует
владеющий бинарный ключ для сравнения по полному содержимому. Адреса указателей,
padding структур и GPU handles не сериализуются. unordered_map сравнивает полные
строки, поэтому совпадение хешей не означает совпадения pipeline.

Поля:

- нормализованный shader source key и CPU resource generation (`pipelineVersion`);
- полные VS/PS/DS/HS/GS bytecode bytes;
- serialized root signature, включая bindings/static samplers;
- stream output declarations, semantic strings, strides и rasterized stream;
- alpha-to-coverage, independent blend, активные RT blend/logic/write states;
- sample mask;
- все поля rasterizer, включая winding/culling/depth bias/conservative raster;
- depth enable/write/function и stencil enable/masks/front/back operations;
- input layout: semantic name/index, format, slot, offset, classification, step rate;
- strip cut, primitive topology type, активные RT formats и DSV format;
- sample count/quality, node mask, flags.

Игнорируемые disabled blend/depth/stencil поля не входят в ключ. Неактивные RTV
слоты не входят в ключ. Semantic names input layout нормализуются к верхнему
регистру. `CachedPSO` — driver compilation hint, а не графическое состояние.
Нормализация не пытается объединять произвольные семантически эквивалентные
программы или альтернативные кодировки root signature/input layout.

## Reuse, ownership и invalidation

Существующий UploadedShader по-прежнему предоставляет prelinked ordinary,
instanced, mirrored, double-sided и HDR variants. Вызовы создания вариантов
проходят через общий cache acquisition внутри `CreateShaderProgram`.

`m_MeshPipelineCache` хранит weak references; UploadedShader владеет cache entries
и сохраняет прежние ComPtr views вариантов. Эквивалентный ключ возвращает тот же
ID3D12PipelineState. Ключ строится при подготовке shader, не на каждом draw.
Различные material assets и instance constants не создают отдельные PSO.

Изменение shader generation меняет ключ даже при неизменном bytecode. После
успешной замены старый UploadedShader уничтожается; entries без владельцев
удаляются из lookup. Если другой handle ещё использует entry, PSO остаётся общим.
Независимые shader keys не дедуплицируются только по совпадению исходного текста.

`DestroyShader` сохраняет PSO/root signatures в существующих frame retirement
списках. `BeginFrame` освобождает slot после fence wait. Вне записи кадра
используется WaitForGpu. Cache не удерживает старые allocations бессрочно.
Ошибка подготовки освобождает временные entries без публикации handle.

## Material Binding contract

`BindMeshMaterial` используется и ordinary, и instanced mesh draws. Он выбирает
prelinked cache variant, устанавливает root signature/PSO, валидный CB0 address,
общие texture descriptors, scene/frame/environment/shadow bindings и MaterialCB.

Различие geometry data сохранено намеренно: ordinary заполняет draw CB и normal
root constants, instanced использует immutable RenderInstanceData span. Это не
две разные материальные системы. UI, particles, shadows, postprocessing имеют
собственные специализированные contracts и не включаются в mesh PSO counters.

Исходники: `engine/ecs/systems/RenderSystem.cpp`,
`engine/render/backends/dx12/Dx12RenderAdapter.{h,cpp}`,
`engine/render/RenderInstanceData.h`, `engine/shaders/dx12/textured.hlsl`.

## Ошибки и shader reload

ResourceManager сообщает path и decode/parse reason. Mesh resolver дополнительно
сообщает выбранное поведение (один раз на ошибочный resource generation):

| Сбой | Поведение |
|---|---|
| Missing material / malformed material JSON | Default material при первой неудачной загрузке; last-good CPU values при неудачном reload |
| Missing base texture | Default texture fallback; до готовности GPU upload draw пропускается |
| Missing normal/MR/AO | Optional map отключена, neutral shading fallback |
| Missing shader без рабочего GPU handle | Draw пропускается, неинициализированный PSO не используется |
| Invalid HLSL при reload рабочего shader | Сохраняется прежний GPU shader/PSO, compiler error и path выводятся в Console |

Общий `engine/render/ShaderGpuPreparation.h::PrepareShaderGpu` используется
RenderSystem и Material Preview. Сначала создаётся полный replacement handle,
затем он публикуется, и только потом retiring старого handle. Одна попытка на CPU
resource generation предотвращает compile-error spam каждый кадр. Новый reload
повышает generation и разрешает повторную попытку. `gpuHandleVersion` отражает
успешную GPU generation; `gpuAttemptVersion` — последнюю попытку. Color pipeline
contract также сохраняется от работающей GPU generation при неудачном reload.

Resource::ReplaceData переносит ownership старого handle и его color contract.
ResourceManager не перестраивался; сериализация shader/material не изменялась.

## Как показать PSO Cache в Diagnostics

Открыть **Console / Diagnostics → Diagnostics**. Строка **Mesh PSO cache**:

- hits — повторный cache acquisition и использование prelinked entry в mesh bind;
- misses — запрос нового pipeline при подготовке shader;
- creations — успешные CreateGraphicsPipelineState данного mesh cache;
- cached — число живых entries в lookup, без уже удалённых retired allocations.

После прогрева hits растёт, creations/misses/cached остаются стабильными при
изменении instance constants. Double-sided выбирает другой заранее созданный
entry. Успешный shader reload увеличивает creations и заменяет затронутые entries.
Counters накопительные на время жизни adapter, сбрасываются при Shutdown.
Material resolver hits/misses и descriptor-table cache — отдельные механизмы.

## Регрессии

`tests/MaterialSystemRegression.cpp` проверяет content-based equality ключа,
нормализацию disabled states, разделение pipeline states/version/identity;
DX12 readback для OBJ/glTF ordinary/instanced, разные instance constants,
общие PSO двух standalone materials и эквивалентного shader preparation,
разные rasterizer PSO pointers, отсутствие creations после прогрева, shader
reload/repair и last-good pixels, missing material/texture/shader и malformed JSON.

Scene material save/reload покрыт Schema в том же тесте. Изолированный
`MaterialEditorUXRegression` проверяет Material draft/Save/Reload, scene file
preservation и сохранение material override в Prefab. Prefab inheritance/lifecycle
проверяется существующим `PrefabInheritanceRegression`. Новые тесты не открывают
пользовательские проекты; материальные temporary fixtures принадлежат build tree.

Фактический запуск 29 октября 2026:

- Полная сборка `cmake --build out/build/x64-release --parallel 8` успешна
  (MSVC x64 Release, DX12 и Vulkan включены). В сборке есть прежние предупреждения;
  отсутствие warnings не заявляется.
- Целевой набор CTest: **25/25 passed**, 360.96 s. Это correctness run, а не
  performance measurement и не три независимых запуска.
- MaterialSystem CPU/DX12, MaterialEditorUX/Restart, Resource,
  RenderInterpolation, GpuInstancing/Lighting CPU, ShadowMapping CPU/DX12,
  Viewmodel CPU/DX12/Vulkan, CameraSystem CPU/DX12/Vulkan,
  PrefabInheritance CPU/DX12, PbrCore CPU/DX12, PbrTextures CPU/DX12,
  Ibl CPU/DX12, HdrOutput DX12.
- DX12 tests с соответствующими assertions прошли: ERROR/CORRUPTION = 0.
  Ошибки HLSL compiler намеренно вызываются negative tests; это не сообщения
  DX12 validation layer о некорректных GPU commands.
- MaterialEditorUX дополнен реальным watched HLSL-файлом в изолированном
  MaterialUX project: failure сохраняет старый handle и GPU pixels, repair
  публикует новую generation/PSO с теми же ожидаемыми пикселями.

Логи (локальные, ignored build artifacts):
`out/material-pipeline-final-build.log`, `out/material-pipeline-final-tests.log`,
`out/material-pipeline-final-details.log`. Исходный новый тест с изменением цвета
скрытого перекрывающим mesh instance был исправлен; итоговый набор выше прошёл.

После последней UI-правки (shader error показывается из актуального resource
status и исчезает после успешного repair) полная Release-сборка повторно прошла.
MaterialSystem CPU/DX12 и MaterialEditorUX/Restart повторно: **4/4 passed**,
153.81 s. Это повторная correctness-проверка четырёх тех же тестов, не четыре
дополнительных уникальных теста и не performance series.

Итоговая материальная DX12-регрессия вывела:

```
Mesh PSO cache: hits=558 misses=60 creations=60 cached=12; DX12 validation=0
```

Это итог сценария, включающего deliberate reload и release/recreate; стабильность
creations после прогрева проверяется отдельными assertions внутри сценария.

Дополнительно с `WHISP_DX12_DEBUG=1` успешно выполнены прямые команды из build dir:

```
GpuInstancingRegression.exe --dx12
LightingRegression.exe --dx12
```

Они подтвердили GPU pixels, ordinary/instanced equivalence, 501 → 1 draws,
lighting/normals/imports и resource retirement. Логи:
`out/material-pipeline-instancing-dx12.log`,
`out/material-pipeline-lighting-dx12.log`.
Последняя сборка/проверка и полный test output:
`out/material-pipeline-complete-build.log`, `out/material-pipeline-complete-tests.log`,
`out/material-pipeline-complete-details.log`.

Команды CTest используют `--test-dir out/build/x64-release`,
`--output-on-failure`, фильтр перечисленных test names. MaterialSystemDx12 и
MaterialEditorUX регистрируются с `WHISP_DX12_DEBUG=1`.

## Известные ограничения

- Cache относится к mesh graphics PSO, а не всем специализированным проходам.
- Variants создаются заранее; lazy compilation и disk pipeline library не добавлены.
- При горячей подготовке shader возможна CPU-пауза компиляции; асинхронная GPU
  pipeline compilation не добавлялась.
- Параметры/текстуры относятся к текущим CPU material data; при ошибке нового
  custom shader остаётся последний GPU contract. Автоматическая миграция
  произвольных несовместимых пользовательских layouts не поддерживается.
- Hot reload действует для зарегистрированных watches; удаление файлов и граф
  HLSL include dependencies этим этапом не расширялись.
- Masked/Transparent остаются unsupported. Schema v4 не расширена произвольными
  blend/depth states, reflection UI или material asset inheritance.

## Как объяснить преподавателю

«Материал — JSON с параметрами и ссылками. Entity хранит ссылку на общий материал
и только свои overrides. Resolver собирает итоговые данные, затем draw packet
передаёт их рендеру. Оба mesh draw пути вызывают общий material binding.

PSO описывает программу и графические состояния, а не конкретный цвет объекта.
Мы сравниваем полный нормализованный ключ: shader generation/bytecode, layout,
root signature, rasterizer, depth, blend, formats и samples. При совпадении
используем один GPU PSO. После прогрева draw только выбирает готовый вариант.
Это видно по стабильному creations и растущему hits.

При shader reload сначала готовим замену. Если компиляция сломана, старый shader
продолжает рисовать. Если замена успешна, переключаем handle и освобождаем старые
GPU объекты только с учётом уже отправленных кадров».

## Git status по завершении

Все изменения unstaged, новых коммитов нет. `git diff --check` проходит.
Изменённые файлы:

```
 M .gitignore
 M engine/ecs/systems/RenderSystem.cpp
 M engine/editor/EditorMaterial.cpp
 M engine/editor/EditorUi.cpp
 M engine/render/IRenderAdapter.h
 M engine/render/backends/dx12/Dx12RenderAdapter.cpp
 M engine/render/backends/dx12/Dx12RenderAdapter.h
 M engine/resources/Resource.h
 M engine/resources/ShaderResource.h
 M tests/MaterialEditorUXRegression.cpp
 M tests/MaterialSystemRegression.cpp
?? docs/RenderingLab3-MaterialPipeline.md
?? engine/render/ShaderGpuPreparation.h
?? engine/render/backends/dx12/Dx12PipelineKey.h
```

`.gitignore` содержит узкое исключение для данного отчёта; остальные локальные
заметки в docs остаются ignored. Исходный `/docs/` исключал весь каталог.
Ветка остаётся `rendering_lab`, HEAD не менялся. Изменений Scene/Prefab formats,
MaterialResource schema, пользовательских проектов или ресурсов нет.
