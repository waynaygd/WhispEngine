# P3.1 — Performance Stabilization

Ветка `broken_ps`. Коммит не создавался. Игровые сцены, descriptor проектов, ЛР №3, Job System, алгоритмы света/Fog и форматы сериализации не изменены.

## Причина и исправление

**City:** `TryBuildRenderSnapshot` вызывал `NormalizeAssetKey` для основной текстуры каждого submesh до обращения к уже существующему `m_TextureResources`. В активном проекте это не просто строковая нормализация: `ProjectContext::ResolveAsset → Resolve → CheckNoLinks` проверяет каждый компонент пути через `symlink_status` и Windows `GetFileAttributesW`.

111 submeshes, 12 материалов в исходном GLTF, 12 embedded images. В прогретом кадре 102 проверки base-texture path. В контрольной серии с выбранным Spot на них пришлось **204.991 ms** из **206.538 ms** Material Resolve. По графу вызовов для этого восьмикомпонентного пути — 102 × 8 × 2 = **1632 вызова проверок метаданных/кадр**. Это расчёт числа вызовов API, а не измеренное число физических дисковых чтений. Отдельных `exists` / `weakly_canonical` в этом относительном hot path нет. Загрузки/декодирования текстур в нём также нет.

Разбивка City, median, до → после:

- Весь Material Resolve: 206.538 → 0.226 ms.
- Path validation: 204.991 → 0.014 ms; checks 102 → 0.
- Texture section, включая path validation: 205.182 → 0.040 ms.
- Shader key / lookup / source access: 0.726 → 0.098 ms.
- Остальное: копирование параметров GLTF, overrides и подготовка snapshot. Отдельного Material Resource lookup для исходных inline GLTF materials нет. GPU descriptor bindings выполняются при submission, вне Material Resolve.

`ResourceManager::Get` по-прежнему берёт recursive mutex и нормализует вход. В этом месте основной повторяющийся запрос заменён доступом к уже принадлежащему renderer ресурсу; блокировки Job System не менялись. Строковые преобразования, validation и filesystem time измерены вместе в path section; независимого таймера каждого системного вызова не вводилось.

**Interactive Room исследована отдельно.** В исходной комнате 72 entities, 61 MeshRenderer с одним `cube.obj`, два назначенных material keys (51 + 10 экземпляров), четыре Point Lights, 10 Lua behaviours и четыре prefab instances. В режиме активного проекта обнаружились ещё две повторяющиеся проверки:

- Нормализация Mesh path и назначенного Material path для каждого из 61 объектов перед доступом к renderer cache.
- `DrawLightGizmos`: 61 `ResourceManager::Get<MeshResource>` для одного общего Mesh. Стоимость Edit gizmos до исправления — **201.914 ms**, World Render — **391.571 ms**, из них материалы **186.718 ms**. В Play без gizmos: CPU **144.735 ms**, GPU **2.996 ms**, wall **147.050 ms**, **6.80 FPS**. Низкий FPS воспроизведён на CPU, без добавления Fog/PostFX.

Исправление минимальное: сначала exact-key lookup в существующих Mesh/Texture/Material resource caches; новый ключ проходит прежнюю нормализацию и загрузку. Для gizmos — локальный на один вызов map, один lookup на уникальный Mesh. BVH `MeshOcclusion` не менялся.

**Ownership / invalidation:** нового глобального кэша нет. `GetOrCreateTexture/GetOrUploadMesh` продолжают проверять usability и `gpuHandleVersion`; reload меняет данные/версию прежнего owner. Overrides, slot mapping, tint и material parameters вычисляются каждый кадр, поэтому не устаревают и различаются у экземпляров/prefabs/submeshes. При unload существующий cache epoch обновляет owners и освобождает устаревшие GPU allocations. При смене проекта работает прежний `ReleaseGpuResources`. Frame-local gizmo map исчезает после рисования. Проверки project boundary/reparse points при загрузке/перезагрузке файлов сохранены: повторное использование уже загруженного GPU ресурса не читает файл по непроверенному пути.

## Методика

RTX 4060 Laptop GPU; DX12; 1280×720 render target и viewport во всех сценариях, включая F11; window 1280×720; instancing on; FXAA High (один effective sample); `Present(0,0)`, без engine FPS limiter. Ограничение количества кадров у probe не является ограничением FPS.

