# P0.3d-A2 — Safe Application World Ownership

Дата: 2026-10-18. Ветка: `project_system`.

**Вердикт: READY для P0.3d-A2.** Окончательные результаты проверок — ниже.
Область — инфраструктура A2. Публичный Hierarchy Editor и hierarchy Play
не включены. A3 не начинался.

## A. Исходная проблема и аудит

Изучены P03D_EDITOR_HIERARCHY_REPORT, P03D_A1_WORLD_MODE_PERSISTENCE_REPORT,
P03D_A1_FINAL_VERIFICATION, P03C_RUNTIME_HIERARCHY_REPORT и непосредственно
связанные Application/Project, World/SystemPipeline, serializer, Editor
transactions/selection, Physics/Render, LuaRuntime и ResourceManager pathways.
Исходный baseline из A1 final report: Release PASS, профиль 16/16,
полный CTest 61/61 (373.25 s), DX12 errors 0. Это исторический baseline,
а не результат A2. Исторический intermittent RMB failure не считается решённым.

Ранее value-member `m_World` одновременно содержал authoring ECS data и
владел registered systems/GPU caches. Immutable DataMode не позволял заменить
RootOnly на HierarchyData через ReplaceData; CloneData не переносит systems.
Простая замена экземпляра потеряла бы systems и оставила session handles.

| Объект | Lifetime и политика A2 |
| --- | --- |
| Authoring ECS storage, components, SceneIdentity | Отдельный `unique_ptr<World>`; candidate сохраняет собственные immutable mode/identity при публикации |
| Runtime ECS storage | Независимый CloneData authoring World; прекращение Lua ownership перед уничтожением |
| PhysicsSystem, RenderSystem, дополнительные registered systems | Постоянный `m_SystemWorld`, существующий World/SystemPipeline используется только как владелец систем; execution target — текущий GetWorld |
| MotionSystem, BoundsBounceSystem | Application их ранее не регистрировал; алгоритмы/standalone World API не менялись. Дополнительная Probe system проверяет общий lifetime contract |
| EventBus, windows, render adapters, JobSystem, ResourceManager | Application lifetime; не заменяются при смене сцены. Project commit сохраняет существующий явный drain/release/clear ресурсов |
| Settings, render pointers на App settings/interpolation | Адреса App members постоянны; candidate переносит значения, настройки lighting-enabled/shadow-debug сохраняются как раньше |
| Physics pairs/contact cache/broadphase, render packets/snapshots/batches | Сбрасываются при публикации; GPU/asset caches сохраняются для смены сцены |
| LuaRuntime и gameplay camera service | App members; Lua Stop до удаления старого World, camera target/player/clone map сбрасываются |
| Selection, active/anchor, Undo/Redo, transaction/group session, inspector drafts | Явный ResetHistory/ResetWorldState; история со старыми handles не переносится |
| Rename/drag handles, hierarchy-visible list, background draft identity | Сбрасываются при публикации |
| Clipboard | JSON component values, а не component pointers; существующая политика сохранена |
| Debug/stress Entity vectors, collision pairs, deferred mutations | Переносятся подготовленные candidate handles либо очищаются; старые команды не применяются к новому identity |
| GetWorld/GetEditorWorld references и component pointers | Borrowed до публикации/смены session; необходимо заново получать после Open/New/Project/Play/Stop. Указатели на удалённый World не продлеваются искусственно |

Entity identity/generation guards сохранены; старый handle отклоняется новым
World. Стабильные SceneIdentity восстанавливаются из документа. Editor history
из другой World identity намеренно не восстанавливается. Production consumers
получают World из Application; долгоживущие внутренние caches очищаются выше.

## B. Минимальное разделение владельцев

Application владеет постоянным `m_SystemWorld` без authoring data и заменяемыми
`m_EditorWorld`/`m_RuntimeWorld`. Существующие Physics/Render регистрируются в
system owner. Все App frame/fixed execution sites вызывают
`UpdateWorldSystems`, который направляет тот же pipeline на GetWorld.
Shutdown очищает systems один раз до уничтожения adapters/resources.
Существующая explicit Legacy demo initialization сохранена.

