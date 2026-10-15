# PBR Core, этап 1 — DX12

Дата: 2026-10-13. Ветка: `job_system`. Конфигурация Release: `ENABLE_DX12=ON`, `ENABLE_VULKAN=OFF`, Tracy ON. GPU: NVIDIA GeForce RTX 4060 Laptop GPU. Реализованы linear-light pipeline и числовой Metallic/Roughness BRDF. Это ядро прямого освещения; полноценного indirect lighting нет.

Перед изменениями выполнены `git status` и проверка ветки. Сохранены 62 локально изменённых/новых файла: `out/diagnostics/pbr/before-tree.zip`, `before.patch`, `before-status.txt`, `before-sha256.json`. ZIP сохраняет исходные пути. Ничего из предыдущей работы не удалено; прежние отчёты и assets сохранены. Commit/push/merge/PR/смена ветки не выполнялись.

## 1. Исходный Color Pipeline

Изучены существующие отчёты Lighting, Material, Shadow Mapping и Shadow Quality, RenderSystem, DX12, shaders, загрузчики и редактор. Ранее PS перемножал texture RGB, material baseColor и tint непосредственно в числах UNORM/sRGB, затем умножал на Lambert lighting. Отдельного output transfer не было. Material schema v2; instance stride 192 байта. Существовали inverse-transpose normals, shared instance uploads между shadow/main и fence-защита.

OBJ/glTF идут через Assimp и ResourceManager. MeshVertex содержит position/normal/UV, stride 32. Узловые преобразования glTF запекаются загрузчиком с корректным преобразованием нормалей. Material slots имеют отдельные index ranges. Эта загрузка не переписана; MR-импорт glTF на этом этапе не добавлен.

## 2. Новый linear/sRGB contract

| Данные | Хранение / интерпретация | Преобразование |
|---|---|---|
| Base Color texture RGB | 8-bit sRGB | decode каждого texel до фильтрации |
| ECS individual tint RGB | display sRGB | decode в scene PS |
| Material baseColor RGB | linear factor, включая imported factors | передаётся отдельно, без decode |
| Light/ambient RGB | linear radiance chromaticity | UI отображает через encode и сохраняет через decode |
| Material ColorEdit RGB | display sRGB | UI переводит в linear asset factor |
| Alpha | linear | transfer не применяется |
| Metallic/roughness, normals, positions, depth, shadow map | линейные числовые данные | transfer не применяется |
| Scene PS результат | linear | один sRGB encode перед UNORM output |
| Clear/UI цвета | display values | напрямую в UNORM |

Base Color factor отделён от tint: предварительное перемножение linear factor и encoded tint на CPU было бы неправильным. Контракт факторов соответствует [glTF specification](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc).

Обычный `textured.hlsl`, включая Lambert и режим Lighting OFF, использует этот контракт. Явный `textured_unlit.hlsl` и пользовательские shaders без маркера `PBR_COLOR_PIPELINE_V1` сохраняют прежний display-space путь для debug/совместимости. Для них RenderSystem сохраняет прежнее folding factor*tint. Shader contract определяется по исходнику, поэтому копия shader под другим asset key также работает. Числовые shadow debug views остаются визуализацией маски, не radiometric lighting.

## 3. Texture/RTV/swapchain formats

PNG/JPEG декодируются stb_image в RGBA8 без преобразования цвета. DDS lightweight loader распаковывает BC1/DXT1 и BC3/DXT5 в RGBA8; существующие caches содержат те же bytes. Embedded glTF images также поступают через существующий TextureLoader. В этом этапе RGB albedo трактуется как sRGB; ICC/HDR и отдельная metadata цветового пространства не добавлены.

Texture resource/SRV, swapchain/backbuffer RTV, editor viewport resource/RTV/SRV и ImGui PSO сохраняют `R8G8B8A8_UNORM`. Shadow DSV/SRV остаются D32/R32, без color conversion. Все текущие color textures загружаются с одним mip.

