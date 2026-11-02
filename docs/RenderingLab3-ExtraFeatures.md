# ЛР №3, вариант Г — дополнительные возможности Material System

## Контекст и границы

Ветка `rendering_lab`, исходный HEAD `8ca46647fdffeaaa33c06a6aaf0facbe8e598595`,
рабочее дерево в начале чистое. Предыдущие этапы сохранены. Коммиты, смена ветки,
reset/clean не выполнялись. Пользовательские проекты и существующий RenderLab
не менялись. Проверки выполнены на изолированных копиях RenderingLab3 в out.
Оценку выставляет преподаватель; этот отчёт не объявляет получение 6/6 баллов.

## Контрольная точка A: Automatic Material Hot Reload

Инкремент A проверен **до начала реализации B**:

- MSVC x64 Release: WhispEngine, MaterialSystemRegression,
  MaterialEditorUXRegression, ResourceRegression собраны.
- RenderingLab3 verifier: **304 checks passed**, exit code 0, DX12 validation = 0.
- CTest: **5/5 passed**, 170.62 s: Resource, MaterialSystem CPU/DX12,
  MaterialEditorUX и MaterialEditorUXRestart.
- Проверены external Base Color/Roughness, несколько Entity, slot material,
  Prefab instance и Scene roundtrip, texture reference replacement, missing map,
  malformed/repair JSON, burst atomic saves, dirty draft, watch ownership.
- 48 последовательных material reload cycles: live mesh/texture/shader counts
  и PSO creations остались на прогретом уровне.

Логи: `out/material-extra-A-build.log`, `out/material-extra-A-tests.log`,
`out/material-extra-A-verification.log`. Контрольный diff без коммита:
`out/material-extra-A-checkpoint.patch`.

Во время разработки A обнаружено и исправлено повторное снятие watch ownership
после уничтожения ResourceManager при shutdown. До исправления один запуск
завершился аварийно после successful rendering assertions. В результат A выше
входит повторный штатно завершившийся запуск, а не тот аварийный процесс.

### Механизм

`ResourceManager::SetMaterialUsage/ClearMaterialUsage` учитывает независимых
владельцев: RenderSystem и активный Material Preview. RenderSystem собирает
реально разрешаемые materialPath/slotMaterials в `TryBuildRenderSnapshot`, затем
публикует usage после Gather. Prefab использует тот же ECS-компонент и путь.
Preview публикует asset и текущий draft. Открытие Material Editor не требуется
для обновления материалов сцены.

`RefreshMaterialWatches` строит объединение нужных material/shader/texture watches.
Изменение texture/shader key добавляет новую зависимость; неиспользуемые watches
удаляются, пока другой owner не удерживает их. Explicit legacy WatchForHotReload
остаётся отдельным владельцем: автоматическая подписка его не удаляет.
История stamp сохраняется без polling неактивных файлов, чтобы при повторном
использовании заметить изменения, сделанные в отсутствие подписчиков.

`PollMaterialWatches` использует mtime/size/existence, включая disappearance и
возвращение файла. После изменения требуется 120 ms без новых изменений.
Повторные atomic saves объединяются. Неудача JSON reload сохраняет last-good
Resource data, сообщение содержит key/reason; следующая попытка происходит после
нового file event. Dirty Material draft оставляет reload pending, предупреждение
выводится один раз; несохранённые данные не заменяются молча.

Новые GPU-зависимости загружает существующий RenderSystem finalization path.
Цвет/roughness не меняют shader generation и не создают PSO. Shader source reload
использует существующий PrepareShaderGpu с подготовкой replacement до retirement.
Asset Browser Refresh к этому механизму не относится.

Исходники: `engine/resources/ResourceManager.{h,cpp}`,
`engine/ecs/systems/RenderSystem.{h,cpp}`, `engine/editor/EditorMaterial.cpp`,
`engine/editor/EditorDocuments.cpp`.

## B: Material Asset Inheritance

### Формат и совместимость

Материалы v1–v4 читаются и сохраняются прежним путём без массовой миграции.
Для child используется v5:

```json
{
  "version": 5,
  "name": "Rough child",
  "parentMaterial": "materials/demo_base.material.json",
  "overrides": { "roughness": 0.85 }
}
```

`MaterialResource::parentMaterial/parameterOverrides` — authored metadata.
Наличие ключа означает явный override: false, 0 и пустой texture path сохраняются.
Отсутствующий ключ означает Inherit. В overrides допускаются поля существующей
v4 material schema; name/version не являются наследуемыми параметрами.
Пустой shaderPath остаётся недопустимым, как в v4; пустые texture paths допустимы.

