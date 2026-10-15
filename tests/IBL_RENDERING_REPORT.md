# WhispEngine — Image-Based Lighting, DX12

Дата: 2026-10-13. Ветка: `job_system`. Реализация только DirectX 12, Release, Tracy ON.
Commit/push/merge/PR не выполнялись. Vulkan не изменялся, не собирался и не запускался.

## 1. Исходное состояние rendering

Изучены PBR Core, PBR Textures, Material System, Lighting, Shadow Mapping, Shadow Quality и GPU
Instancing reports, ResourceManager, ECS RenderSystem, DX12 adapter, shaders, editor и serializer.
До изменений проверены `git status` и ветка. Все 83 исходных изменённых/новых файла сохранены в
`out/diagnostics/ibl/before-tree.zip`; рядом `before.patch`, `before-status.txt`, `before-sha256.json`.
Контроль сохранности: `preservation-audit.json`: 66 исходных файлов byte-identical, 17 расширены,
удалённых нет. Physics, JobSystem, fixed timestep, RenderInterpolation, Vulkan и прежние отчёты
не изменены. Исходный набор: 14/14 CTest PASS (`ctest-baseline.log`).

До IBL PBR использовал direct GGX + простое ambient; final shader выдавал encoded/clamped SDR
в R8G8B8A8_UNORM. Editor viewport также UNORM, ImGui компонует его в SDR swapchain.
MeshVertex: position/normal/UV/tangent, 48 bytes; RenderInstanceData: 240 bytes. OBJ/glTF normals,
tangents, mesh material slots, texture semantic views, interpolation и batching уже существовали.

## 2. Environment Asset и архитектура

`EnvironmentResource` — единый CPU asset: source path, размеры panorama, linear RGBA radiance,
environment cube, diffuse cube, prefiltered cube, shared BRDF LUT, preprocessing time, placeholder.
Статус Loading/Loaded/Failed и version находятся в существующем Resource<T>.
GPU textures и descriptors принадлежат DX12 adapter; ECS не содержит D3D типов.
`Application::m_EnvironmentSettings` — единственный источник настроек, editor редактирует его,
RenderSystem получает указатель. Asset загружается через ResourceManager, не привязан к stress scene.

## 3. HDR decode

Проверенный существующий stb_image декодирует Radiance `.hdr` через `stbi_loadf` в linear float RGBA.
HDR не проходит sRGB decode и не ограничивается единицей. Проверяются формат, размеры,
nonnegative finite values: encoded file ≤64 MiB, panorama ≤16 MP. Другие форматы HDR пока не поддержаны.
Own synthetic assets: `studio.hdr` (sky + локализованный яркий источник), `uniform.hdr` (radiance 2),
`extreme.hdr` (65536/131072/32768). Они созданы для проекта, сторонних лицензий не требуют.

## 4. Panorama → cubemap

CPU worker создаёт cube 64×64×6. D3D face order: +X, −X, +Y, −Y, +Z, −Z;
subresource = face × mipCount + mip. UV: u=atan2(z,x)/(2π)+0.5, v=acos(y)/π.
Panorama bilinear sampling: U wraps, V clamps. Все грани получаются из общего направления в world space.
Runtime yaw применяется обратным вращением N/R перед lookup, без повторной обработки файла.
GPU cube sampling — linear/clamp sampler и аппаратная cube addressing. Это не гарантия отсутствия
всех швов при произвольной карте: разрешение и число samples ограничены.

## 5. Diffuse irradiance

Cosine-weighted Hammersley integration 256 samples/texel, cube 16×16.
Храним E/π, поэтому shader умножает на baseColor, energy split и (1−metallic), **без второго /π**.
Uniform L=2 даёт E/π=2 для каждой ориентации. Аналитическая карта C+direction даёт
E/π=C+(2/3)N: CPU oracle проверяет ориентацию и нормировку (tolerance .008).
Это направленное интегрирование полусферы, не средний цвет всей panorama.

## 6. Specular prefilter

