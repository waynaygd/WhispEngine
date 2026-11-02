# RenderingLab3 — демонстрация Material System

## Расположение и безопасность

Проект: `test_projects/RenderingLab3/RenderingLab3.whispproject`.
Стартовая сцена: `Scenes/MaterialSystem.json`.

Начальное состояние этапа 3: `rendering_lab`, HEAD
`186215f090dc880a266c8cd755859857d44608f2`; рабочее дерево содержит незакоммиченные
изменения этапа 2. Они сохранены. Ветки не переключались, коммиты не создавались.
RenderLab, DOOM67 и другие пользовательские проекты не открывались и не менялись.
Production renderer, PSO Cache и Scene/Material/Prefab serialization не менялись.

Использованы действующие Project v1, Scene v4, Material v4, Prefab v2 и UI v1.
Модели sphere/cube скопированы из `engine/assets/models/primitives`, текстура —
из корректного `legacy_workspace/Assets/textures/pbr_base.png`, HLSL — отдельная
копия `engine/shaders/dx12/textured.hlsl`. Все используемые material/model/texture
keys локальны проекту. HLSL-копия не имеет внешних include dependencies.

`Recovery` содержит shader, два материала и сцену в исходном виде. Это не assets
для автоматической загрузки, а безопасные копии для восстановления демонстрации.
`DemoActions.ps1` использует только фиксированные пути внутри RenderingLab3.

## Состав сцены

| Entity | Геометрия и материал | Индивидуальные данные |
|---|---|---|
| A — Base Material | sphere.obj, demo_base.material.json | Tint белый, overrides пусты |
| B — Material Instance | тот же sphere.obj и demo_base | cyan Tint [0.12, 0.8, 0.95, 1] |
| C — Roughness Instance | тот же sphere.obj и demo_base | roughness override 0.85 |
| D — Assign Here | тот же sphere.obj, demo_alternate | отдельный медный asset с demo_surface.png |

Base: серебристый PBR, metallic 0.65, roughness 0.18. У C широкий рассеянный
блик вместо более узкого блика A. B имеет отдельный цвет. D демонстрирует другой
asset и текстуру. Четыре постамента и платформа используют нейтральный материал.
Два directional lights и ambient освещают объекты, HDR tone mapping включён.
Тени отключены: они не нужны для сравнения свойств материала.

В сцене 13 Entity: четыре образца, четыре постамента, платформа, Demo Camera,
два источника света и UI Canvas с легендой. Все объекты статичны, physics/scripts
для демонстрации не требуются. Prefab `prefabs/demo_roughness.prefab.json`
содержит дополнительный образец с roughness override; по умолчанию не заспавнен.

## Открытие и камера

Из корня репозитория:

```powershell
$env:WHISP_DX12_DEBUG='1'
& .\out\build\x64-release\WhispEngine.exe --project .\test_projects\RenderingLab3\RenderingLab3.whispproject
```

Либо Project Browser → Open Project → выбрать descriptor выше. Открывается
MaterialSystem. Сцена рассчитана на исходную editor camera, смотрящую вдоль +Z.
При ранее изменённом положении камеры выделить A/B/C/D в Scene Hierarchy
с Ctrl/Shift и нажать **F**. Для чистого вида включить **Game View (G)**.
Необязательная легенда: **UI Preview (Ctrl+U)** в панели Viewport.
Надписи легенды описывают исходные значения и не являются динамическими показаниями.

Для Play задана отдельная Demo Camera с обзором ряда. Основную демонстрацию
проводить в Edit: Material Editor и Save заблокированы в Play.

## Live Demo: точные действия

1. Открыть проект и убедиться, что видны четыре сферы слева направо A/B/C/D.
2. Выделить **A - Base Material** в Scene Hierarchy.
3. Inspector → Material → **Open Material Asset Editor**. Либо Asset Browser →
   materials → demo_base.material.json → **Open Material Editor**.
4. Material Editor → Surface → Base Color: изменить цвет; **Save Material**
   (либо Ctrl+S при активном Material Document). Пока Save не выполнен,
   меняется только preview draft.
