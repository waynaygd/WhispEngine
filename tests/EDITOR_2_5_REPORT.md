# Editor 2.5 — Stability, Transform Precision, Asset Workflow & Lighting Fixes

15 октября 2026, DX12-only, ветка `job_system`. Исходный HEAD:
`dc6ef17d5f9dbf4ef48aa23f127166db2fe9e2c5`.
Baseline — сохранённое **dirty состояние Editor 2.4**, включая пользовательские Hospital
parts, а не только этот commit. Commit/push/merge/PR, Vulkan, Lua/sol2 и Prefab Runtime
не выполнялись. Job System, Physics и Fixed Timestep не менялись.

## 1. Аудит и статусы замечаний

Прочитаны восемь заданных reports; hashes/sections/relevant paragraphs сохранены в
`out/diagnostics/editor25/report-audit.json`. Проверены Inspector/Transactions/Assets,
Catalog/Import/InputRouting/LightGizmos, World/ComponentRegistry, MaterialResource,
ResourceManager/loaders/GltfSceneImport, RenderSystem/DX12/shadows и scene settings/serializer.
До правок сохранены status/branch/HEAD, 681 существующий файл и hashes в `before-source.zip`
и `before-sha256.json`, а также INI/preferences. Финальный preservation audit приложен ниже.

| Замечание | Root cause | Изменение и проверка | Remaining limitations | Status |
|---|---|---|---|---|
| Подмена материалов | CPU fallback копировал чужой GPU handle; invalid base-texture bind оставлял предыдущую текстуру. Дополнительно stale render cache после Unload и key-token race при повторной async загрузке. | Ownership fix, явное очищение binding, cache epoch, guarded completion; два before repro и randomized pixel oracles. | Seeded synthetic coverage и actual Hospital parts; нативная пользовательская последовательность не воспроизводилась. | Fixed — подтверждённые причины |
| Свет через стены | Camera-fitted directional Z-range не учитывал удалённый upstream caster. Spot contribution вообще не имеет occlusion term/map. | Extend caster depth при XY overlap; сохранить world fade/receiver bias. Внекамерный тонкий потолок: 231 → 0. | Directional map остаётся одной ограниченной областью; Spot/Point shadows отсутствуют; ambient/IBL не имеют room occlusion. | Partially Fixed; Spot Shadows Deferred |
| Precision и radians UI | Vector drawer использовал `%.3g`; rotation напрямую показывала radians; узкие поля обрезали значащие знаки. | Compact/Precise/Full, 9 significant digits, responsive stacked XYZ, degrees только на UI boundary. Ctrl-click/Enter/Escape, float/JSON roundtrip. | IEEE float ULP, Euler gimbal lock, canonical angles после gizmo. | Fixed |
| Rotation 90° | Не было отдельного rotation snap/action; простая запись одного Euler component неверна для Local/World composition. | 15/30/45/90/Custom, единый rotation snap value в toolbar/Inspector, ±90° с R*Q / Q*R, одна Undo. | World nonuniform scale/shear не добавлены; integer quarter-turn actions canonicalize Euler. | Fixed |
| Folder navigation | Плоский recursive список, смешанные native backslashes/forward keys и CP1251 parent strings. | Cached tree/current folder/breadcrumbs/up/back/forward/search scopes, UTF8 generic directory keys. Actual Unicode folder regression. | Маленький dock может требовать resize/scroll; search byte-wise case folding не Unicode linguistic search. | Fixed |
| Import destination | Browse безусловно выбирал `type/imported/stem`. | Начало операции фиксирует current browser directory; editable Target Folder + Package; полный путь до Scan. | Несовместимый type/root требует явного выбора suggested directory; import синхронен. | Fixed |
| Белый Sun Disc | Binary smoothstep заменял background одной константой высокой radiance без внутреннего профиля. | Limb falloff, corona, elevation tint/attenuation, отдельные glow radius/intensity, bounded SDR helper; HDR сохраняет scene tone mapper. | Artist approximation; extreme exposure / HDR tone OFF могут клиппировать. | Fixed |
| Spot Gizmo за стеной | Early return при occluded origin скрывал весь selected cone/handles; только global X-ray toggle. | Visible Only / Selected X-Ray default / Always X-Ray; dashed dim hidden segments, endpoint visibility/clipping/picking. Actual wall integration: 0 → 700 yellow pixels. | CPU triangle occlusion opaque geometry; не точная material alpha/depth-SRV policy. | Fixed |
| Physics panel | Безусловный Begin без `p_open`, View entry и preference. | Default OFF, X/View/persistence, explicit `--physics-stress-demo`. Closed panel не открывается сам. | Benchmark инструменты сохранены, они всё ещё заменяют active scene по явному Create/Restart. | Fixed |

## 2. Material binding defect: воспроизведение и ownership