City: оригинальный `D:/WhispEngineProjects/ProjectA/Scenes/Main.json`, камера `(0, 1.7, 0)`, yaw/pitch 0, near/far 0.1/2000, тот же GLB, Fog/Lighting/PostFX. Release, Debug Layer on, GBV off, directional shadow map 2048, 111 main и 111 shadow draws. Для таблицы выбран одинаковый selected Spot (`spot_cast_flag`); общий флаг не добавляет отсутствующий в движке Spot shadow pass. Прогрев 24 кадра, 2 transition, 8 samples на фазу. Native editor probe использует отдельные `Run(1)`, поэтому wall/CPU включают обслуживание между этими вызовами; до/после метод одинаков. p95 при восьми samples — максимум серии, оценка грубая.

Room: исходный `Scenes/InteractiveRoom.json`, камера `(-1.85, 1.69, -7.3)`, yaw/pitch 0, активный project asset root `engine/assets`. В исходном descriptor startup указывает на отсутствующий `Scenes/interactive_room_ui.json`; для диагностики ProjectContext создан в памяти с существующей комнатой. Descriptor и scene не переписывались. Это legacy Application initialization с заменой World; bootstrap resource watches остаются и входят в ResourceUpdate. Результаты не следует выдавать за измерение успешного открытия неизменённого исходного descriptor.

24 warmup + 24 samples на сценарий внутри одного `Run(48)`, наблюдатель после Present; сохранение preferences вне измеренного кадра. Nearest-rank median/p95. Для повторяемой physics нагрузки simulation dt фиксирован 1/60, без sleep; FPS вычислен по реальному wall clock, **не** по заголовку окна/логам с тестовым dt. Камера фиксирована тестовым gameplay host, игрок в основных timing series стоит; движение, двери, взаимодействия и FPS controls отдельно проверены native интеграционной регрессией. Это ограничение профиля Play.

CPU = wall минус измеренные slot/Present/fence waits для каждого sample; это CPU-side elapsed time, включающее filesystem stalls и preemption, не process CPU cycles. Frame/physics/Lua/RenderSystem/ImGui/UI-layout замерены отдельно. Вложенные timings не складываются. GPU — DX12 timestamps, а не CPU recording. Readback проверяет completion соответствующего frame fence; существующие Fog/culling queries могут читаться с задержкой, warmup длиннее ring. Для Custom PostFX и Runtime UI добавлены только две пары timestamp slots на pass, с учётом отсутствующих стадий и frame slots.

Частоты CPU/GPU, температура и внешняя нагрузка не фиксировались. Между короткими сериями есть разброс: первый post-fix City probe дал 0.507 ms Material / 18.837 ms wall, финальный — значения ниже. Снижение числа проверок 102 → 0 воспроизводится; весь разброс FPS и GPU time нельзя приписывать кэшу или validation.

## City: до / после

Значения ms — **median / p95**.

| Метрика | До | После | Изменение |
|---|---:|---:|---:|
| Material Resolve CPU, ms | 206.538 / 221.801 | 0.226 / 0.241 | 915.9x меньше median |
| Total CPU без измеренных GPU waits, ms | 221.242 / 239.365 | 5.088 / 6.377 | 43.5x меньше median |
| GPU Frame, ms | 2.840 / 3.172 | 2.126 / 2.152 | 1.3x меньше median |
| Wall Frame, ms | 224.269 / 242.683 | 7.255 / 8.341 | 30.9x меньше median |
| FPS = 1000 / wall median | 4.46 | 137.84 | 30.9x |

GPU алгоритмы и изображение не менялись; изменение GPU time само по себе не доказывает GPU-оптимизацию. Изображение Viewport 1280×720 до/после совпало побайтно: **0 различающихся байтов**. Source project, Main.json, GLB и `.wmesh` совпали по SHA-256 с исходной копией.

## Interactive Room: финальная Release, validation off