Аппаратные sRGB views рассмотрены: они выполняют decode при sampling и encode при записи, см. [Microsoft DXGI color conversion](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/converting-data-color-space). Их глобальное включение потребовало бы разделения legacy/UI views. Выбран ручной путь без изменения render-target formats: четыре wrapped `Load`, decode каждого tap, linear bilinear interpolation, затем encode результата PS. Decode после обычного bilinear `Sample` был бы ошибкой. Readback black/white 50% blend даёт **188**, а не 128. Сейчас это эквивалентно принятому single-mip bilinear sampling; будущие mipmaps/anisotropy требуют продолжения работы с texture views.

ImGui получает уже encoded viewport bytes через UNORM SRV и выводит их через свой UNORM PSO. Повторного encode нет. Clear values остаются display-space background colors, что сохраняет цвета редактора.

## 4. LDR output

BRDF, radiance и сумма освещения рассчитываются в linear float до вывода. Перед output выполняются finite sanitation, clamp `[0,1]` и точная piecewise sRGB transfer (`0.0031308`, `12.92`, exponent `1/2.4`). HDR buffers, exposure и tone mapping не добавлены. Сильные узкие блики могут насыщаться до 255; это ограничение LDR, а не ограничение BRDF промежуточных значений.

## 5. Material schema и версия

Save пишет v3 со всеми существующими полями и новыми `metallic`, `roughness`, `shadingModel` (`Lambert`/`PBR`). Unversioned/v1/v2 читаются; отсутствие новых полей даёт Lambert, metallic=0, roughness=0.5. V3 также требует явного выбора PBR, не меняет режим автоматически по metallic.

```json
{
  "version": 3,
  "name": "Metal",
  "shaderPath": "dx12/textured.hlsl",
  "texturePath": "",
  "baseColor": [0.8, 0.55, 0.35, 1.0],
  "shadingModel": "PBR",
  "metallic": 1.0,
  "roughness": 0.35,
  "surfaceMode": "Opaque",
  "doubleSided": false,
  "castShadows": true,
  "receiveShadows": true
}
```

Типы, finite values и диапазоны валидируются. Неизвестная модель и будущая версия отклоняются. В existing atomic Save сохранены validation-before-write, path containment/symlink проверки, sibling temporary file и atomic replacement. Invalid MR save сохраняет прежний файл. При загрузке пользовательские assets автоматически не переписываются.

## 6. Legacy compatibility

Старые материалы остаются Lambert, формула освещения и shadow semantics сохранены. **Отображение меняется из-за color pipeline:** освещение теперь применяется к linear RGB; material factors больше не перемножаются с encoded texture/tint на CPU. Например texture R=128, tint R=0.6, lighting=0.7: прежний результат R≈54, новый R≈62. При коэффициентах материала, отличных от единицы, изменения могут быть ещё заметнее: factor 0.25 теперь означает четверть linear light, а не четверть encoded числа.

Ожидаемые цвета старых GPU regression-тестов обновлены формулами transfer, а не расширением допуска. Проверки ambient preservation, PCF footprint, ordinary/instance equality и batching сохранены. Hard-shadow threshold теперь рассчитывается из encoded linear ambient вместо прежнего числа 55. Отдельный unlit asset сохранён без изменения.

## 7. Metallic/Roughness parameters

Пользовательские значения `[0,1]` сохраняются точно, включая roughness=0. Только вычисление BRDF использует минимум 0.04. MaterialOverrides поддерживает optional numeric values и shading model; explicit zero отличается от отсутствия override. SceneSerializer, editor snapshots и undo используют существующие JSON helpers с новыми полями.

Приоритет: Defaults → Imported Material → Entity Material Asset → Slot Asset → Overrides. Assets по существующей семантике заменяют полный resolved material; slot asset имеет приоритет над entity asset. Legacy shader/texture paths сохраняют свой последний override; tint остаётся индивидуальным множителем. В конце применяется только явно выбранный диагностический global shading override. Новая независимая Material System не создана.