`MaterialLoader::Parse` валидирует sparse v5 с теми же types/ranges/path rules,
`Serialize` сохраняет только parent/name/overrides. `SerializeValues` и `Merge`
формируют и проверяют эффективные v4-параметры без flattening сохранённого child.

### Централизованный resolver

`ResourceManager::ResolveMaterial` разрешает цепочку до RenderPacket. Источником
остаются обычные кешированные Resource<MaterialResource>, отдельного GPU material
cache не появляется. Resolved entries зависят от raw resource identity/version и
generation родителя. Изменение base инвалидирует результаты потомков. После этого
RenderSystem применяет прежние Entity MaterialOverrides и Tint.

Проверяются direct self-reference, косвенный cycle, максимум 32 материала в цепочке,
missing/invalid parent и относительные ключи внутри разрешённого asset root.
Путь parent разрешается относительно project asset root, а не каталога child.
При ошибке сохраняется последний успешный resolved snapshot; если его не было,
невалидный child не отправляется на GPU. Ошибки выдаются с путями, без повторного
логирования неизменной ошибки каждый кадр. Parent JSON watches входят в usage
зависимого child, даже если parent не назначен отдельной Entity.

CPU-потомки не владеют GPU allocations. Их resolved shader/texture keys проходят
через существующие общие ресурсы и PSO Cache. Совпадающие pipeline states остаются
общими независимо от parent chain и material constants.

### Existing ImGui Material Editor

Добавлена секция **Inheritance → Parent Material**. Создать обычный материал через
Asset Browser, выбрать parent, Save — получится sparse v5 child. Для grandchild
выбрать child в Parent Material. Второй редактор не создавался.

Controls показывают resolved values. У наследуемых полей есть checkbox Override;
unchecked показывает Inherit и отключает редактирование эффективного значения.
Снятие checkbox сбрасывает конкретный override; **Reset to Inherited** в секции
Inheritance снимает все overrides. Цвет, shader/textures, numeric parameters,
UV sets и render flags используют существующие controls. UV set array наследуется
как одно поле. Циклический/недействительный parent показывается как error;
Save не публикует невалидную цепочку. Изменения draft видны в preview, в сцену
публикуются после Save. Parent changes обновляют чистый draft, а own overrides
и dirty edits сохраняются.

Пустой Parent у уже разрешённого child означает превращение в самостоятельный
v4 material с текущими values; автоматическая миграция остальных файлов не нужна.

## Демонстрация

Проект: `test_projects/RenderingLab3/RenderingLab3.whispproject`.
Открыть через Project Browser → Open Project; затем Asset Browser → Scenes →
`Inheritance.json` (Open Scene). Ресурсы находятся в `test_projects/RenderingLab3/Assets`,
резервные демонстрационные копии — в `test_projects/RenderingLab3/Recovery`.

Исходные четыре сферы и startup scene `Scenes/MaterialSystem.json` сохранены.
Отдельная `Scenes/Inheritance.json` добавляет верхний ряд E/F/G:

- E: demo_base, metallic 0.65 / roughness 0.18;
- F: inheritance_child, roughness override 0.85;
- G: inheritance_grandchild, red Base Color, остальные поля через child.

Дочерние assets и recovery copies находятся только в RenderingLab3.
Сцена использует исходные shader/mesh ресурсы проекта.

### Защита допфич за 3–4 минуты

1. **0:00–0:40:** открыть RenderingLab3, Edit mode. Не открывая Material Editor,
   изменить Base Color/metallic в `Assets/materials/demo_base.material.json`
   внешним редактором и сохранить. Через debounce A/B/C обновятся. Показать
   стабильные PSO creations в Console / Diagnostics.
2. **0:40–1:20:** открыть base в Material Editor. Изменить roughness/texture,
   preview, Save, вернуться в сцену. Показать Save/Revert и отсутствие C++ build.
3. **1:20–2:10:** открыть `Scenes/Inheritance.json`. Показать Parent у F, затем
   Parent у G. Изменить Base metallic: оба потомка наследуют его. Изменить Base
   roughness: F/G остаются на 0.85 из-за child override. Red G блокирует только
   Base Color, остальные параметры продолжает наследовать.
4. **2:10–2:50:** в child снять Override Roughness → Inherit → Save. F/G берут
   roughness base. Открыть JSON child: inherited values не записаны в overrides.
   Выбрать другой корректный Parent, сохранить и показать новое разрешение.
5. **2:50–3:40:** временно задать Parent=grandchild у child в draft: показать
   cycle error и отказ Save. Revert вернёт рабочую цепочку. Показать возрастающие
   hits и стабильные creations при параметрических изменениях.