Cube 64×64, 7 mip levels до 1×1. Mip roughness=mip/(mipCount−1).
Mip 0 хранит sharp environment; остальные получают GGX convolution, 256 samples/texel,
N=V=R, radiance weighted by NdotL / sum(NdotL). Shader выбирает LOD=roughness×6.
Математический PBR core по-прежнему ограничивает roughness минимумом .04.
Рост roughness увеличивает пространственную ширину отражения и уменьшает peak.

## 7. GGX sampling

Hammersley radical inverse + GGX half-vector importance sampling, alpha=roughness²;
L=reflect(−V,H). Последовательность детерминирована, basis и zero vectors защищены.
Это split-sum approximation: N=V assumption в prefilter ограничивает точность grazing reflections;
нет multi-scattering compensation. Подход и ограничения сопоставлены с
[Karis, Real Shading in Unreal Engine 4](https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf).
Наша LUT использует точный separable Smith текущего PBR core, а не Schlick-k approximation.

## 8. BRDF integration LUT

64×64 RG32_FLOAT, axes NdotV / roughness, texel-center sampling, 512 samples.
Интегрируются коэффициенты A/B для F0×A+B, Fresnel-Schlick и exact separable Smith GGX.
CPU LUT shared через once_flag, GPU LUT shared внутри adapter между environment reload/recreation.
Стоимость ограничена; постоянного дискового cache нет.
Независимый oracle интегрирует полный BRDF по uniform hemisphere 256×512 quadrature,
с другой реализацией Smith через lambda и другой sampling sequence. Контрольные NV/roughness
(.25,.35), (.6,.6), (.95,1) сравниваются с high-sample LUT integration (tolerance .003).
Проверены finite edges, GPU float texels и dielectric specular против full-BRDF oracle.

## 9. HDR linear color handling

Environment/irradiance/prefilter textures и scene targets: **R32G32B32A32_FLOAT**.
LUT: R32G32_FLOAT. FP32 выбран для независимого float readback и сохранения Radiance HDR значений
выше предела FP16 (65504), включая extreme fixture. Цена — увеличенная bandwidth/memory.
Scene target существует отдельно для каждого fenced frame slot. Main PSMainHdr возвращает
linear radiance без output clamp. Base textures продолжают использовать sRGB semantic SRV;
Normal/MR/AO остаются linear data. Base/tint factors декодируются по прежнему контракту.
Nonfinite final arithmetic защищена; SDR clamp выполняется только в output pass.

## 10. Output, exposure и tone mapping

IBL Enabled переводит main pass в HDR target. Fullscreen triangle преобразует его в существующий
UNORM viewport/backbuffer: exposure 2^EV → Reinhard c/(1+c), если включён → clamp SDR → exact sRGB
transfer **один раз**. Tone off оставляет exposure + SDR clamp + sRGB, яркое изображение ожидаемо клиппируется.
ImGui рисуется после output в прежнем SDR target; UI colors не получают exposure/tone.
Shadow debug encoded colors предварительно декодируются, output bypasses exposure/tone и сохраняет их.
IBL OFF возвращает прежний SDR shader/ambient path; exposure/tone в этом режиме не применяются.
Если IBL enabled, но asset ещё не готов, indirect использует старый ambient fallback с HDR output.
Это SDR renderer с внутренним HDR, не HDR10 swapchain.

## 11. DX12 bindings и PSO

Исходные root slots 0–7 не переупорядочены. Добавлены slot 8: SRV table t5 environment, t6 irradiance,
t7 prefilter, t8 LUT; slot 9: b6, 8 DWORD Environment constants (enabled/diffuse/specular/intensity/
cosYaw/sinYaw/maxMip/reserved). Root footprint **30 → 39 DWORD**, ниже лимита 64.
Добавлен s2 linear clamp; material s0 и shadow s1 сохранены.
Reflection проверяет b6 size=32/space=0, texture dimensions/registers/count и instanced globals.
Существующие ordinary/instance, mirrored/double-sided PSO дополнены HDR-форматами с тем же layout.
`HDR_SCENE_V1` shader имеет PSMainHdr. Legacy VSOut shader получает decode wrapper.
Для прочих VS signatures построен безопасный HDR unlit pixel fallback, сохраняющий полный input
signature, включая SV_Position и indices: проверена linkage и instancing. Произвольные custom PS effects
в этом fallback не воспроизводятся; для них нужен явный PSMainHdr contract. Старый LDR shader сохранён.