## 8. Cook-Torrance BRDF

`N` — normalized world normal, `V=normalize(cameraWorldPosition-worldPosition)`, `L=-directionalRayDirection`, `H=normalize(V+L)`. Для `NdotL<=0`, `NdotV<=0` или вырожденного H прямой BRDF равен нулю.

```text
F0 = lerp(0.04, baseColorLinear, metallic)
diffuse = (1-F) * (1-metallic) * baseColorLinear / PI
specular = D * G * F / (4 * NdotL * NdotV)
direct = (diffuse + specular) * directionalRadiance * NdotL * visibility
```

Нормали сохраняют inverse-transpose transform. Нормализация shader vectors предварительно делит на максимальный модуль компонента, чтобы не переполнять/не обнулять squared length при extreme scale. Singular/invalid normal transform использует существующий unlit fallback. Нормализуется и интерполированная нормаль в PS.

## 9. GGX

Perceptual roughness `r=max(userRoughness,0.04)`, `alpha=r*r`, `alpha2=alpha*alpha`. `D=alpha2/(PI*denom²)`, где `denom=(1-NdotH²)+NdotH²*alpha2`. Эта эквивалентная запись уменьшает cancellation при гладком материале и normal incidence. Numerical denominator guard `1e-20` не обрезает правильный пик при r=0.04.

## 10. Fresnel-Schlick

`F=F0+(1-F0)*(1-VdotH)^5`. Dielectric F0=0.04; у металла F0 берётся из linear base color. Diffuse умножается на `(1-F)*(1-metallic)`; у metallic=1 diffuse отсутствует.

## 11. Smith Geometry

Использован exact separable Smith GGX: `G=G1(NdotL)*G1(NdotV)`, `G1(x)=2*x/(x+sqrt(alpha²+(1-alpha²)*x²))`. Приближение Schlick k не смешивается с этой convention. Независимый CPU oracle вычисляет geometry через Smith lambda/tan(theta), в double.

## 12. Ambient approximation

`ambient=baseColorLinear*(1-metallic)*ambientColorLinear*ambientIntensity`. Это простой diffuse ambient, **не IBL и не физически полный indirect lighting**. Металлы в тени могут быть чёрными. Искусственный specular ambient/добавочные источники света для скрытия этого ограничения не добавлены. Material Editor сообщает об отсутствии IBL.

## 13. Lighting/Shadow integration

Directional normalization/fallback и single source lighting settings сохранены. Visibility вычисляется один раз и умножает общую direct сумму diffuse+specular. Ambient остаётся вне visibility. Cast/Receive, strength, hard/PCF, edge fade, receiver-plane correction и stabilized camera-fit projection сохранены. `Shadows.h`, `Dx12Shadows.cpp`, `shadow_depth.hlsl` не изменены в этом этапе.

## 14. Ordinary/Instanced compatibility

Instance stride **224 bytes**, alignment/offsets проверены static_assert:

| Поле | Offset | Размер |
|---|---:|---:|
| Model | 0 | 64 |
| MVP | 64 | 64 |
| Tint, sRGB | 128 | 16 |
| Normal rows | 144 | 48 |
| Metallic / roughness / shading model / reserved | 192 | 16 |
| Linear baseColor factor | 208 | 16 |

MVP/tint/normal prefix сохранён для прежних shaders. Новые semantics: `INSTANCE_SURFACE0`, `INSTANCE_BASECOLOR0`. Все MR/model/tint/normal values индивидуальны; MR и shading model не входят в batch key. Opaque PSO states, winding, texture, shader и mesh range остаются ключами совместимости.