До исправления `ResourceManager::CreateLoadingResource/CreateFallbackResource` и async failure
копировали весь `TextureResource/ShaderResource/MeshResource`, включая `gpuHandle` и version.
Renderer считал handle собственным для нового key. Failed finalization с новым version могла
уничтожить GPU allocation исходного default resource. `BindTexture(0, invalid/stale)` затем
ничего не менял; draw использовал ранее привязанную текстуру другого объекта.

Контрольные before результаты сохранены:

- `material-repro-before.log`: default handle **41**, missing resource handle **41**;
- `material-gpu-repro-before.log`: после уничтожения запрошенной red texture и invalid bind
  center readback был **(0,255,0)** — предыдущая green texture;
- after: missing CPU fallback handle **0**; invalid bind center **(0,0,0)**, draw безопасно
  пропускается. Обычный RenderSystem выдаёт fallback собственный GPU allocation.

`DefaultCpuData<T>` переносит только CPU values и обнуляет GPU identity. `Resource<T>::ReplaceData`
сохраняет **собственное** прежнее GPU allocation/version до renderer retirement; decoder не
передаёт ownership между keys и не теряет старый allocation при reload. Descriptor таблицы
по-прежнему immutable в frame slot; reuse происходит после fence. Invalid base binding явно
очищается, available map mask восстанавливается также на fast cache path.

Unload/Clear увеличивают cache epoch и освобождают pending key ticket. Старое completion,
чей Resource identity уже заменён, не стирает ticket новой загрузки. RenderSystem на смене epoch
retire только исчезнувшие/заменённые GPU owners и отбрасывает stale handles в собственных caches.
Stable Reload продолжает менять data/version того же Resource identity. Jobs/loaders не переписаны.

Shared Material Asset semantics сохранены: edit содержимого shared asset отражается у всех
ссылок; assignment другого key/индивидуального override влияет только на entity. Imported
material arrays копируются в resolved material перед override; submesh material slots сохраняются.
Legacy shader/texture priority, texture sampling, PBR maps/tint и instancing payload 240 B не изменены.

Opt-in diagnostics (`RenderSystem::SetMaterialDiagnostics`, DX12 `SetMaterialBindingTrace`)
пишут entity/generation, mesh/submesh/slot, asset/resolved source, tint/base/surface, texture
identity/version/load state/handle, map handles, frame slot и descriptor-table address.
В обычном editor trace выключен; per-frame JSON/vector recording не выполняется.

## 3. Directional occlusion и Spot capability

RenderGather сохраняет cached mesh local bounds и interpolated world AABB для настоящих
cast-shadow objects. После camera receiver fit рассматриваются все эти casters, включая
находящиеся вне camera frustum. Light-space XY overlap с map обязателен; upstream Z расширяет
near side, не меняя receiver XY coverage/texel snapping. Extension quantized to world unit,
ограничен 4096 units; сохраняются world-space Z fade width и receiver comparison offset.
Global depth/normal/slope bias и ambient не повышались и не уменьшались ради скрытия дефекта.

Depth/main passes сохраняют submesh ranges, cast/receive, double-sided и mirrored winding.
Negative/nonuniform scale проходит GPU ceiling oracle вместе с ordinary/instanced equivalence.
Тест использует реально находящийся на Y=100 тонкий ceiling и нулевой ambient:
нерасширенный fit даёт **231**, исправленный **0** в независимом receiver pixel. До/после
снимки — `directional-unextended/extended`. Original XY matrix rows остаются равны;
CPU regression проверяет world fade/bias invariance.

Это не бесконечные shadows: XY coverage, manual fit, map resolution, single-sided authored
planes, user cast/receive flags и geometry holes остаются существенными. Single-sided plane
с обратной стороны требует корректного winding/Double Sided; это не исправляется ambient/bias.
Actual пять пользовательских Hospital parts читаются без записи; их submeshes проверены.
Набор modular parts не выдаётся за автоматически собранный indoor level.

`textured.hlsl::LocalContribution` вычисляет дистанцию, нормализованный surface-to-light L,
cone `dot(-L, outgoingDirection)`, cos-inner/cos-outer transition и range attenuation.
Ни texture shadow lookup, ни per-spot projection там нет. Equal-angle, range/zero-distance,
multiple lights и Forward+/Full equivalence сохраняют прежние regressions. Подтверждённого
дополнительного Spot cone/sign bug не обнаружено. Directional shadow map не применяется к Spot.

**Spot Shadows не реализованы.** Для безопасного ограниченного этапа требуется:

1. До 4 explicit shadowed spots, stable assignment/priority и capability/budget diagnostics.
2. Perspective light view/projection, отдельные D32 maps и SRVs на frame slot; valid near plane,
   cone FOV, range far plane, caster/receiver/mirror flags и fail-safe unshadowed fallback.
3. Per-light shadow index/data + shader binding, PCF, world/normalized bias contract.
4. Fence retirement, отдельные timestamps и подсчёт shadow draw calls/VRAM.
5. Independent wall/ceiling/negative-scale/readback, beyond-budget и lifetime regressions.