## 12. Material System

PBR indirect:

```text
F = F0 + (1−F0)(1−NdotV)^5
diffuse = (1−F)(1−metallic) * baseColor * irradianceEOverPi
specular = prefiltered(reflect(−V,N), roughness*maxMip) * (F0*A+B)
indirect = (diffuse + specular) * environmentIntensity * AO
finalLinear = directBRDF * shadowVisibility + indirect
```

Metal F0=baseColor; dielectric F0=.04. Normal-mapped world N применяется для direct и IBL,
geometric N остаётся для shadow bias. AO strength/texture умножают только indirect, один раз.
Scalar AO для specular — простая approximation, без directional specular occlusion.
Lambert сохраняет прежнее ambient/direct поведение; unlit сохраняет base color; при HDR enabled
их radiance проходит общий output. Material assets v4/overrides/slots не меняются.
CPU decode external/embedded glTF и UV1 fallback сохранены; OBJ и glTF slots проверены при IBL ON.

## 13. GPU Instancing

Per-instance model/MVP/normal/tint/base/surface/map parameters не расширялись: stride остаётся 240 bytes.
Inverse-transpose normal matrix и tangent determinant работают через прежние ordinary/instance paths.
Никаких environment resources на объект и никаких IBL параметров в batch key.
500 cubes + floor: 501 instances, **1 main + 1 shadow draw**, один общий instance upload
501×240=120240 bytes, плюс **1 fullscreen output draw** при IBL ON.
Ordinary path: 501 main + 501 shadow + 1 output. Normal/instanced readbacks bit-identical.

## 14. Shadow Mapping

Алгоритм теней, bias/PCF/projection не изменены. Dx12Shadows только начинает/заканчивает HDR main target.
Новый ECS readback реконструирует в float линейном пространстве:
lit = indirect + (unshadowed−indirect)×visibility, tolerance .004.
Проверены более 20 shadow receivers с ненулевым IBL: тени не выключают окружающее освещение.
AO не влияет на direct. Старые ShadowMapping/ShadowQuality suites остаются частью регрессий.

## 15. ImGui и настройки

Lighting → Environment / IBL: enabled, relative asset path, intensity, yaw, exposure,
Reinhard enabled, diffuse/specular, Reload HDR; Loading/Ready/Failed и last-good status,
64/16/7/64 sizes, GPU MiB, CPU preprocessing и upload wall time.
Intensity/rotation/exposure меняют constants без reread/rebuild. Asset switch запускает async request;
watcher и Reload используют тот же cache/version flow. Path traversal/absolute keys запрещены.
Настройки session-local, переживают Play/Stop/Restart/Clear. SceneSerializer пока их не сохраняет,
как допускает ТЗ. Environment/LUT отдельной UI preview и isolated IBL GPU timer нет.

## 16. Демонстрация

`--ibl-demo` использует существующую mapped PBR sphere grid: metallic 0…1, roughness .1….9,
цветные материалы и normal/MR/AO maps, external/embedded glTF. Directional первоначально OFF,
ambient=0, IBL ON: металлы остаются видимыми только за счёт environment.
Lighting UI позволяет включать direct/shadows. `studio.hdr` имеет локальный яркий участок,
поэтому yaw и roughness дают видимое изменение reflection. Автоматические Editor captures:
`out/diagnostics/ibl/editor-{full,off,diffuse,specular,rotated,no-normal,no-tone}.ppm`.
PNG/contact sheets создаются из сохранённых GPU captures для просмотра.
Все семь captures выполнены с Debug Layer ON и просмотрены: `editor-contact.png`, `editor-full.png`.
OFF при direct/ambient=0 даёт ожидаемо чёрные материалы; diffuse-only не освещает металлы,
specular-only сохраняет отражения; yaw 90° меняет highlights, normal mapping меняет их структуру,
tone off сильнее клиппирует яркие участки. External/embedded glTF в demo сохраняют текстуры.

## 17. Regression results