Все timings — ms, **median / p95**. В исходной комнате нет Fog, Custom PostFX и Canvas; shadow resolution в settings — 2048, но shadow pass не активен без directional source. Для C в runtime World добавлен существующий PlayerHUD; D/E оставляют уже отсутствующие эффекты выключенными, поэтому их отличия от C — разброс, не ускорение. F отключает только Canvas; G — editor visual switches; H — F11 с реальным существующим capture state, поддерживаемым synthetic input. Fog Medium/32 samples и один VHS pass проверены отдельно временными components.

| Сценарий | CPU median / p95 | GPU median / p95 | Wall median / p95 | FPS |
|---|---:|---:|---:|---:|
| A — Edit | 5.983 / 6.702 | 0.470 / 5.252 | 6.680 / 12.777 | 149.7 |
| B — Play | 4.677 / 5.604 | 0.446 / 0.449 | 5.391 / 6.285 | 185.5 |
| C — Play + HUD | 6.395 / 7.372 | 0.449 / 0.452 | 7.102 / 7.999 | 140.8 |
| D — без Volumetric Fog | 6.349 / 7.325 | 0.449 / 0.457 | 7.151 / 8.036 | 139.8 |
| E — без Custom PostFX | 6.704 / 7.844 | 0.450 / 0.453 | 7.354 / 8.544 | 136.0 |
| F — без Runtime UI | 5.004 / 5.520 | 0.449 / 0.855 | 6.940 / 7.196 | 144.1 |
| G — без overlays | 6.190 / 7.016 | 0.889 / 0.925 | 7.433 / 8.273 | 134.5 |
| H — F11 / Clean Play | 3.717 / 4.096 | 0.938 / 0.940 | 6.933 / 7.168 | 144.2 |
| + VHS (диагностика) | 6.064 / 6.688 | 1.219 / 1.227 | 7.933 / 8.780 | 126.1 |
| + Volumetric Fog (диагностика) | 3.889 / 4.369 | 1.807 / 2.491 | 6.913 / 7.258 | 144.7 |

Исходный Edit в активном проекте: wall **608.267 ms**, **1.64 FPS**. После — **6.680 ms**, **149.7 FPS**. Standalone без активного проекта первоначально работал значительно быстрее; смешивать его с project-mode baseline нельзя.

### CPU stages, median ms

| Стадия | Edit | Play + HUD | Clean Play |
|---|---:|---:|---:|
| ResourceUpdate (poll / finalization / hot reload) | 1.4937 | 1.7061 | 1.7412 |
| Lua Update | 0.0001 | 0.1475 | 0.1532 |
| Fixed simulation, включая Lua FixedUpdate и physics | 0.0020 | 0.0790 | 0.0819 |
| Physics (входит в fixed simulation) | 0.0000 | 0.0673 | 0.0711 |
| ECS / structural commands | 0.0005 | 0.0002 | 0.0002 |
| World frame systems / RenderSystem | 0.3011 | 1.4086 | 1.3585 |
| Material Resolve (часть RenderSystem) | 0.1318 | 0.1321 | 0.1343 |
| Runtime UI build/layout (часть RenderSystem) | 0.0000 | 1.0726 | 1.0703 |
| Runtime UI command recording (CPU) | 0.0000 | 0.0178 | 0.0134 |
| RenderSystem GPU finalization (CPU) | 0.0002 | 0.0001 | 0.0002 |
| Editor UI build, включая gizmos | 3.7716 | 2.4708 | 0.0098 |
| Light Gizmos (часть Editor UI) | 1.1884 | 0.0001 | 0.0001 |
| ImGui command recording (CPU) | 0.0309 | 0.0356 | 0.0086 |
| Present API (CPU wall) | 0.1643 | 0.1909 | 0.1586 |
| BeginFrame slot fence wait | 0.0003 | 0.0004 | 0.0004 |
| Post-Present submitted-frame fence wait | 0.5329 | 0.4550 | 2.9860 |

### GPU stages, median ms

| Стадия | Play + HUD | Clean + Volumetric Fog | Clean + VHS |
|---|---:|---:|---:|
| Frame | 0.4485 | 1.8074 | 1.2186 |
| Shadow | 0.0000 | 0.0000 | 0.0000 |
| Main, включая Fog | 0.1577 | 1.0537 | 0.3912 |
| Light culling | 0.0082 | 0.0184 | 0.0184 |
| Fog (часть Main) | 0.0000 | 0.6697 | 0.0000 |
| HDR / tone mapping / FXAA / built-in output | 0.2324 | 0.6410 | 0.6707 |
| Custom PostFX (часть Output) | 0.0000 | 0.0000 | 0.0287 |
| Runtime UI | 0.0010 | 0.0020 | 0.0020 |
| Final output, включая Custom PostFX | 0.2324 | 0.6410 | 0.7066 |