Для исходных примеров Reset можно использовать существующий DemoActions RestoreBase.
Изменение startup scene не требуется. Все эксперименты проводить в отдельной копии
проекта; после демонстрации восстановить child из Recovery или Revert до Save.

## Финальная проверка и ограничения

MSVC **Release x64**: полная сборка завершилась с exit code 0
(`out/material-extra-full-build.log`). Для standalone PhysicsRegression добавлена
его прямая зависимость WhispEngineJson: MaterialResource теперь содержит JSON
metadata. Новых renderer pipelines или изменений алгоритма PSO Cache нет.
Параллельное копирование существующих IBL fixtures конфликтовало на одном output
directory; полная сборка выполнена последовательно (`--parallel 1`).

Итоговый RenderingLab3 verifier: **396 checks passed**, exit code 0.
Это один итоговый прогон, а не сумма повторных запусков. Проверены:

- реальные DX12 pixels после external material edits и parent propagation;
- прежние A–D, Scene/Prefab roundtrip, Entity overrides поверх grandchild;
- создание Material Asset, existing preview/editor Save/Revert, shader/texture
  edits, native ImGui Roughness Override checkbox и sparse child Save;
- Parent replacement, Inherit/Reset, cycle/missing parent diagnostics, отказ Save
  ошибочного draft без повреждения сценового resolver cache;
- malformed JSON и invalid HLSL сохраняют last-good GPU output; исправление
  восстанавливает reload; missing texture использует штатный fallback;
- 48 material reload cycles, затем 6 texture file events и 4 успешных shader
  reloads. GPU handles действительно заменяются, live allocation counts и
  размер PSO cache после retirement остаются ограниченными;
- до открытия проекта creations=0; первая подготовка creations=12/cached=12;
  после прогрева hits растут при стабильных creations. Изменения constants и
  texture references не создают PSO; успешный HLSL reload пересоздаёт затронутые
  pipeline states. Итог после всех shader-сценариев: hits=3105, misses=84,
  creations=84, cached=24. Это счётчики корректности, не performance measurements;
- **DX12 validation errors = 0**; исходные файлы RenderingLab3 не изменились
  во время тестирования, изменения делались в уникальном project clone.

Полный лог: `out/build/x64-release/renderinglab3-verification/verification.log`.
Артефакты там же: `inheritance.ppm`, screenshots исходной демонстрации и
изолированные `run-*` проекты. Запуск из корня репозитория:
`python test_projects/RenderingLab3/Verification/VerifyDemo.py` после Release build
из x64 VS Developer shell.

Общий CTest прогон: **29/30**, 529.57 s
(`out/material-extra-final-tests.log`). Прошли Resource, Physics,
RenderInterpolation, GpuInstancing/Lighting CPU, MaterialSystem CPU/DX12,
MaterialEditorUX и Restart, Prefab CPU/Document/Restart/Inheritance CPU/DX12,
Camera/Viewmodel CPU/DX12/Vulkan, ShadowMapping CPU/DX12,
PbrCore/PbrTextures/Ibl CPU/DX12 и HdrOutput DX12.

PrefabDx12 сначала остановился на стартовой сцене. Его fixture переведён на
уникальный project clone с явным OpenProject вместо зависимости от recent
workspace. Повторный прогон прошёл загрузку сцены, native drag/drop, Undo/Redo,
но выявил тайминг synthetic double-click: при 2–3 FPS два кадра длиннее штатных
0.30 s. Только для тестовой пары событий интервал увеличен до 2 s, реальные
ImGui card events и прежние assertions сохранены. Повторный CPU/DX12 прогон:
**2/2 passed**, 39.55 s (`out/material-extra-prefab-retest.log`). Таким образом,
все 30 выбранных тестов имеют успешный итоговый результат; единый первый прогон
не выдаётся за 30/30. Это исправление тестовой изоляции и ввода, не изменение Prefab
или material production logic; исходный сбой не объявляется доказанным baseline.

Дополнительно `GpuInstancingRegression.exe --dx12` и
`LightingRegression.exe --dx12` завершились с exit code 0: реальные ordinary /
instanced pixels совпадают, проверены 501 → 1 draws, resize, buffer growth,
retirement, OBJ/glTF, lighting и fallback. Логи:
`out/material-extra-instancing-dx12.log`, `out/material-extra-lighting-dx12.log`.
После добавления явных schema v1/v2/v3/v4 compatibility и roundtrip assertions
MaterialSystemRegression повторно собран и прошёл: **1/1 passed**
(`out/material-extra-final-schema-tests.log`). Production code после итогового
DX12 verifier и общего прогона не менялся.