Финальная сборка PASS (`build-final.log`, затем `build-resize-test.log`).
**16/16 CTest PASS, 59.65 s**, `out/diagnostics/ibl/ctest-final.log`.
DX12 GPU suites проверяют **0 ERROR/CORRUPTION**, включая расширенный IBL readback.
Дополнительные `LightingRegression --dx12` и `GpuInstancingRegression --dx12` PASS:
`lighting-final.log`, `instancing-final.log`. Публичный engine lifecycle scenario с IBL/PBR:
**744 frames**, 9 phases (InitialDrop/Stopped/Resumed/Serial/Parallel/Restart/StoppedAfterRestart/
Cleared/Recreated), exit 0; environment-settings equality guard проходит все переходы.
Логи/CSV: `lifecycle.log`, `lifecycle.csv`. Demo runs exit 0, ERROR/CORRUPTION в логах отсутствуют.
Промежуточный resize-test failure был вызван наследованным mesh range в тестовом helper;
helper теперь явно сбрасывает range перед quad. Это исправление не меняло runtime renderer.
CPU IblRegression: decode >1 и >FP16, corrupt/missing/unsafe path, cached identity,
async Loading→Ready/Failed, same-handle reload, last-good, pending ClearAll и shutdown;
face orientation, analytic irradiance, uniform all mips, independent Smith BRDF, finite bounds.
GPU IblDx12Regression: environment/LUT float readback, output oracle, metal/dielectric, AO/direct,
rotation, roughness/view/normal map, negative/nonuniform scale, ordinary/instance, legacy fallback,
destroy-before-submit, recreation, viewport equality, models/slots, 500 batch, shadow separation,
texture/material reload, entity delete/Clear, resize и shutdown.
Существующие Physics/Resource/Interpolation/Material/Lighting/Instancing/PBR/Shadows suites сохранены.

## 18. DX12 GPU readback

ReadBackFloatTexture читает RGBA32F/RG32F с footprint/row-pitch handling и transitions туда/обратно.
`ReadBackHdrPixels` снимает линейный HDR scene до output, `ReadBackEnvironment` — cube/mip/LUT;
SDR и viewport readback остаются отдельными. Все вызовы тестов выполняются до Present.
Клинические captures: `out/build/shadows-dx12/ibl-readback/*.ppm`, `linear-hdr.f32` (320×240×4 float).
Sharp/broad localized HDR lobe: peak **40.7851 → .660457**, half-height area **9896 → 46080**
для roughness .1 → .9. Это пространственная проверка отражения, а не только среднего цвета.
Uniform 2 читается без clamp, LUT uploaded float texels совпадают с CPU payload.
Каждая face/mip направленной карты C+direction прочитана с GPU и точно совпала с CPU payload,
который отдельно проверен аналитическим orientation oracle.
Dielectric diffuse (.5 base, F0 .04) ожидается .96; specular сверяется с independent quadrature.
Reinhard/sRGB/exposure oracle tolerance 2/255, HDR shadow oracle .004.

## 19. CPU/GPU performance

Hardware: Intel Core i5-13500H (12 cores / 16 threads), NVIDIA RTX 4060 Laptop GPU,
driver 32.0.15.8157. Полная таблица 32 groups находится в
`out/diagnostics/ibl/performance.csv` и `performance.md`.
Каждый size×instancing run: 2400 CSV frames, 4 режима по 300 frames, два цикла.
После environment GPU Ready ещё 100 warmup frames; первые 30 каждого блока исключены:
**540 steady samples/group**, nearest-rank p50/p95/p99. Physics paused существующим benchmark
сценарием, её параметры не менялись. Maps/shadows и камера постоянны, Debug Layer OFF для измерений.
Regressions/captures с Debug Layer ON выполняются отдельно.