World, DataMode, ReplaceData/CloneData, SystemPipeline и solver/render algorithms
не изменялись. Нового ECS implementation нет. Public GetWorld/GetEditorWorld
сохранили сигнатуры; добавлены const editor accessor и lifetime comment.
Новые preparation/publication APIs приватны; integration tests вызывают их
через уже существующий `friend ApplicationTestAccess`.

Execution/transition scopes запрещают публикацию внутри system, Lua и game-state
update/render callbacks, live ECS iteration и hierarchy retirement. Play/Stop
также запрещён во время execution, чтобы Stop не удалил runtime под выполняющейся
system. Вложенный Play/Stop из lifecycle callback отклоняется.

## C. Candidate preparation и commit

`PrepareAuthoringScene` читает document и вызывает A1 DetermineWorldMode.
Отсутствующий marker означает RootOnly; `rootOnly`/`hierarchy` выбирают новый
World соответствующего режима. Некорректный marker отклоняется. Relationship
не используется для определения режима. Для v4 RestoreWorld получает тот же
прочитанный JSON. Legacy formats проходят существующий LoadWorld; этот loader
повторно открывает файл, поэтому immutable disk snapshot для legacy не обещается.
Mode mismatch guards остаются действующими.

Candidate validation проверяет persisted SceneIdentity каждого Entity, сериализуемые
компоненты/settings, graph и существующие RequireRuntimeWorld restrictions.
Precomputed entity vectors и scene/config keys готовятся до commit.
RootOnly sun synchronization выполняется на candidate. Hierarchy sun driving
с DirectionalLight явно отклоняется до A3: старый authoring adapter нельзя
безусловно применять к full affine hierarchy. При публикации candidate повторно
валидируется; изменения candidate после подготовки не обходят проверку.

Проверка Project Settings подготавливает только временный scene candidate,
не публикуя World; ей разрешено работать внутри Save-and-continue project
commit. Это отдельный приватный параметр подготовки. Сама publication boundary
проверяется всегда, включая настоящий OpenProject/CloseProject/OpenScene/NewScene.

**До commit:** parsing, mode/settings/component/identity/graph/runtime validation
и пользовательский запрет hierarchy завершаются до Stop, Editor reset,
ProjectManager activation и resource drain/clear. Отказы не меняют author/runtime,
paused session, selection/history/dirty, settings, scene, project и resources.

**Scene commit:** начинается перед Stop/Lua Stop/StateMachine Clear и очисткой
Editor/queue. InstallAuthoringWorld делает nonthrowing swap подготовленного
World и перенос подготовленных значений, сбрасывает world-specific caches.
После этого old author уничтожается. RememberScene/watch initialization —
postcommit steps. Ошибки помечаются `World commit failed (session may have
changed)` либо `Scene commit failed (world published)`.

**Project commit:** начинается перед ResolveProjectEdits, потому что явное Save
может записывать файлы. Затем выполняются существующие lifecycle cleanup,
resource drain/release/clear, Activate context, установка candidate и Editor
ProjectOpened. Отказ Save может оставить уже записанные изменения. Исключения
после границы явно обозначаются `Project commit failed (session may have
changed)`. Полная rollback-гарантия для GPU/device/allocations/preferences не
заявляется. GPU owners при смене authoring World не заменяются.

## Runtime и публичная граница

RootOnly SetEditorPlayMode использует настоящий PrepareRuntimeWorld **до**
изменения history/camera/active session: CloneData с mode/map, runtime validation,
существующий SpawnPlayer и повторная validation. После этого обычный Play commit
публикует candidate, применяет camera/input lifecycle и запускает Lua.
Lua Start остаётся commit-phase: атомарный rollback произвольных Lua callbacks
не заявляется. Stop завершает Lua и возвращает authoring settings/history.