Budget proposal: 4×1024²×4 B×**2 frame slots = 32 MiB** depth textures, либо 8 MiB при 512²,
без учета heaps/retired allocations. GPU time пока **не измерен**, поскольку path отсутствует.
Это отдельный renderer/shader contract, а не использование единственной directional map.
Point cubemap shadows не добавлялись. Ambient/static IBL также не знают room visibility;
их вклад через стены не является доказательством ошибки directional shadow visibility.

## 4. Transform precision, degrees и snap

Compact `%.3f`, Precise `%.6f`, Full Precision `%.9g`; NoRoundToFormat сохраняет float при idle
и drag. Для narrow Inspector XYZ располагаются на отдельных строках; ширина учитывает
precision и DPI. Отображение не обещает различить значения ниже IEEE 754 ULP.

Rotation UI — degrees, underlying `rotation` и scene v4 остаются Euler radians, order Rz*Ry*Rx.
Конверсия пишется только при изменении конкретной оси; idle conversions не меняют ECS.
Негативные и >360° значения допускаются текущим numeric/Transform contract; gizmo decomposition
может привести их к equivalent canonical Euler. Gimbal lock не устранён.

Quarter turn строит exact 0/±1 pure rotation matrix, затем Local **R*Q** или World **Q*R**;
scale/position не участвуют и сохраняются. Signed scales остаются signed; near-zero angle noise
канонизируется. Проверены 1440 sequential quarter turns против исходной model matrix, tolerance
1e-4, и один Undo record на action. Rotate gizmo получает degree snap step. Toolbar/Inspector
используют один `rotationSnapping`; OFF не переопределяется скрытым global Snap. Legacy preference
`snapping=true` мигрирует в rotation ON, если новый optional field отсутствует.

## 5. Asset Browser / Import

AssetCatalog keys/type contracts сохранены. Directory identity теперь `generic_u8string`:
UTF8 + `/` на Windows; filesystem directory и parent-from-file дают одну строку. Это устраняет
дубликаты separator/encoding, кроме замены плоского отображения.

AssetNavigation строит child/count index только при catalog revision. Left tree использует
leaf names, expand/collapse и selected highlight; right content — непосредственные files/children
current folder. Back/Forward отбрасывает forward tail при новой навигации. Up, breadcrumbs,
double-click folders, current-folder/project search и Locate selected используют этот state.
Filesystem scan/decode/load не выполняются каждый frame. Toolbar/breadcrumbs объединены для
сохранения пространства content; маленький dock по-прежнему можно resize/scroll.

Import operation захватывает выбранную папку при нажатии Import. Browse обновляет source,
package name/dependency root; уже выбранный destination не заменяет. Root имеет explicit suggested
type destination, а несовместимый current directory показывает сообщение/кнопку — изменение
происходит только по нажатию. Target Folder и package редактируются, итоговый path виден до Scan.
Dependency-aware glTF/GLB staging/security/atomic publish/no overwrite/rollback не изменены.

## 6. Sun visualization и X-Ray

Sun shader остаётся частью SceneBackground/DayNight/SunSynchronization. Limb профиль
`.35 + .65*sqrt(1-r²)`, derivative-smoothed edge, exponential corona, elevation-dependent warm
tint/attenuation и independent glow radius/intensity. Он не создаёт direct light или новую
environment-lighting систему. Visible Disc OFF отключает disc + corona; художественный sunset
background glow остаётся частью Day/Night sky.

SDR helper ограничивает видимую radiance до display encoding; HDR отдаёт linear radiance
существующему output tone mapper. Проверены SDR/HDR, Reinhard, exposure −2/0/+2 и FXAA OFF/Low/High,
limb/corona toggles и typical non-clipped center. Extreme exposure или выключенный HDR tone mapping
могут клиппировать высокую radiance, как остальной color pipeline. Это не физическая атмосфера.
Root constants остаются **60 DWORD + descriptor table**, новые controls используют reserved fields.
Scene glow fields optional; старые scenes загружаются с defaults.

Selected X-Ray — default preference. Видимые segments обычные; occluded segments — dim dashed.
Selected visible projected endpoints интерактивны даже за wall; Visible Only скрывает их и
не создаёт InvisibleButton. Unselected occluded icons не перехватывают mesh clicks, даже при
Always X-Ray display. Near/far/viewport clipping сохранены, Game/Play overlays скрыты. Bounds
metadata cached by weak Resource identity/version (cap 4096); resource lifetime не продлевается.
Actual wall integration: **0 → 700 yellow pixels**, выбранный range handle захватывает/release,
Visible Only handle не захватывает. Это programmatic ImGui/GLFW input, не native walkthrough.

Physics Stress panel имеет X, View entry, preference и default OFF; explicit
`--physics-stress-demo` создаёт 500 cubes и открывает panel. Закрытие не влияет на DockSpace
или наличие benchmark функций; закрытое состояние сохраняется.

## 7. Regression / GPU verification