5. Вернуться во вкладку MaterialSystem. A/B/C обновились; B сохраняет свой Tint,
   C сохраняет roughness override. D использует другой asset и не изменился.
6. Выделить C. Inspector → Material → Roughness: включённый override, значение
   0.85. Изменить на 0.18 и наблюдать блик. Для сравнения вернуть 0.85.
7. Снять checkbox override у Roughness: появится **Inherit**, параметр берётся
   из demo_base. При возврате в 0.85 снова включить checkbox и задать значение.
8. Asset Browser → папка materials → **Create → Material**, key
   `materials/demo_created.material.json` → Create. В новом Material Document
   задать цвет/roughness и нажать Save Material.
9. Вернуться в сцену, выбрать D. В Asset Browser выбрать созданный материал →
   **Assign to Active Entity** / **Assign selected to entity**. Изменится D.
   Для продолжения вернуть D материал demo_alternate той же командой.
10. Открыть **Console / Diagnostics → Diagnostics**, найти **Mesh PSO cache**.
    Подождать окончания загрузки/прогрева; записать creations/cached.
11. Менять Tint B или Roughness C, затем Base Color общего asset с Save.
    Изображение меняется, hits растёт, creations после прогрева остаётся прежним.
    C++ не изменяется и не перекомпилируется.
12. Для Missing Texture выполнить подготовку и команды из следующего раздела.
13. Для Invalid Shader Reload сломать только проектную HLSL-копию при уже
    работающем shader. Показать ошибку и сохранённую сцену.
14. Восстановить HLSL-копию. Ошибка GPU preparation исчезает, creations возрастает
    из-за новой shader generation; сцена продолжает отображаться.

После назначения нового asset через Assign прежние slot/optional/legacy overrides
сбрасываются существующей операцией редактора. Это удобно для D. Не назначать так
материал B/C, если требуется сохранить их индивидуальные overrides.

## Ошибки только на демонстрационных копиях

Перед ошибками вернуть D `demo_alternate.material.json`. Открыть этот asset в
Material Editor, дождаться рабочего preview, затем вернуться во вкладку сцены.
Так регистрируются watches материала и project-local shader. Draft сохранить
или отменить: dirty material блокирует внешний reload.

Из корня репозитория, в другом PowerShell:

```powershell
.\test_projects\RenderingLab3\DemoActions.ps1 MissingTexture
```

У D меняется ссылка на `textures/INTENTIONALLY_MISSING.png`. Этот файл намеренно
не существует. Через watcher обновляется материал; ResourceManager сообщает
path/reason, renderer использует белую default texture. Медная текстура исчезает,
но объект продолжает рисоваться. Console содержит конкретный missing texture key.

```powershell
.\test_projects\RenderingLab3\DemoActions.ps1 RestoreAlternate
```

Ссылка возвращается к `textures/demo_surface.png`, изображение D восстанавливается.
Нельзя ограничиваться Asset Browser Refresh: он не перечитывает содержимое ресурса.

```powershell
.\test_projects\RenderingLab3\DemoActions.ps1 BreakShader
```

Команда заменяет только `RenderingLab3/Assets/shaders/demo_surface.hlsl` невалидным
текстом. Не закрывать процесс: last-good GPU allocation существует в его памяти.
Console показывает compiler reason, shader key и `retaining last working GPU
shader/PSO`. Изображение ряда сохраняется; creations не растёт от failed compile.

```powershell
.\test_projects\RenderingLab3\DemoActions.ps1 RestoreShader
```

Восстанавливается рабочая копия из Recovery. Watcher повышает resource version;
успешная подготовка публикует replacement и retiring старого handle. PSO creations
растёт, последующие кадры снова переиспользуют готовые entries. Не редактировать
engine/shaders/dx12/textured.hlsl для этой демонстрации.