Integration tests вызывают тот же private preparation API для HierarchyData,
проверяя graph, PlayerStart, mode и handle/storage isolation. Тесты не создают
альтернативный runtime. Native test проходит настоящий Application Initialize,
Run, Play/Pause/Resume/Stop и Shutdown; зарегистрированные системы проверяются
на опубликованном hierarchy authoring candidate. Для проверки physics attachment
используется также один детерминированный fixed pipeline step через production
UpdateWorldSystems, без ожидания wall-clock tick и без прямого изменения камеры.

Полный публичный hierarchy Play остаётся запрещён: A3 должен проверить editor
adapters, hierarchy-safe authoring/sun behavior и всю активную hierarchy session.
OpenScene, project startup/settings, New Scene и legacy workflow по-прежнему
RootOnly. Попытка открыть hierarchy через UI получает понятный отказ до commit.
New Scene создаёт пустой RootOnly World, а не конвертирует текущую иерархию.
Tree UI, Reparent, parent-aware Gizmo, subtree duplicate и Collider migration
не реализованы.

## D. Покрытие и сохранение исходных изменений

ApplicationWorldOwnershipRegression проверяет оба режима, repeated preparation,
immutable/cross-mode guards, identity/stable IDs, revalidation/phase/reentrancy
refusals, selection/history/queue cleanup, старые сцены, A→B→A, Legacy,
RootOnly transform/collider raycast, Save marker, реальный Lua start/stop и
paused-session failure preservation. Native вариант проверяет реальные
Physics/Render/ResourceManager instances, resource handles, snapshots,
runtime physics, author isolation, single system destruction и DX12 validation.
Editor2Regression направлен на настоящий App pipeline: прежний прямой вызов
systems на data-only authoring World стал бы no-op. Его assertions сохранены.

Изменено в A2:

- engine/core/Application.h, Application.cpp, ApplicationProject.cpp;
- новый engine/core/ApplicationWorld.cpp;
- engine/editor/EditorLayer.h, engine/ecs/systems/RenderSystem.h;
- engine/CMakeLists.txt и CMakeLists.txt (добавление ownership CPU/DX12 tests);
- tests/Editor2Regression.cpp;
- новый tests/ApplicationWorldOwnershipRegression.cpp;
- этот отчёт.

На входе уже были изменения CMakeLists.txt, LightGizmos.cpp,
SceneSerializer.cpp/.h, LuaGameplayControllerRegression.cpp, SceneWorldModeRegression.cpp
и четыре отчёта A1/A1.1/A1.2/final. Они сохранены, включая исходные assertions
и continuous bounded RMB trace. Snapshot: out/p03da2-start-status.txt и
out/p03da2-start-hashes.json. Пользовательские assets/Lua/scenes/models/EventBus
не входят в изменения A2. Git history/index не изменялись.

## Проверки и остаточные риски

Все логи находятся в локальном игнорируемом out/. Summary и LastTest detail
сохраняются раздельно для каждого прогона. Полный suite запускается один раз,
без until-pass, skip или ослабления assertions.

Промежуточные проверки блоков: A 3/3 (1.90 s), B 4/4 (2.43 s), C 6/6
(3.48 s), соответствующие p03da2-audit-*, p03da2-b-*, p03da2-c-* logs.
Ownership проверка до финальных дополнений: 97 CPU + 20 DX12 checks, 2/2
(4.38 s), p03da2-checked-ownership-{ctest,detail}.log.

Промежуточные неуспешные результаты также сохранены: первоначальный missing
MeshResource include; ожидание async resource без drain; ANSI path в test JSON
на Cyrillic user path; fixture SkyDrivesLight без обязательной sunMetadata;
ожидание physics tick за несколько быстрых кадров. Исправлены include,
test setup/UTF-8 snapshot и детерминированный вызов реального fixed pipeline.
Assertions существующих regression tests не ослаблялись.