`tests/Editor25Regression.cpp` добавляет CPU и DX12 cases, не ослабляя старые suites:

- exact float strings −2.9501/−2.95001/−2.950001/−2.95002/scientific, actual Ctrl-click/Enter/Escape,
  idle stability, degrees/radians, repeated quarter rotations, scene JSON roundtrip и Undo/Redo;
- real cached Unicode directory identity, history/search scope/current import target, unsafe target;
- preferences/panel visibility, 10 placement API deliveries/Undo/Redo, Edit/Play/Stop;
- material seed **0x25c0ffee (633405422)**, **80 sequences**, warm/cold textures, repeated assignments,
  stable reload and unload/new identity, **12 in-flight reload races**, multislot/shared vs overrides;
- independent white/blue unchanged-object pixel oracles, requested red/green/blue texture oracle,
  actual compatible instancing + ordinary equivalence, frames in flight and resource retirement;
- directional upstream thin ceiling, mirrored/nonuniform and ordinary/instanced, world fade/bias;
- limb/corona/non-clipping/visible OFF/HDR/FXAA, selected X-Ray drawing/picking/clipping/Game;
- unchanged old suites cover PBR/IBL/Forward+, shader fallback, instancing500, resizing, lifecycle,
  source import/flat scene group, scene save/load and interpolation.

Seed/order/material bindings сохраняются **также при failure** в `material-sequence.json`;
raw diagnostics include Resource identities/versions and GPU descriptor addresses. Early warm
frames do not wait for readback; final independent oracle waits for completion. Vulkan не проверялся.
Missing texture / deliberate invalid bind warnings ожидаемы; DX12 ERROR/CORRUPTION — отдельная проверка.

Финальный Release build PASS (`build-release-final.log`). **31/31 CTest PASS, 108.80 s**
(`ctest-release-final.log`); прежние tests не ослаблялись. Последние UI/Unicode/snap changes
также включены в этот полный запуск. Все DX12 oracles/lifecycle проверки дали **0 ERROR/CORRUPTION**.

Первый baseline запуск дал 28/29: `MaterialSystemDx12Regression` остановился на CPU `replace save`.
Файл теста/MaterialLoader не менялись; последующие полные и targeted runs проходили. Точная
причина этого однократного atomic-save failure не установлена; он не объявляется исправленным
material-binding bug и не скрыт из baseline logs. Основной P0 подтверждён отдельными repro.

Пять current Hospital parts прочитаны: Wall 4 ranges, ceiling 1, door 1, doorframe 5, floor 2.
Старый Editor24 actual combined-Hospital readback также проходит из сохранённой runtime copy;
для его повторения требуется original 7-node GLB, который пользователь удалил из source
до начала этого этапа. Новые user parts не удалялись/не заменялись.

## 8. Команды воспроизведения

Все команды из repository root, VS x64 Release; cached dependencies существующего build используются
без переустановки. `WHISP_DX12_DEBUG=1` для GPU regressions.

```powershell
$cmake='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$python='C:\Users\Тимофей\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && chcp 65001 > nul && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out/build/shadows-dx12 --parallel 8'
$env:WHISP_DX12_DEBUG='1'
& $ctest --test-dir out/build/shadows-dx12 --output-on-failure
& $ctest --test-dir out/build/shadows-dx12 -R '^Editor25' -V
Remove-Item Env:WHISP_DX12_DEBUG
& $python tests/PrepareEditor25Baseline.py
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && chcp 65001 > nul && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S out/diagnostics/editor25/baseline-source -B out/diagnostics/editor25/baseline-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out/diagnostics/editor25/baseline-build --parallel 8'
Copy-Item out/diagnostics/editor25/baseline-build/EditorBaseline.exe out/diagnostics/editor25/baseline-runtime/EditorBaseline.exe
& ./tests/RunEditor25Diagnostics.ps1 -BuildDirectory out/diagnostics/editor25/baseline-runtime -Executable EditorBaseline.exe -Phase before
& ./tests/RunEditor25Diagnostics.ps1 -Phase after
& ./tests/RunEditor25Diagnostics.ps1 -BuildDirectory out/diagnostics/editor25/baseline-runtime -Executable EditorBaseline.exe -Phase before -Cases @('materials-heavy')
& ./tests/RunEditor25Diagnostics.ps1 -Phase after -Cases @('materials-heavy')
& ./tests/RunEditor25ClockProbe.ps1
& $python tests/SummarizeEditor25Diagnostics.py out/diagnostics/editor25
& ./tests/RunEditor25VisualCaptures.ps1 -BuildDirectory out/diagnostics/editor25/baseline-runtime -Executable EditorBaseline.exe -Phase before -Cases @('editor','day','sunset')
& ./tests/RunEditor25VisualCaptures.ps1 -Phase after -Cases @('editor','day','sunset','night','game')
& ./tests/CaptureEditor25Tracy.ps1
Push-Location out/build/shadows-dx12
.\WhispEngine.exe --primitive-demo --daynight-demo 20 --aa-quality 2 --scene-background-panel
.\WhispEngine.exe --physics-stress-demo
Pop-Location
```