Ordinary b0 содержит MVP/Tint/Model/BaseColor — 160 meaningful bytes внутри прежнего 256-byte draw allocation. B4 — receive flag + ordinary MR/model, b5 — camera position. VS передаёт material parameters в PS как nointerpolation; instanced VS получает их из VB. Root signature: **64 DWORD** (`2+1+12+12+28+1+4+4`). Reflection проверяет Lighting/Shadow/Material/Camera buffer sizes, исключает per-draw CB0/NormalCB из instance contract; PSO failure сохраняет ordinary fallback.

Camera constants записываются в command list для каждого draw, без перезаписи shared mapped camera buffer. Instance pages, frame fences, retired GPU resources и reuse upload между shadow/main сохранены. Для 501 экземпляра upload=112224 bytes, main=1, shadow=1; payload вырос на 16.7% относительно 192-byte layout.

## 15. Shading Model selection

Model сохраняется в asset v3 и optional entity override. В Material Editor есть global comparison: Material setting / Force Lambert / Force PBR. Это диагностический override, он не переписывает assets. CLI: `--pbr`, `--lambert`. Совместимые Lambert и PBR instances могут находиться в одном draw.

## 16. ImGui

Добавлены model combo, metallic/roughness sliders и explicit override controls. Existing Save/Revert, dirty guard, asset sharing, slot assignment и undo сохранены. Material/light ColorEdit отображает sRGB, сохраняет linear RGB; tint остаётся sRGB. Изменения применяются на следующем render frame без shader compilation. Play/Stop не сбрасывает runtime settings; Clear/Restart удаляет/создаёт entities, сохраняя global comparison. Entity parameters сериализуются вместе с обычными material overrides.

## 17. PBR demo

`--pbr-demo` и кнопка PBR spheres demo создают 15 сфер: metallic по строкам 0/0.5/1, roughness по столбцам 0.1/0.3/0.5/0.7/0.9. Есть floor, Directional Light и текущие dynamic shadows. `models/pbr_sphere.obj` — статический unit sphere asset с outward analytic normals, 32×64 tessellation; проходит обычный Assimp/ResourceManager путь. Существующий glTF lighting sphere содержит запечённый nonuniform scale и для наглядной сетки менее удобен.

Визуально проверены `out/diagnostics/pbr/editor-{pbr,lambert,unlit,imported,stress}.png` и `sphere-comparison.png`. Roughness расширяет specular lobe; metallic убирает diffuse. Grid в editor perspective имеет круглые сферы. Readback sphere projection учитывает aspect ratio 320/240. OBJ/glTF slots и stress scene также запускаются с PBR.

## 18. Regression results

Полный CTest: **12/12 PASS**. Также явно запущены DX12 варианты PbrCore, Lighting, GPU Instancing, Material и Shadow Mapping; Shadow Quality проходит в CTest. Артефакты: `ctest-final.log`, отдельные executable logs, `validation-run.log`.

PbrCoreRegression проверяет v1/v2/v3, disk Save/Load и failed save preservation, finite/range validation, overrides/scene roundtrip, independent BRDF/transfer oracle, imported OBJ/glTF slots без преждевременного MR import, mixed 500 batching, ordinary/instance payload equality, interpolation pose и normal/tangent perpendicularity, отсутствие изменения physical position, cast flag, shared editing/reload, missing texture fallback, shader instance fallback, create/delete/clear/restart.

MaterialSystemRegression дополнительно подтверждает actual shared asset reload/dirty guard, slot priority, receive/double-sided states, scene persistence и GPU PBR imported slots. Existing physics, resource, fixed/interpolation, instancing и shadow regressions не удалены и не ослаблены. PBR lifecycle application run завершён, Interpolation OFF run завершён; physics/job/timestep/interpolation implementation не менялась.

## 19. GPU readback