Main GPU timestamps измеряют scene main pass; HDR output находится после его end timestamp.
Frame GPU включает scene/shadow/output/ImGui, но не Present. CPU Frame — существующая duration
main loop до Present. Submit и GPU shader cost различаются; CPU fluctuations не приписываются PS.
Diffuse/Spec/Full сравнение с OFF включает смену SDR→FP32 HDR и дополнительный output pass,
поэтому это стоимость всей IBL pipeline, а не изолированных texture fetches.
Новые Tracy zones: IblPreprocessCPU, IblUploadGPU (CPU upload scope), HdrSceneBegin, HdrOutput;
capture `out/diagnostics/ibl/ibl-500.tracy`. Отдельный GPU preprocessing отсутствует: вся convolution
работает на CPU worker, uploadMs — CPU wall duration bounded synchronous upload+queue wait.
Capture успешно сохранён: **2556 frames, 17.9 s, 1,364,077 zones, 4,973,281 bytes**;
`tracy-capture.log`, `trace-500.csv`, `trace-500.log`. Измерения в performance.csv относятся к runs
без подключённого Tracy capture; trace используется для анализа scopes, не смешивается с таблицей.

| Cubes | Inst. | Mode | CPU frame p50 | Gather | Prepare | Submit | Main GPU | Shadow GPU | Frame GPU |
|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 100 | 1 | OFF | 1.7755 | 0.3881 | 0.0098 | 0.0111 | 0.0154 | 0.0154 | 0.0635 |
| 100 | 1 | Diffuse | 1.8040 | 0.3867 | 0.0097 | 0.0106 | 0.0154 | 0.0287 | 0.0768 |
| 100 | 1 | Specular | 1.9203 | 0.3894 | 0.0099 | 0.0114 | 0.0276 | 0.0379 | 0.1341 |
| 100 | 1 | Full | 1.9026 | 0.3870 | 0.0098 | 0.0109 | 0.0297 | 0.0399 | 0.1608 |
| 100 | 0 | OFF | 1.8202 | 0.3870 | 0.0098 | 0.0488 | 0.0143 | 0.0143 | 0.0625 |
| 100 | 0 | Diffuse | 2.1636 | 0.3937 | 0.0099 | 0.0560 | 0.0276 | 0.0338 | 0.1321 |
| 100 | 0 | Specular | 2.0273 | 0.3900 | 0.0099 | 0.0556 | 0.0276 | 0.0369 | 0.1393 |
| 100 | 0 | Full | 2.7447 | 0.4776 | 0.0104 | 0.0682 | 0.0276 | 0.0338 | 0.1393 |
| 250 | 1 | OFF | 3.4827 | 0.9898 | 0.0246 | 0.0179 | 0.0532 | 0.0512 | 0.2007 |
| 250 | 1 | Diffuse | 3.8083 | 1.0079 | 0.0250 | 0.0191 | 0.0563 | 0.0584 | 0.3041 |
| 250 | 1 | Specular | 3.3423 | 0.9802 | 0.0243 | 0.0171 | 0.0584 | 0.0594 | 0.2816 |
| 250 | 1 | Full | 3.4551 | 0.9837 | 0.0246 | 0.0173 | 0.0604 | 0.0604 | 0.2929 |
| 250 | 0 | OFF | 4.7363 | 1.9168 | 0.0395 | 0.2387 | 0.0246 | 0.0174 | 0.0686 |
| 250 | 0 | Diffuse | 3.6674 | 1.2408 | 0.0262 | 0.1321 | 0.0399 | 0.0389 | 0.1075 |
| 250 | 0 | Specular | 4.7143 | 1.3580 | 0.0306 | 0.1468 | 0.0369 | 0.0246 | 0.1075 |
| 250 | 0 | Full | 2.8342 | 0.9826 | 0.0241 | 0.1098 | 0.0358 | 0.0225 | 0.0952 |
| 500 | 1 | OFF | 7.6790 | 3.9508 | 0.0816 | 0.0236 | 0.0471 | 0.0543 | 0.1454 |
| 500 | 1 | Diffuse | 7.5904 | 3.9286 | 0.0806 | 0.0234 | 0.0492 | 0.0205 | 0.1085 |
| 500 | 1 | Specular | 7.4825 | 3.9433 | 0.0807 | 0.0228 | 0.1034 | 0.0901 | 0.3625 |
| 500 | 1 | Full | 7.5547 | 3.9388 | 0.0805 | 0.0239 | 0.0461 | 0.0236 | 0.1147 |
| 500 | 0 | OFF | 8.6079 | 3.9377 | 0.0796 | 0.4856 | 0.1075 | 0.0952 | 0.3717 |
| 500 | 0 | Diffuse | 9.6922 | 4.1918 | 0.0814 | 0.5334 | 0.1065 | 0.1085 | 0.6605 |
| 500 | 0 | Specular | 9.8875 | 4.1853 | 0.0823 | 0.5368 | 0.1126 | 0.1167 | 0.6758 |
| 500 | 0 | Full | 9.8158 | 4.1756 | 0.0819 | 0.5373 | 0.1116 | 0.1044 | 0.6769 |
| 1000 | 1 | OFF | 13.1544 | 8.3784 | 0.1616 | 0.0253 | 0.1300 | 0.0942 | 0.4434 |
| 1000 | 1 | Diffuse | 13.3978 | 8.3886 | 0.1618 | 0.0253 | 0.1526 | 0.1096 | 0.6093 |
| 1000 | 1 | Specular | 13.3410 | 8.2738 | 0.1605 | 0.0248 | 0.1331 | 0.0993 | 0.5396 |
| 1000 | 1 | Full | 13.3699 | 8.2729 | 0.1609 | 0.0252 | 0.1198 | 0.0911 | 0.5151 |
| 1000 | 0 | OFF | 7.4914 | 3.9691 | 0.1109 | 0.3419 | 0.1546 | 0.1137 | 0.4280 |
| 1000 | 0 | Diffuse | 8.0522 | 3.9990 | 0.1112 | 0.3802 | 0.1700 | 0.1372 | 0.5714 |
| 1000 | 0 | Specular | 12.9750 | 7.5717 | 0.1568 | 0.9934 | 0.2673 | 0.2007 | 0.9001 |
| 1000 | 0 | Full | 8.4584 | 4.0519 | 0.1124 | 0.3862 | 0.1772 | 0.1567 | 0.6646 |