Initial configure, если build cache отсутствует (в VS x64 environment):

```powershell
& $cmake -S . -B out/build/shadows-dx12 -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_DX12=ON -DENABLE_VULKAN=OFF -DENABLE_TRACY=ON -DWHISP_BUILD_PHYSICS_TESTS=ON
```

Baseline preparer требует сохранённый `before-source.zip` и `before-head.txt`, а также existing
compiled cached dependency libraries. Это archive текущего dirty state; git archive HEAD не
заменяет его. Первый failed build/tool logs также сохранены; final commands не скрывают failures.
Monitor использует `nvidia-smi` read-only и не меняет clocks/power/system settings.

## 9. Ручной UX checklist и ограничения

Нативный walkthrough не выполнялся; programmatic ImGui/GLFW tests не выдаются за него.

- Две стены: Z −2.95001 / −2.95002; Compact/Precise/Full, узкий/широкий Inspector, Ctrl-click,
  scientific input, Enter/Escape, Undo/Redo, Save/Open и DPI 125/150/200%.
- Rotation 0/90/180/270/−90/450°, Local/World, negative scale, Rotate Snap OFF/ON и Custom;
  проверить Move/Scale/Alt+Move и одно Undo на ±90°.
- Browser nested ASCII/Cyrillic folders: arrows, double-click, breadcrumbs, Up, Back/Forward,
  current/project search и Locate. Import в current directory; менять target только явно.
- Десять последовательных импортов/реальных mouse drops; новые textures/materials/slots при
  cold/warm resources; source shared asset edit против individual assignment/override.
- Actual assembled Hospital: directional primary/cast/receive/double-sided/winding, внекамерный
  roof, coverage debug, camera moves; сравнить ambient/IBL OFF с direct shadows.
- Spot за wall: shadowless direct contribution ожидаем; Selected X-Ray, visible/hidden handles,
  pointer capture/release/outside viewport, Game/Play, больших radius/near clip/resize/DPI.
- Sun diameter/glow/intensity/elevation/exposure, Visible OFF, HDR/Reinhard/FXAA; material
  surfaces не затемняются отдельными искусственными поправками.
- Stress panel X/View/restart preferences/default OFF/demo explicit; Scene Save/Reload/Play/Stop.

Single directional region, static IBL/ambient occlusion, unsupported alpha/skeletal semantics,
Cyrillic case-insensitive linguistic search и actual native DND/DPI остаются перечисленными
ограничениями. Spot Shadows — отдельный ограниченный этап, не «исправлено через стены».
После Editor 2.5 остановиться; scripting не начинался.

## 10. Performance / Tracy / measured impact

Широкая серия: Release, 744×411, 100 warmup +1200 frames/case; после каждого 300-frame
переключения отбрасываются первые 30. По 540 samples ON/OFF. Material-heavy — 200 entities,
400 material ranges, 1800 samples; shadow OFF/hard/PCF отдельно, по 270 samples/state.
**48 groups** проверены. GPU values — actual timestamp queries; CPU frame включает Present/fence
и OS scheduling, не является чистой стоимостью pixel shader.

Полные p50/p95/p99 для CPU/UI/Inspector/Browser/Gizmo/Gather/Prepare/Submit/shadow/main/output/frame
находятся в `performance-summary.json`. Ниже p50 ms; -1/-1 означает неразделённое constant state
обычного diagnostic case. Shadow draw calls указаны **отдельно** от main batching.