Реальный DX12, Debug Layer: **0 ERROR/CORRUPTION**. Проверены MR endpoints/intermediate, r=0/1, m=0/1, изменение light/camera, degenerate camera vector, NaN/INF input sanitation, rotation, nonuniform/negative/extreme scale, tint/texture/factor, ambient-only/direct-only/zero intensity, Lambert/Lighting OFF, отдельный unlit shader, ordinary/instance bitwise equality, mixed 501 instances, shadowed/unshadowed/receive OFF и hard/PCF.

Analytic RGB oracle в double проверяет 15 pixel locations в каждом простом сценарии с допуском **2 UNORM levels**. Shader implementation не используется как CPU oracle; Smith считается другой формой, transfer — отдельными double functions. Texture filtering oracle ожидает 188 для 50% black/white. Shader reload/recreation, resize 400×280, destroy-before-submit и shutdown проходят с нулём validation errors.

Finite MR sanitation выполняется **в VS до raster interpolators**, дополнительно в PS. Это исправляет обнаруженный readback крайний случай nonfinite varyings. Используется IEEE bit classification; параметр файла с NaN/INF отклоняется ещё CPU validation.

Loaded metallic sphere, m=1: при r=0.2 площадь `R>32` **1044**, peak **255**; при r=0.8 площадь **19656**, peak **96**. Диэлектрик: peak 255 → 121. Проверяется broadArea > 2*sharpArea; diagnostic PPM/PNG сохранены. Это подтверждает распределение блика, а не только compilation.

## 20. CPU/GPU performance

Все четыре размера: paused physics, одинаковые camera/geometry/material factors, Instancing ON, shadows ON/PCF/2048. Четыре 300-frame блока чередуют Lambert/PBR; 100 warmup frames. CSV содержит 1200 samples. Summary исключает первые 30 frames каждого блока для переходов/timestamp lag: **540 samples/model/count**. GPU query values читаются только после completed fence; gpuValid=1 во всех samples. Main=1, Shadow=1, instances=count+1 во всех режимах.

| Cubes | Model | CPU frame p50/p95/p99, ms | Main GPU p50/p95/p99, ms | Shadow GPU p50, ms | Upload, bytes |
|---:|---|---:|---:|---:|---:|
| 100 | Lambert | 1.5441 / 2.0408 / 2.3968 | 0.0082 / 0.0143 / 0.0143 | 0.0143 | 22624 |
| 100 | PBR | 1.8454 / 3.9360 / 4.4309 | 0.0138 / 0.0205 / 0.0205 | 0.0200 | 22624 |
| 250 | Lambert | 6.2401 / 7.3690 / 7.8244 | 0.0174 / 0.0297 / 0.0303 | 0.0256 | 56224 |
| 250 | PBR | 4.9957 / 8.2122 / 9.7055 | 0.0271 / 0.0399 / 0.0420 | 0.0328 | 56224 |
| 500 | Lambert | 6.7691 / 9.5702 / 12.4177 | 0.0384 / 0.0543 / 0.0580 | 0.0558 | 112224 |
| 500 | PBR | 6.6548 / 10.0237 / 12.5380 | 0.0584 / 0.0614 / 0.0758 | 0.0707 | 112224 |
| 1000 | Lambert | 11.1389 / 15.3455 / 16.8244 | 0.0763 / 0.0984 / 0.1204 | 0.1055 | 224224 |
| 1000 | PBR | 10.4355 / 15.1974 / 20.4755 | 0.1096 / 0.1219 / 0.1557 | 0.1229 | 224224 |