Full IBL tails, ms (все прочие p50/p95/p99 также в CSV):

| Cubes | Inst. | CPU frame p50/p95/p99 | GPU frame p50/p95/p99 |
|---:|---:|---|---|
| 100 | 1 | 1.9026 / 2.9598 / 4.1512 | 0.1608 / 0.4076 / 0.5693 |
| 100 | 0 | 2.7447 / 5.4776 / 7.1705 | 0.1393 / 0.1546 / 0.1608 |
| 250 | 1 | 3.4551 / 5.8568 / 7.2400 | 0.2929 / 0.5949 / 0.7250 |
| 250 | 0 | 2.8342 / 4.2618 / 6.9351 | 0.0952 / 0.1495 / 0.1546 |
| 500 | 1 | 7.5547 / 11.3300 / 17.9855 | 0.1147 / 1.9302 / 5.4528 |
| 500 | 0 | 9.8158 / 13.8497 / 17.4793 | 0.6769 / 2.8938 / 4.1144 |
| 1000 | 1 | 13.3699 / 17.5937 / 21.4621 | 0.5151 / 2.3470 / 3.8410 |
| 1000 | 0 | 8.4584 / 18.4289 / 23.8726 | 0.6646 / 3.2348 / 6.7174 |

В этих коротких laptop runs времена не монотонны: clocks/power scheduling, CPU gather/material
resolution и tail spikes заметнее небольшой разницы IBL shader. Не заявляем ускорения от IBL
или точной isolated shader overhead по разнице медиан. Для 500 Full Submit p50: .0239 ms
instanced против .5373 ms ordinary; draw reduction подтверждён независимо от timings.

CPU preprocessing/upload wall ranges между runs: environmentPreprocessMs=590.910…1280.981 ms, environmentUploadMs=2.812…6.278 ms.

## 20. Память