Для возврата исходного Base Color есть `DemoActions.ps1 RestoreBase`. Для полного
возврата сцены после завершения редакторской сессии можно скопировать
`Recovery/MaterialSystem.json` в `Scenes/MaterialSystem.json` и заново открыть проект.
Это перезаписывает только демонстрационную сцену; сохранить нужные эксперименты заранее.

## PSO Diagnostics: воспроизводимый порядок

- В новом процессе при загрузке создаются ordinary/instanced/culling/HDR variants.
  Поэтому creations изначально больше нуля даже при одном shader.
- После загрузки открыть Diagnostics и сравнить creations за несколько секунд:
  число стабильно, hits растёт.
- Изменить только константы B/C: creations не должен увеличиться.
- Изменение цвета asset + Save также не требует нового PSO.
- Перед shader error открыть material document и дождаться preview, чтобы
  дополнительные editor preparations уже завершились; затем зафиксировать счётчики.
- Failed HLSL compile сохраняет прежние PSO. Успешное восстановление меняет version
  и создаёт затронутые variants заново. Снова подождать и показать стабильность.

Глобальные totals могут включать thumbnails/preview и зависеть от ранее открытых
assets. Сравнивать дельты в одной сессии после прогрева, а не требовать фиксированный
стартовый total. `Material resolve hits/misses` — другой кеш. Эти действия не
являются performance benchmark.

## Проверка

Демонстрационный verifier: `test_projects/RenderingLab3/Verification/VerifyDemo.cpp`.
Он использует настоящий Application/ImGui/DX12 и ReadBackViewportPixels, работает
только с копией проекта в `out/build/x64-release/renderinglab3-verification/run-*`.
Перед завершением проверяет неизменность всех исходных файлов RenderingLab3.

Запуск из x64 VS Developer shell после сборки MaterialEditorUXRegression:

```powershell
python test_projects/RenderingLab3/Verification/VerifyDemo.py --build out/build/x64-release
```

`python` можно заменить путём к доступному Python. Скрипт берёт реальные compile/link
flags существующего target из Ninja, создаёт отдельный executable и не меняет
production CMake targets или исходники. Требуются уже настроенная Release DX12
сборка и доступные MSVC/Windows SDK environment variables.

Фактическая проверка 29 октября 2026: **98 checks passed**, exit code 0,
MSVC x64 Release, DX12, NVIDIA GeForce RTX 4060 Laptop GPU.

| Проверка | Результат |
|---|---|
| Project/Scene schemas, запуск editor Application | Проект открывается, startup scene выбирается |
| Mesh/Material/texture/shader paths | Загружены sphere/cube, три материала, локальная текстура и HLSL |
| Сцена на DX12 | Четыре сферы, четыре постамента, платформа отправлены на GPU; viewport readback 960×540 |
| Shared Material Save | GPU-регионы A/B/C изменились, регион D остался побайтово прежним |
| Roughness override / Inherit | Изменение через Inspector operation меняет GPU pixels; Inherit даёт nullopt |
| Scene Save/Reload | CaptureWorld после reload совпадает; Tint и roughness override сохранены |
| Prefab | v2 Prefab успешно разрешён, roughness override 0.85 сохранён |
| Material Editor | Material Document открывается, native preview создан, draft Save публикует asset |
| Новый Material / Assign | Операции Asset Browser создают asset и назначают D; GPU output меняется |
| PSO initial preparation | До открытия проекта creations=0, после загрузки creations=12/cached=12 |
| PSO warm reuse | Hits растёт, creations стабильно; instance edits не создают PSO |
| Missing Texture | Есть error path/reason и usable fallback; GPU output D меняется, восстановление возвращает точные pixels |
| Invalid Shader Reload | Старый GPU handle и точные scene pixels сохранены, новых PSO нет |
| Shader repair | Новый GPU handle, creations растёт, исходные pixels восстановлены |
| DX12 validation | **0 errors** |
| Неизменность fixture | Все исходные файлы RenderingLab3 совпали с сохранёнными bytes |