### Готовность к демонстрации

| Допфича из ТЗ | Статус | Подтверждение |
| --- | --- | --- |
| Existing ImGui Material Editor, 2 балла | Реализована, готова к показу | Native UI regression, preview, Save/Revert, child edits |
| Automatic Material Hot Reload, 2 балла | Реализована, готова к показу | Отдельная контрольная точка A и итоговый DX12 verifier |
| Material Asset Inheritance, 2 балла | Реализована, готова к показу | CPU schema/resolver и native DX12 chain/reset/cycle/pixel проверки |

Это три реализованные демонстрируемые допфичи по 2 балла; баллы назначает
преподаватель. RenderDoc captures и performance before/after не заявляются.

### C++ для показа преподавателю

- `engine/resources/ResourceManager.cpp`: SetMaterialUsage,
  RefreshMaterialWatches, PollMaterialWatches, ResolveMaterial.
- `engine/resources/loaders/MaterialLoader.cpp`: Parse, SerializeValues,
  Serialize, Merge, Save; `engine/resources/MaterialResource.h`: sparse metadata.
- `engine/ecs/systems/RenderSystem.cpp`: TryBuildRenderSnapshot — resolver,
  затем ECS overrides; Update — публикация usage.
- `engine/editor/EditorMaterial.cpp`: EnsureMaterialSession, DrawMaterialEditor,
  SaveMaterialDraft/ReloadMaterialDraft — существующий ImGui редактор.
- `engine/render/ShaderGpuPreparation.h`: существующий PrepareShaderGpu;
  `engine/render/backends/dx12/Dx12RenderAdapter.cpp`: существующие
  CreateShaderProgram (локальный acquire), BindMeshMaterial,
  PruneMeshPipelineCache и diagnostics PSO cache.
- `tests/MaterialSystemRegression.cpp`, `tests/MaterialEditorUXRegression.cpp`,
  `test_projects/RenderingLab3/Verification/VerifyDemo.cpp`: проверяемые сценарии.

К известным ограничениям относятся синхронное чтение небольшого JSON parent при
первом разрешении и синхронная shader compilation существующего backend.
File events основаны на metadata (mtime/size/existence), не на content hashing.
Hot reload не отслеживает полный граф HLSL include dependencies. Неактивные кеши
ресурсов хранятся до штатного release/project switch; проверяется bounded reuse
конечного набора ресурсов, а не бесконечная загрузка уникальных texture keys.
Vulkan smoke Camera/Viewmodel прошёл; полный parity DX12 не заявляется.
Masked/Transparent, Shader Reflection UI, RHI и RenderGraph не добавлялись.

## Git status

Финальная ветка `rendering_lab`, HEAD
`8ca46647fdffeaaa33c06a6aaf0facbe8e598595`, исходный HEAD не изменён.
18 modified tracked files, 7 новых файлов; index не менялся, коммиты не создавались.
`git diff --check` прошёл. Исходные четыре сферы, startup scene, descriptor,
base/alternate materials и существующий RenderLab не изменены.

```text
 M .gitignore
 M CMakeLists.txt
 M engine/ecs/systems/RenderSystem.cpp
 M engine/ecs/systems/RenderSystem.h
 M engine/editor/EditorDocuments.cpp
 M engine/editor/EditorLayer.cpp
 M engine/editor/EditorLayer.h
 M engine/editor/EditorMaterial.cpp
 M engine/render/backends/dx12/Dx12RenderAdapter.h
 M engine/resources/MaterialResource.h
 M engine/resources/ResourceManager.cpp
 M engine/resources/ResourceManager.h
 M engine/resources/loaders/MaterialLoader.cpp
 M engine/resources/loaders/MaterialLoader.h
 M test_projects/RenderingLab3/Verification/VerifyDemo.cpp
 M tests/MaterialEditorUXRegression.cpp
 M tests/MaterialSystemRegression.cpp
 M tests/PrefabRegression.cpp
?? docs/RenderingLab3-ExtraFeatures.md
?? test_projects/RenderingLab3/Assets/materials/inheritance_child.material.json
?? test_projects/RenderingLab3/Assets/materials/inheritance_grandchild.material.json
?? test_projects/RenderingLab3/Assets/ui/inheritance_legend.ui.json
?? test_projects/RenderingLab3/Recovery/inheritance_child.material.json
?? test_projects/RenderingLab3/Recovery/inheritance_grandchild.material.json
?? test_projects/RenderingLab3/Scenes/Inheritance.json
```

Сборочные логи, clone-проекты и screenshots находятся в игнорируемом `out/`;
в исходный demo не записываются результаты испытаний.