Environment GPU allocation диагностируется через GetResourceAllocationInfo: **1114112 bytes,
1.0625 MiB**, включая shared LUT. Это environment allocation, не вся renderer memory.
Logical texel payload: cube 393216, irradiance 24576, prefilter 524256, LUT 32768 bytes.
HDR scene target дополнительно 16×width×height bytes на каждый из двух frame slots, плюс allocation
alignment; при 1280×720 logical total 28.125 MiB. Upload staging временный; CPU хранит decoded
panorama и processed cubes до cache release. Reload временно увеличивает retained resources.
Для 500 Full CSV: frameConstantUploadBytes=768, materialDescriptors=12 в обоих путях,
instance upload=120240 bytes ON; ordinary draw-CB upload=128256 bytes OFF.
Environment table: 4 descriptors/frame table; materialDescriptors в CSV — прежние material table
copies, **не полный SRV heap usage**. Output добавляет один descriptor. Table копируется при изменении
generation, не на каждый object; frame arena сбрасывается только после fence.

## 21. Resource lifetime

CPU loading/preprocessing на существующих jobs; main-thread finalization проверяет cache identity,
поэтому ClearAll не принимает старую pending completion. Failed reload сохраняет последнее CPU data
и последнее готовое GPU окружение. Первый missing asset использует ambient fallback.
RenderSystem запоминает requested key/version и failed GPU version: ошибки не запускают allocation
каждый кадр. GPU upload подготавливает отдельный набор resources/descriptors и заменяет old только
после успеха. Recorded-but-not-submitted textures/heaps/PSOs удерживаются frame retirement vectors.
Existing frame fences защищают descriptor/CB/instance pages; old HDR targets/RTV heap также retiring.
Resize пересоздаёт HDR targets по размерам active viewport/backbuffer. Shutdown ждёт GPU и чистит
cached LUT/targets/environment. Env source settings и ресурс handles не дублируются в editor.

## 22. Изменённые файлы этого этапа

Расширены: root/engine CMakeLists, WhispEngine.cpp; Application.h/.cpp, RenderDiagnosticSample.h;
RenderSystem.h/.cpp; EditorLayer.cpp; IRenderAdapter.h, RenderInstanceData.h (только statistics);
Dx12RenderAdapter.h/.cpp, Dx12Shadows.cpp (только HDR pass hooks); ResourceManager.h/.cpp;
textured.hlsl. Полный preservation manifest: `out/diagnostics/ibl/preservation-audit.json`.
Новые: Environment.h, EnvironmentResource.h, loaders/EnvironmentLoader.h/.cpp,
dx12/Dx12Environment.cpp, assets/environments/{studio,uniform,extreme}.hdr,
tests/IblRegression.cpp, RunIblDiagnostics.ps1, SummarizeIblDiagnostics.py, этот отчёт.
Не изменены MeshLoader, TextureLoader, MaterialLoader, SceneSerializer, PhysicsStressScene,
RenderInterpolation, jobs, physics, Vulkan, textured_unlit.hlsl, shadow_depth.hlsl и прежние tests/reports.

## 23. Воспроизведение

Из корня репозитория, Developer PowerShell/Command Prompt VS 2022. `cmake`/`ctest` из VS CMake tools.
Текущая конфигурация `out/build/shadows-dx12`, Ninja, Release; CMakeCache фиксирует DX12 ON,
Vulkan OFF, Tracy ON. Кириллица showIncludes требует code page 65001 перед build.

```powershell
chcp 65001
cmake -S . -B out/build/shadows-dx12 -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_DX12=ON -DENABLE_VULKAN=OFF -DENABLE_TRACY=ON
cmake --build out/build/shadows-dx12 --parallel 8
ctest --test-dir out/build/shadows-dx12 --output-on-failure
./tests/RunIblDiagnostics.ps1
python ./tests/SummarizeIblDiagnostics.py out/diagnostics/ibl
```

Отдельные runs из build directory:

```powershell
cd out/build/shadows-dx12
$env:WHISP_DX12_DEBUG='1'
./IblRegression.exe --dx12
./LightingRegression.exe --dx12
./GpuInstancingRegression.exe --dx12
./WhispEngine.exe --ibl-demo
./WhispEngine.exe --ibl-demo --environment environments/uniform.hdr --environment-rotation 90 --exposure 1
./WhispEngine.exe --ibl-demo --pbr --stability-scenario ../../diagnostics/ibl/lifecycle.csv
Remove-Item Env:WHISP_DX12_DEBUG
./WhispEngine.exe --ibl-benchmark 500 ../../diagnostics/ibl/perf-500-i1.csv
./WhispEngine.exe --ibl-benchmark 500 ../../diagnostics/ibl/perf-500-i0.csv --no-instancing
```