В комнате нет directional source, поэтому Shadow = 0; его работа проверена отдельной City/shadow regression. Custom входит в Output, Fog входит в Main. Built-in output объединяет HDR/tone mapping/FXAA и экранные эффекты; отдельного timestamp каждого встроенного эффекта нет. HUD остаётся после PostFX.

### Конфигурации: Play + HUD

Отдельные короткие серии; **median / p95**, ms. Debug — /Od /Ob0 /RTC1, debug CRT, без NDEBUG; символы /Z7 вместо /Zi из-за C1902 локального PDB manager, в том числе forced `_CL_=/Z7` для Assimp. Release — /O2. Adapter, сцена, камера, resolution и настройки одинаковы, но неизмеренные частоты/фон могут различаться; особенно GPU delta Debug/Release нельзя целиком объяснять CPU optimization flags.

| Конфигурация | CPU | GPU | Wall | FPS |
|---|---:|---:|---:|---:|
| Release, validation off | 6.395 / 7.372 | 0.449 / 0.452 | 7.102 / 7.999 | 140.8 |
| Release, Debug Layer | 7.137 / 7.810 | 1.678 / 4.413 | 10.315 / 12.793 | 96.9 |
| Release, Debug Layer + GBV | 9.502 / 10.678 | 1.479 / 1.663 | 9.833 / 10.960 | 101.7 |
| Debug /Od, validation off | 11.295 / 12.062 | 2.315 / 4.921 | 14.573 / 18.729 | 68.6 |

Validation остаётся включаемой: `WHISP_DX12_DEBUG=1`; дополнительно `WHISP_DX12_GBV=1` включает GBV **до** CreateDevice. Необходимо удалить переменную для выключения: существующая проверка смотрит её наличие, значение `0` не выключает. GBV используется вместе с Debug Layer.

Для запуска Debug устранены две локальные assertions редактора: основной шрифт теперь явно использует прежний reference size 13px перед Cyrillic merge; geometry copy `ImGuiViewport` для dockspace не копирует backend ownership. Runtime HUD font и rendering не менялись. Эти изменения не выдаются за FPS-оптимизацию.

## Что осталось и дальнейший этап

1. Наиболее заметен **ResourceUpdate**: 1.706 ms median в Play + HUD. `PollHotReload` опрашивает metadata всех watches каждый кадр под mutex; сюда также входят другие resource/config операции. Отдельный этап: измерить watch-only cost, затем ограничить частоту/бюджет polling с сохранением invalidation и dirty-edit guard. Глобальный вечный path cache не вводить.
2. **Синхронизация:** Clean Play CPU 3.717 ms, GPU 0.938 ms, wall 6.933 ms; submitted fence wait 2.986 ms. `MoveToNextFrame` ожидает только что отправленный кадр и ограничивает overlap. Перенос ожидания только на reuse slot требует проверки lifetime uploads, descriptors, retired resources и delayed diagnostics; не менялся в этом исправлении. Наблюдаемые ~144 FPS в Clean Play не доказывают наличие engine FPS limiter; влияние compositor/driver отдельно не измерялось.
3. **Runtime UI CPU layout/build**: 1.073 ms, при GPU UI 0.0010 ms. Возможное отдельное улучшение — кешировать статическую layout часть с dirty/version invalidation, сохраняя динамические значения и resize. В этом задании не переписывалось.

В steady state 14640 B reported uploads/кадр (динамические instance data для 61 объектов), instance buffer growth = 0; GPU-finalization CPU 0.0001 ms. Повторного декодирования GLB/texture или создания material PSO каждый кадр не обнаружено.