| Phase | Case | Inst | Shadow/PCF | CPU | UI | Gather | Submit | Shadow GPU | Main GPU | Output GPU | Frame GPU | Main / Shadow draws |
|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| before | 100 | OFF | -1/-1 | 6.920 | 0.140 | 0.377 | 0.054 | 0.068 | 0.147 | 0.000 | 1.367 | [101] / [101] |
| before | 100 | ON | -1/-1 | 6.949 | 0.139 | 0.376 | 0.009 | 0.063 | 0.130 | 0.000 | 0.411 | [1] / [1] |
| before | 1000 | OFF | -1/-1 | 7.400 | 0.191 | 3.778 | 0.405 | 0.115 | 0.258 | 0.000 | 0.614 | [1001] / [1001] |
| before | 1000 | ON | -1/-1 | 6.895 | 0.179 | 3.824 | 0.009 | 0.104 | 0.204 | 0.000 | 0.532 | [1] / [1] |
| before | 500 | OFF | -1/-1 | 6.946 | 0.167 | 1.858 | 0.209 | 0.085 | 0.168 | 0.000 | 0.509 | [501] / [501] |
| before | 500 | ON | -1/-1 | 6.940 | 0.165 | 1.849 | 0.009 | 0.073 | 0.151 | 0.000 | 0.471 | [1] / [1] |
| before | browser-grid | OFF | -1/-1 | 6.917 | 0.210 | 1.827 | 0.208 | 0.076 | 0.168 | 0.000 | 0.503 | [501] / [501] |
| before | browser-grid | ON | -1/-1 | 6.934 | 0.210 | 1.816 | 0.009 | 0.070 | 0.159 | 0.000 | 0.465 | [1] / [1] |
| before | browser-list | OFF | -1/-1 | 6.904 | 0.209 | 2.116 | 0.206 | 0.081 | 0.172 | 0.000 | 0.534 | [501] / [501] |
| before | browser-list | ON | -1/-1 | 6.914 | 0.209 | 1.837 | 0.009 | 0.074 | 0.169 | 0.000 | 0.467 | [1] / [1] |
| before | lights100 | OFF | -1/-1 | 6.927 | 0.386 | 0.417 | 0.054 | 0.000 | 0.287 | 0.030 | 0.595 | [101] / [0] |
| before | lights100 | ON | -1/-1 | 6.942 | 0.382 | 0.415 | 0.012 | 0.000 | 0.259 | 0.028 | 0.534 | [1] / [0] |
| before | lights250 | OFF | -1/-1 | 6.943 | 0.549 | 0.415 | 0.053 | 0.000 | 0.372 | 0.026 | 0.642 | [101] / [0] |
| before | lights250 | ON | -1/-1 | 6.949 | 0.542 | 0.412 | 0.011 | 0.000 | 0.333 | 0.025 | 0.520 | [1] / [0] |
| before | lights500 | OFF | -1/-1 | 6.941 | 0.890 | 0.415 | 0.053 | 0.000 | 0.491 | 0.025 | 0.905 | [101] / [0] |
| before | lights500 | ON | -1/-1 | 6.950 | 0.886 | 0.414 | 0.011 | 0.000 | 0.483 | 0.032 | 0.676 | [1] / [0] |
| before | materials-heavy | OFF | 0/0 | 2.552 | 0.102 | 1.071 | 0.164 | 0.000 | 0.048 | 0.000 | 0.072 | [400] / [0] |
| before | materials-heavy | ON | 0/0 | 2.667 | 0.106 | 1.130 | 0.013 | 0.000 | 0.145 | 0.000 | 0.226 | [4] / [0] |
| before | materials-heavy | OFF | 1/0 | 3.018 | 0.112 | 1.082 | 0.163 | 0.072 | 0.195 | 0.000 | 0.370 | [400] / [400] |
| before | materials-heavy | ON | 1/0 | 2.982 | 0.111 | 1.087 | 0.009 | 0.074 | 0.234 | 0.000 | 0.447 | [4] / [4] |
| before | materials-heavy | OFF | 1/1 | 3.389 | 0.113 | 1.093 | 0.163 | 0.112 | 0.379 | 0.000 | 0.646 | [400] / [400] |
| before | materials-heavy | ON | 1/1 | 3.214 | 0.111 | 1.091 | 0.009 | 0.109 | 0.367 | 0.000 | 0.684 | [4] / [4] |
| before | materials | OFF | -1/-1 | 6.938 | 0.098 | 0.023 | 0.013 | 0.058 | 0.132 | 0.000 | 0.400 | [4] / [4] |
| before | materials | ON | -1/-1 | 6.947 | 0.098 | 0.023 | 0.013 | 0.052 | 0.128 | 0.000 | 0.377 | [4] / [4] |
| after | 100 | OFF | -1/-1 | 2.391 | 0.111 | 0.375 | 0.046 | 0.150 | 0.318 | 0.000 | 0.682 | [101] / [101] |
| after | 100 | ON | -1/-1 | 2.324 | 0.111 | 0.374 | 0.006 | 0.155 | 0.311 | 0.000 | 0.676 | [1] / [1] |
| after | 1000 | OFF | -1/-1 | 7.571 | 0.167 | 3.720 | 0.399 | 0.267 | 0.559 | 0.000 | 1.042 | [1001] / [1001] |
| after | 1000 | ON | -1/-1 | 7.118 | 0.159 | 3.712 | 0.007 | 0.260 | 0.499 | 0.000 | 0.970 | [1] / [1] |
| after | 500 | OFF | -1/-1 | 5.044 | 0.140 | 1.839 | 0.199 | 0.212 | 0.470 | 0.000 | 0.898 | [501] / [501] |
| after | 500 | ON | -1/-1 | 4.949 | 0.140 | 1.833 | 0.007 | 0.207 | 0.443 | 0.000 | 0.862 | [1] / [1] |
| after | browser-grid | OFF | -1/-1 | 5.133 | 0.208 | 1.856 | 0.220 | 0.206 | 0.468 | 0.000 | 0.914 | [501] / [501] |
| after | browser-grid | ON | -1/-1 | 5.082 | 0.209 | 1.852 | 0.007 | 0.208 | 0.467 | 0.000 | 0.919 | [1] / [1] |
| after | browser-list | OFF | -1/-1 | 5.204 | 0.209 | 1.843 | 0.217 | 0.213 | 0.481 | 0.000 | 0.945 | [501] / [501] |
| after | browser-list | ON | -1/-1 | 5.030 | 0.204 | 1.835 | 0.006 | 0.208 | 0.466 | 0.000 | 0.921 | [1] / [1] |
| after | lights100 | OFF | -1/-1 | 3.740 | 0.346 | 0.412 | 0.050 | 0.000 | 0.635 | 0.080 | 1.039 | [101] / [0] |
| after | lights100 | ON | -1/-1 | 3.630 | 0.345 | 0.413 | 0.008 | 0.000 | 0.570 | 0.076 | 0.965 | [1] / [0] |
| after | lights250 | OFF | -1/-1 | 4.186 | 0.508 | 0.415 | 0.050 | 0.000 | 0.858 | 0.069 | 1.257 | [101] / [0] |
| after | lights250 | ON | -1/-1 | 4.119 | 0.509 | 0.415 | 0.008 | 0.000 | 0.799 | 0.068 | 1.199 | [1] / [0] |
| after | lights500 | OFF | -1/-1 | 4.903 | 0.857 | 0.425 | 0.050 | 0.000 | 1.106 | 0.062 | 1.559 | [101] / [0] |
| after | lights500 | ON | -1/-1 | 4.837 | 0.852 | 0.423 | 0.009 | 0.000 | 1.079 | 0.060 | 1.522 | [1] / [0] |
| after | materials-heavy | OFF | 0/0 | 2.855 | 0.114 | 1.172 | 0.165 | 0.000 | 0.169 | 0.000 | 0.241 | [400] / [0] |
| after | materials-heavy | ON | 0/0 | 2.724 | 0.115 | 1.155 | 0.013 | 0.000 | 0.161 | 0.000 | 0.255 | [4] / [0] |
| after | materials-heavy | OFF | 1/0 | 3.178 | 0.113 | 1.117 | 0.162 | 0.083 | 0.304 | 0.000 | 0.511 | [400] / [400] |
| after | materials-heavy | ON | 1/0 | 3.005 | 0.112 | 1.130 | 0.008 | 0.075 | 0.257 | 0.000 | 0.500 | [4] / [4] |
| after | materials-heavy | OFF | 1/1 | 3.434 | 0.119 | 1.122 | 0.163 | 0.125 | 0.401 | 0.000 | 0.701 | [400] / [400] |
| after | materials-heavy | ON | 1/1 | 3.258 | 0.112 | 1.126 | 0.008 | 0.119 | 0.391 | 0.000 | 0.729 | [4] / [4] |
| after | materials | OFF | -1/-1 | 1.738 | 0.076 | 0.020 | 0.009 | 0.096 | 0.279 | 0.000 | 0.528 | [4] / [4] |
| after | materials | ON | -1/-1 | 1.738 | 0.078 | 0.021 | 0.009 | 0.096 | 0.275 | 0.000 | 0.520 | [4] / [4] |