Diagnostics script умеет SkipRegression/SkipBenchmarks/SkipDemos/SkipCapture; запускает тесты и
benchmark sequentially, проверяет timestamps/batching/counts. Tracy capture tool должен находиться
в `out/diagnostics/tracy-tools/tracy-capture.exe`; script использует localhost и `--wait-tracy`.
При запуске через restricted Windows sandbox canonical filesystem queries для assets могут быть
недоступны: runs выполнены с доступом к локальным asset paths, без ослабления path validation.

## 24. Ограничения

CPU precomputation, runtime cache без persistent disk cache; fixed small cube resolutions/samples.
Bounded main-thread GPU upload может дать единичный короткий stall при смене asset; production async
copy/preprocessed disk asset — возможный следующий шаг. No skybox, environment UI preview,
local probes/parallax/SSR/RT/dynamic GI/multi-bounce, material extensions, HDR10, Vulkan IBL.
IBL — distant static world-space environment; directional shadow не occludes environment.
Specular AO — scalar approximation. Lambert не получает physically based IBL.
Float extreme decode/upload сохраняются, но произвольная суммарная radiance за пределами FP32
не представима. Generic custom shader HDR fallback unlit; native PSMainHdr требуется для его effects.
GPU IBL-only и GPU preprocessing time не заявляются. Editor 2.0/scripting не реализованы.

## 25. Editor 2.0 и подготовка к scripting — анализ, без реализации

В EditorLayer уже есть DockSpace, Hierarchy/Inspector, Asset Browser, Material/Lighting panels,
scene Save/Load, transform gizmos и undo snapshots selected entity. Это основа, но component UI
и serialization перечисляют типы вручную, полноценного metadata registry нет. Undo snapshots
не являются общей транзакционной историей всех scene/resource операций. Layout persistence следует
проверить и формализовать отдельно; существование DockSpace само по себе не гарантирует UX contract.

World уже использует Entity(index,generation) и IsAlive. Будущий script API должен передавать такие
references с world identity, каждый доступ валидировать generation и запрещать долговечные raw
component pointers. Add/Remove должны проходить безопасную command queue в определённой phase,
особенно при Play и параллельном чтении ECS. Metadata позволит унифицировать Inspector,
component add/remove, serialization и binding fields без переписывания renderer.

Application сейчас владеет одним World, EditorLayer непосредственно изменяет его; Play переключает
physics/update state, а не создаёт автоматически отдельную копию runtime scene. Требуется определить
editor-world snapshot → runtime-world clone и Stop restore/discard contract, resource sharing и
world-scoped entity references. SceneSerializer должен охватить новые scene settings и script fields.
Inspection/runtime execution/editor editing должны стать отдельными responsibilities.

ТЗ лабораторной №2 не предоставлено в материалах этого этапа. **Язык, runtime и binding library
не выбирались.** До реализации нужно получить фактическое ТЗ и определить ScriptComponent,
asset reference, сериализуемые поля, OnCreate/Start/FixedUpdate/Update/Destroy, события, hot reload,
exceptions/error reporting и Inspector. Fixed/frame phases используются через существующие update
точки, не меняя timestep/jobs; script mutation применяются в безопасных boundaries.
В Edit mode пользовательские scripts по умолчанию не выполняются. Editor tool scripts, если нужны,
потребуют отдельного явного режима и capability policy.

Дорожная карта отдельного следующего этапа:

1. Editor UX foundation: стабильный docking/layout, panels, viewport input/focus, toolbar, scene settings.
2. ECS Inspector и component metadata: registry, безопасные add/remove, generation references, общий Undo.
3. Scene/runtime separation: snapshot/clone/restore, Play/Pause/Stop contract, serialization расширений.
4. Scripting integration только по фактическому ТЗ лабораторной №2: runtime/bindings/lifecycle/fields/events.
5. Script debugging/editor tooling: errors, reload, inspector fields, execution visibility и regression tests.

Эта дорожная карта не выполнялась; дальнейшая работа требует отдельного задания.