Итог: прежние 5–25 FPS воспроизводятся при повторных project path validations, без дорогого Lua/GPU эффекта. После исправления обычный Room Play ограничивается преимущественно CPU-side обслуживанием/editor UI, Clean Play заметно ограничен ожиданием submitted fence. Для обычной разработки — Release + Debug Layer по необходимости; игровые FPS — Release без диагностической validation, корректность — отдельный validation/GBV run. Debug пригоден для пошаговой отладки, его FPS не равен Release. Оставшиеся расходы не отключались ради статистики.

## Проверки и воспроизведение

- Сборки `WhispEngine` в `out/build/x64-release` и `shadows-dx12`; Debug `PerformanceStabilizationProbe` в `x64-debug` прошли.
- 15 выбранных существующих CTest targets прошли по совокупности основного прогона и адресных повторов: ProjectSystem CPU/DX12, MaterialSystem CPU/DX12, Fog CPU/DX12, ShadowMapping CPU/DX12, LightSystem CPU/DX12, ScreenEffects, CustomPostFx, RuntimeUI, InteractiveRoom, SpotLight.
- Последний прогон Material CPU/DX12 + RuntimeUI + InteractiveRoom: **4/4 passed**. Новая регрессия проверяет отсутствие warm path validations, изменение texture version, unload/load owner replacement и pixel parity, восстановление после missing material, реальное редактирование файла и material hot reload.
- Material regression сохраняет GLTF/OBJ slot colors, per-instance overrides, shared asset edits, reload, cast/receive flags, mirrored meshes и instancing. ProjectSystem проверяет project switches/reopen, сцены/save/load и missing resources.
- InteractiveRoom проверяет WASD, Lua doors/switches/events, Play/Pause/Stop, Tab/capture, F11, возврат toolbar и неизменность Editor World. Исходный descriptor ломал только финальный OpenProject check: для полного прогона использована временная копия descriptor/scene в build directory с корректным startup. Пользовательский descriptor сохранён.
- City QA: exact viewport parity до/после, mesh resource-version rebuild, picking, instancing, Play/Pause/Stop, F11; **DX12 ERROR/CORRUPTION = 0**. Финальные Room Debug Layer / GBV runs также проверяют zero validation errors. Debug без validation завершён без asserts.

В directory `out/build/shadows-dx12`:

```powershell
# Short city diagnostic; output path relative to build directory.
$env:WHISP_DX12_DEBUG='1'
./SpotLightRegression.exe --editor 'D:/WhispEngineProjects/ProjectA/ProjectA.whispproject' diagnostics/city.json
# Room: project context only in memory, no source saves.
$env:WHISP_PERF_PROJECT='../../../InteractiveRoom.whispproject'
./PerformanceStabilizationProbe.exe '../../../Scenes/InteractiveRoom.json' diagnostics/room.json
```

Сырые aggregate результаты: `diagnostics/p31-city-before.json`, `p31-city-final.json`, `p31-room-project-before.json`, `p31-final-release.json`, `p31-final-layer.json`, `p31-final-gbv.json`; Debug — `out/build/x64-debug/diagnostics/p31-final-debug.json`. Логи: `p31-tests.log`, `p31-retest.log`, `p31-last-tests.log`, `p31-city-qa.log`. Первый retest исправляет ошибочную ссылку fixture на неиспользуемый material slot; полный Room retest использует описанную owned project copy. Ни один неуспешный прогон не выдан за passed.

## Изменённые файлы

- Hot paths: `engine/ecs/systems/RenderSystem.cpp`, `engine/editor/LightGizmos.cpp`.
- CPU counters / short frame observer: `engine/ecs/systems/RenderSystem.h`, `engine/core/Application.h`, `engine/core/Application.cpp`, `tests/ApplicationTestAccess.h`.
- GPU stage slots / opt-in GBV / font merge fix: `engine/render/Shadows.h`, `engine/render/backends/dx12/Dx12RenderAdapter.h`, `Dx12RenderAdapter.cpp`, `Dx12Shadows.cpp`, `Dx12PostFx.cpp`, `Dx12RuntimeUI.cpp`.
- Debug dockspace copy fix: `engine/editor/EditorWorkspaces.cpp`.
- Diagnostics/regressions: `CMakeLists.txt`, `tests/PerformanceStabilizationProbe.cpp`, `tests/SpotLightRegression.cpp`, `tests/MaterialSystemRegression.cpp`.
- Отчёт: `docs/PerformanceStabilization.md`.