Расширенный профиль p03da2-final-profile-{ctest,detail}.log: 22/26 PASS,
133.34 s. Помимо двух новых test setup/timing ошибок обнаружена реальная
регрессия Save-and-continue из-за запрета descriptor-only validation внутри
commit; исправлена отдельно от publication guard. Также воспроизвёлся
исторический `Play without player retains RMB free-fly`.
Полный bounded trace сохранён в этих logs: 20 frames, 23 records,
overflow=0/truncated=0; focused=0, foregroundOwn=0, foregroundPid=17948,
selfPid=9972, firstBlock=native-focus во всех held frames 711–730.
Native RMB/W и captureOwn=1, synthetic RMB становится 1 с frame 712,
viewport hovered=1, ActiveID=0, gizmo=0, camera=0. Успешного кадра activation
в этом trace нет. Это наблюдаемый blocker, не доказанный исторический root cause.
Input/camera code и исходный RMB assertion не менялись.

Исправленный Release build: PASS, out/p03da2-corrected-build.log.
Профиль после исправлений: **7/7 PASS, 25.03 s**, ownership **101 CPU / 20 DX12
checks**, SceneWorldMode **406 checks**, Project Settings CPU/DX12 и Editor2
CPU/DX12. Источники: out/p03da2-corrected-profile.log и
out/p03da2-corrected-detail.log. Изменений исходников после этого прогона нет.

**Единственный полный CTest: 63/63 PASS, 302.09 s**, без повторов.
Источники: out/p03da2-full-ctest.log и out/p03da2-full-detail.log.
Первый shell launch не нашёл ctest вне VS environment и не запускал тесты;
реальное полное выполнение было ровно одно, после загрузки build environment.

| Обязательная проверка в полном прогоне | Результат |
| --- | --- |
| Application ownership CPU / DX12 | 101 / 20 checks PASS |
| SceneWorldMode | 406 checks PASS |
| ECS Hierarchy | 261 checks PASS |
| Runtime Hierarchy CPU / DX12 | 296 / 16 checks PASS |
| Transform Contract | 718 checks PASS |
| Project Core / System CPU / System DX12 | PASS |
| Project Settings CPU / DX12 | 75 / 64 checks PASS |
| MultiSelection CPU / DX12 | 2550 / 395 checks PASS |
| ScalePlaneGizmo | PASS |
| Editor2 / LevelWorkflow / Workflow CPU и DX12 | PASS |
| Lua Gameplay Controller CPU / DX12 | 62 / 226 checks PASS |
| Physics / RenderInterpolation / LuaScripting | PASS |

**DX12 validation: 0 errors.** GetValidationErrorCount()==0 assertions
прошли в native ownership, Project, RuntimeHierarchy, Editor и других полных
regressions. Поиск D3D12 ERROR/CORRUPTION, severity и DX12 validation-failure
markers в corrected-profile/full detail logs дал 0 совпадений. Ожидаемые
asset-loader warnings не приравниваются к GPU validation errors.

**git diff --check: PASS.** Сравнение с исходным snapshot 827 файлов показывает
изменения только в восьми существовавших файлах A2, перечисленных выше;
новые A2 файлы добавлены отдельно. Все 90 protected assets/EventBus совпадают
по SHA-256. Исходные dirty A1 files/reports сохранены; CMakeLists.txt расширен,
исходная регистрация SceneWorldModeRegression сохранена. Index/history не менялись.

## Решение и ограничения

A2 готов: безопасная preflight/publication boundary проверена, systems переживают
замену authoring identity, обязательные RootOnly regressions зелёные. Никаких
новых изменений DataMode/persistence/Collider/HierarchyGraph/input/camera нет.

Один зелёный полный suite не устраняет наблюдавшуюся intermittent RMB проблему:
неуспешный профиль и полный bounded trace сохранены как технический долг.
Сохраняются описанные commit-phase ограничения GPU/allocations/Lua callbacks,
borrowed-reference contract и legacy loader reread. Длительный ручной UI soak
не проводился; проверка основана на CPU и native DX12 integration tests.

Публичное открытие/редактирование hierarchy и полный hierarchy Play остаются
закрыты. Следующий возможный этап — A3 по отдельному заданию; он не выполнялся.