Первую широкую before/after серию нельзя трактовать как чистую regression delta: before executable
`EditorBaseline.exe` был paced примерно 144 Hz, after — другой. Config files byte-identical,
Present(0,0) одинаковый. Отдельный matched-name (`WhispEngine.exe`) clock probe сделан дважды.
Первый дал CPU/GPU-frame около +0.161/+0.115 ms; второй дал противоположную малую дельту.
Clocks/power наблюдались read-only, не фиксировались. Причина первоначального pacing не
изолирована до конкретной driver/DWM настройки; устойчивое ускорение/регрессию не заявляем.

| Последний clock probe: 500 cubes, instancing ON | Before p50 ms | After p50 ms |
|---|---:|---:|
| CPU frame | 4.829 | 4.668 |
| Gather | 1.827 | 1.845 |
| Submit | 0.008 | 0.007 |
| Shadow GPU | 0.165 | 0.139 |
| Main GPU | 0.415 | 0.315 |
| GPU frame | 0.805 | 0.681 |

P0 graphics clocks во время обоих probe: **2370 MHz**; samples/power/temperature сохранены
в `before-clock-clocks.csv` / `after-clock-clocks.csv`, summary — `clock-probe-summary.json`.
Observer имеет одинаковый polling overhead. Это два bounded measurements, не статистическое
доказательство изменения GPU shader cost. Дополнительный caster coverage может увеличить
shadow fill; число draws остаётся прежним. FPS-clocking confound не приписывается shader.

500 cubes: **1 main +1 shadow draw ON**, **501 +501 OFF**. На material-heavy: **4 main +4 shadow
ON**, **400 +400 OFF** при shadow enabled. Spot shadow passes/VRAM cost сейчас **0** — capability
отсутствует. Объекты/physics settings не менялись ради показателей.