Итоговые counters сценария: `hits=575, misses=36, creations=36, cached=24`.
Они включают создание нового материала с built-in shader и успешный reload
локального shader; это не creations за кадр. Первоначальные 12 variants,
прогрев и отсутствие создания от instance constants проверены отдельно.
Все пять команд DemoActions также успешно выполнены на завершённой изолированной
тестовой копии; восстановленные файлы совпали с Recovery по SHA-256.

Native UI проверен программно через реальные editor operations и рендер preview,
а не ручным click-through каждого control. Сцена дополнительно визуально
просмотрена по настоящему GPU readback: различимы блики A/C, Tint B и текстура D.

Артефакты (локальные, ignored):

- `out/build/x64-release/renderinglab3-verification/verification.log` — все assertions;
- `compile.log`, `link.log` в том же каталоге — сборка отдельного verifier;
- `scene.ppm` / `scene.png` — исходный viewport с легендой;
- `base-edited.ppm`, `missing-texture.ppm`, `shader-failed.ppm` — реальные GPU images;
- `out/renderinglab3-stage2-before.json` — исходные SHA-256 файлов этапа 2.

[Проверенный viewport](../out/build/x64-release/renderinglab3-verification/scene.png).
PNG — конверсия readback PPM для просмотра; содержимое сцены не дорисовывалось.

## Какие C++ файлы открыть преподавателю

| Файл | Что объяснять |
|---|---|
| engine/resources/MaterialResource.h | CPU asset, optional overrides |
| engine/resources/loaders/MaterialLoader.cpp | JSON Parse/Load/Serialize/Save, schema v4 |
| engine/ecs/components/MaterialComponent.h | materialPath, tint, instance overrides |
| engine/ecs/systems/RenderSystem.cpp | TryBuildRenderSnapshot, packets, submission |
| engine/render/backends/dx12/Dx12PipelineKey.h | Нормализованный pipeline key |
| engine/render/backends/dx12/Dx12RenderAdapter.cpp | CreateShaderProgram, BindMeshMaterial, cache reuse/retirement |
| engine/render/ShaderGpuPreparation.h | PrepareShaderGpu, last-good replacement |
| engine/editor/EditorMaterial.cpp | Draft/Save/Preview |
| engine/resources/ResourceManager.h/.cpp | Reload, version и PollHotReload |

Полная архитектура: `docs/RenderingLab3-MaterialPipeline.md`.

## Ограничения

- Material Instance — ECS-компонент с overrides, не отдельный asset с parent chain.
- Inherit в Inspector возвращает отдельное поле к материалу; многоуровневое
  наследование Material Assets не заявляется.
- Material Editor работает в Edit, в сцену изменения публикуются через Save.
- Watch требуется зарегистрировать открытием material document. Dirty draft
  блокирует material reload; shader include graph не отслеживается.
- При холодном старте с уже сломанным shader last-good GPU allocation отсутствует.
  Поэтому сначала прогреть рабочую сцену, затем BreakShader, затем RestoreShader.
- Поддерживаемый surface mode — Opaque. Нет новых blend/depth schemas, RenderGraph,
  reflection UI или новых renderer subsystems.
- Результаты являются correctness checks. Performance measurements и `.rdc`
  captures на данном этапе не выполнялись и не заявляются.

## Git status

Ветка и HEAD не изменились, коммитов нет. `git diff --check` проходит.
Хеши всех существовавших файлов этапа 2 сохранены; единственное дополнение к
существующему diff — строка `!/docs/RenderingLab3-Demo.md` в `.gitignore`.
Production renderer и существующие regression sources в этапе 3 не менялись.

`git status --short` по завершении:

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
?? docs/
?? engine/render/ShaderGpuPreparation.h
?? engine/render/backends/dx12/Dx12PipelineKey.h
?? test_projects/RenderingLab3/
```

Строки engine/tests и MaterialPipeline.md относятся к сохранённому этапу 2.
Новые файлы этапа 3: изолированный RenderingLab3 (assets, scene, recovery,
actions, verification и README) и это руководство. В существующем RenderLab
и пользовательских проектах изменений нет.