| Cubes | Model | Gather / Prepare / Batch / Submit p50, ms | GPU frame p50 / p95 / p99, ms |
|---:|---|---:|---:|
| 100 | Lambert | 0.3646 / 0.0096 / 0.0038 / 0.0042 | 0.0450 / 0.0758 / 0.0819 |
| 100 | PBR | 0.3716 / 0.0100 / 0.0045 / 0.0052 | 0.0727 / 0.0963 / 0.1014 |
| 250 | Lambert | 0.9039 / 0.0256 / 0.0150 / 0.0073 | 0.0850 / 0.1352 / 0.1430 |
| 250 | PBR | 1.6721 / 0.0400 / 0.0195 / 0.0083 | 0.1080 / 0.3462 / 0.4362 |
| 500 | Lambert | 3.4547 / 0.0799 / 0.0460 / 0.0087 | 0.1633 / 0.5101 / 2.3867 |
| 500 | PBR | 3.3070 / 0.0793 / 0.0442 / 0.0080 | 0.2734 / 0.4250 / 1.3019 |
| 1000 | Lambert | 7.0000 / 0.1596 / 0.0983 / 0.0098 | 0.3845 / 1.6664 / 3.0459 |
| 1000 | PBR | 6.8548 / 0.1587 / 0.0977 / 0.0093 | 0.4352 / 2.4914 / 4.3449 |


Все p50/p95/p99 для Gather/Prepare/Batch/Submit/CPU frame/Main/Shadow/GPU frame/upload/draws: `out/diagnostics/pbr/performance.csv`; raw: `perf-{100,250,500,1000}.csv`. Lighting/geometry/physics parameters между model modes не изменяются.

Main GPU p50 растёт во всех четырёх сравнениях. На 500: Lambert 0.0384 ms, PBR 0.0584 ms. CPU frame иногда ниже в PBR — **это не доказательство CPU ускорения**, его нельзя приписать pixel shader. Shadow GPU также колеблется при неизменённом depth shader. Частоты/power/background load не фиксировались; результаты показывают данный запуск мобильной GPU, а не изолированный benchmark BRDF. GPU Frame включает editor/UI и waits/transitions вне Main.

Tracy: `pbr-500.tracy`, 1303 frames, span 8.26 s, 692302 zones, 2578.48 KB. Export CPU means за 1300 render frames: Gather **2.577 ms**, Prepare **0.0662 ms**, BatchBuild **0.0404 ms**, Submit **0.00819 ms**, ShadowPass **0.04895 ms**. Trace overhead отличается от uncaptured runs; эти CPU зоны не являются GPU measurements. Экспорты `tracy-{gather,prepare,batch,submit,shadow}.csv`.

## 21. Изменённые файлы

Относительно PBR backup изменены:

- `CMakeLists.txt`, `WhispEngine.cpp`.
- `engine/core/Application.{cpp,h}`, `RenderDiagnosticSample.h`.
- `engine/ecs/systems/RenderSystem.{cpp,h}`.
- `engine/editor/EditorLayer.cpp`.
- `engine/render/IRenderAdapter.h`, `Lighting.h` (color contract comment), `RenderInstanceData.h`.
- `engine/render/backends/dx12/Dx12RenderAdapter.{cpp,h}`.
- `engine/resources/MaterialResource.h`, `MaterialSerialization.h`, `loaders/MaterialLoader.cpp`.
- `engine/shaders/dx12/textured.hlsl`.
- `tests/LightingRegression.cpp`, `MaterialSystemRegression.cpp`, `ShadowMappingRegression.cpp`, `ShadowQualityRegression.cpp` — новый color oracle/новые PBR проверки.

Добавлены `engine/render/ColorPipeline.h`, `engine/assets/models/pbr_sphere.obj`, `tests/PbrCoreRegression.cpp`, `RunPbrDiagnostics.ps1`, `SummarizePbrDiagnostics.py`, этот отчёт. 41 из 62 прежних dirty/new files сохранён byte-for-byte; остальные 21 изменены в перечисленном scope. Mesh/Texture loaders, ResourceManager, SceneSerializer, прежние reports/assets, GPU Instancing regression source и shadow projection/depth implementation сохранены.

## 22. Команды воспроизведения

Из корня репозитория, PowerShell с MSVC x64 environment. Использована Visual Studio 2022 Community CMake/Ninja. Для существующего подготовленного build:

```powershell
$cmake='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && chcp 65001 > nul && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out/build/shadows-dx12 --parallel 8'
& $ctest --test-dir out/build/shadows-dx12 --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File tests/RunPbrDiagnostics.ps1
& 'C:\Users\Тимофей\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' tests/SummarizePbrDiagnostics.py out/diagnostics/pbr
```

Configure при использовании уже загруженных dependencies (из MSVC developer shell):

```powershell
& $cmake -S . -B out/build/shadows-dx12 -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DENABLE_DX12=ON -DENABLE_VULKAN=OFF `
  -DENABLE_TRACY=ON -DWHISP_BUILD_PHYSICS_TESTS=ON `
  "-DFETCHCONTENT_SOURCE_DIR_ASSIMP=$PWD/out/build/x64-release/_deps/assimp-src" `
  "-DFETCHCONTENT_SOURCE_DIR_GLFW=$PWD/out/build/x64-release/_deps/glfw-src" `
  "-DFETCHCONTENT_SOURCE_DIR_STB=$PWD/out/build/x64-release/_deps/stb-src" `
  "-DFETCHCONTENT_SOURCE_DIR_TRACY=$PWD/out/build/x64-release/_deps/tracy-src" `
  "-DFETCHCONTENT_SOURCE_DIR_ENKITS=$PWD/out/build/x64-release/_deps/enkits-src"
```

Отдельные проверки/демонстрация:

```powershell
Push-Location out/build/shadows-dx12
$env:WHISP_DX12_DEBUG='1'
./PbrCoreRegression.exe --dx12
./LightingRegression.exe --dx12
./GpuInstancingRegression.exe --dx12
./MaterialSystemRegression.exe --dx12
./ShadowMappingRegression.exe --dx12
./ShadowQualityRegression.exe
./WhispEngine.exe --pbr-demo
./WhispEngine.exe --pbr-demo --lambert
./WhispEngine.exe --pbr-demo --no-lighting
./WhispEngine.exe --material-demo --pbr --frames 160
Pop-Location
```

`RunPbrDiagnostics.ps1` имеет `-SkipRegression`, `-SkipBenchmarks`, `-SkipCapture`. Без capture tools использовать `-SkipCapture`. Скрипт проверяет exit codes, batching, gpuValid и validation logs. В managed sandbox atomic material tests требуют canonical asset-root доступа; этот запуск выполнен с разрешённой локальной эскалацией, без изменения security validation.

```powershell
./out/diagnostics/tracy-tools/tracy-csvexport.exe -f RenderGather ./out/diagnostics/pbr/pbr-500.tracy
./out/diagnostics/tracy-tools/tracy-csvexport.exe -f RenderPrepare ./out/diagnostics/pbr/pbr-500.tracy
./out/diagnostics/tracy-tools/tracy-csvexport.exe -f RenderBatchBuild ./out/diagnostics/pbr/pbr-500.tracy
./out/diagnostics/tracy-tools/tracy-csvexport.exe -f RenderSubmit ./out/diagnostics/pbr/pbr-500.tracy
```

## 23. Ограничения и следующий этап

Нет IBL/reflection probes/environment maps, HDR/tone mapping, normal/MR/AO/emissive textures, дополнительных источников света или CSM. Masked/Transparent остаются неподдерживаемыми. DoubleSided сохраняет существующие culling semantics и authored normals; отдельная новая модель backside lighting не введена. Vulkan backend/shaders не изменены, Vulkan runtime/build не запускались.

Single-mip manual color filtering выбран для нынешнего backend. Для PBR Textures следующий отдельный этап должен ввести texture semantics/sRGB views, linear MR/normal sampling, glTF MR import, tangent basis/normal maps и соответствующие независимые readback tests. Root signature уже занимает 64 DWORD: расширять её простым добавлением constants нельзя; потребуется перенести крупный Shadow/frame block в CBV или другое компактное binding решение с прежней fence safety. Сейчас это только план, дополнительные PBR textures автоматически не реализованы.