Reload latency: cached **1×1 TGA fixture**, 27 synchronous reload samples:
CPU p50 **0.525 ms**, max **0.702 ms**.
80 request-to-verified-pixels sequences: p50 **7.985 ms**,
max **12.373 ms**; включает warmup и ordinary/instanced verification frames, GPU upload/fence/readback,
это не чистый disk/decode/GPU timestamp и не latency большой Hospital модели.
Source data/order доступны в `material-sequence.json` / `reload-latency-summary.json`.

Browser cases имеют 1000 indexed owned fixture files; до — old recursive All Assets,
после — current-root explorer с child folders. Это разные UX content sets, поэтому frame
сравнение не выдаётся за speedup при показе тех же 1000 file cards. Target resolution одинаковый.
Light gizmo cost добавлен отдельным `lightGizmoMs`; before field отсутствует (n/a).

Tracy: финальный `aa500.tracy` **3,919,657 B**,
1303 frames, 9.73 s, **1,392,914 zones**. `RenderGather`, `RenderPrepare`, `RenderSubmit`,
`HdrOutput` — по 1300 calls. CSV — `tracy-zones.csv`. Capture подтверждает instrumentation;
его CPU zones не подменяют GPU timestamps и не используются как paired timing series.

## 11. Before/after GPU captures

Все PPM — actual GPU readback; PNG — lossless format conversion, без генерации/ретуши.
Снимки просмотрены. Одинаковые scene/camera в editor/day pairs; fixed diagnostic dock маленький,
поэтому stacked Inspector требует vertical scroll, а Browser можно расширить для дерева/content.

[Editor24 baseline](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/before-editor.png)

[Editor25 / precise fields + navigation](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/after-editor.png)

[Original flat solar disc](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/before-day.png)

[Limb/corona solar disc](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/after-day.png)

[Sunset](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/after-sunset.png)

[Night](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/after-night.png)

[Game View](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/after-game.png)

[Large SDR sun: limb and corona](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/sun-sdr.png)

[HDR/Reinhard sun](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/sun-hdr.png)

[Independent texture/white actor oracles](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/material-stress-after.png)

[Unextended receiver fit](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/directional-unextended.png)

[Off-camera caster included](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/directional-extended.png)

[Selected Spot visible through wall](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/spot-selected-xray.png)

## 12. Preservation и изменённые файлы

`preservation-final.json`: branch/HEAD прежние, **681 initial files присутствуют**, initial
user assets hashes не изменены, старые reports/tests byte-identical. Physics/jobs/interpolation,
loaders/GltfSceneImport и SceneSerializer source не изменены. Initial Editor24 dirty edits сохранены.
INI/preferences возвращены byte-for-byte после diagnostics. Общий runtime test snapshot восстановлен
из pre-final validation backup; этот backup был сделан после initial baseline CTest, не заявляется
как полный первоначальный runtime snapshot. Owned fixture directories очищены; out artifacts сохранены.
Пользовательские Hospital source deletion/new parts не откатывались.

Изменения **этого** этапа относительно сохранённого dirty baseline:

- `CMakeLists.txt`
- `WhispEngine.cpp`
- `engine/core/Application.cpp`
- `engine/core/RenderDiagnosticSample.h`
- `engine/ecs/systems/RenderSystem.cpp`
- `engine/ecs/systems/RenderSystem.h`
- `engine/editor/AssetCatalog.cpp`
- `engine/editor/AssetCatalog.h`
- `engine/editor/EditorAssets.cpp`
- `engine/editor/EditorImportUi.cpp`
- `engine/editor/EditorInspector.cpp`
- `engine/editor/EditorLayer.cpp`
- `engine/editor/EditorLayer.h`
- `engine/editor/EditorPreferences.cpp`
- `engine/editor/EditorPreferences.h`
- `engine/editor/EditorTransactions.cpp`
- `engine/editor/EditorUi.cpp`
- `engine/editor/LightGizmos.cpp`
- `engine/render/EditorVisuals.h`
- `engine/render/Shadows.h`
- `engine/render/backends/dx12/Dx12EditorVisuals.cpp`
- `engine/render/backends/dx12/Dx12RenderAdapter.cpp`
- `engine/render/backends/dx12/Dx12RenderAdapter.h`
- `engine/resources/Resource.h`
- `engine/resources/ResourceManager.h`
- `engine/scene/SceneSettings.cpp`
- `engine/shaders/dx12/editor_visuals.hlsl`
- `engine/editor/AssetNavigation.h`
- `engine/editor/TransformPrecision.h`
- `tests/CaptureEditor25Tracy.ps1`
- `tests/EDITOR_2_5_REPORT.md`
- `tests/Editor25Regression.cpp`
- `tests/MonitorEditor25Gpu.ps1`
- `tests/PrepareEditor25Baseline.py`
- `tests/RunEditor25ClockProbe.ps1`
- `tests/RunEditor25Diagnostics.ps1`
- `tests/RunEditor25VisualCaptures.ps1`
- `tests/SummarizeEditor25Diagnostics.py`

Lua/sol2, ScriptComponent, Prefab Runtime, commit/push/merge/PR не начинались.
