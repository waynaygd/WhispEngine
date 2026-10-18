# P0.3d-A3 — Safe Hierarchy Activation & Application Play

Дата: 2026-10-18. Ветка `project_system`.
**Вердикт: READY для инфраструктурного этапа P0.3d-A3.**
Область: инфраструктура A3; P0.3d-B/C/D UI не реализованы.

## Аудит и граница доступа

Прочитаны отчёты P03D_EDITOR_HIERARCHY_REPORT,
P03D_A1_WORLD_MODE_PERSISTENCE_REPORT, P03D_A1_FINAL_VERIFICATION,
P03D_A2_APPLICATION_WORLD_OWNERSHIP_REPORT, P03C_RUNTIME_HIERARCHY_REPORT.
Проверены реальные ApplicationWorld/ApplicationProject/Application lifecycle,
SceneSerializer, World/HierarchyGraph/TransformAccess, hierarchy physics geometry,
PlayerStart, LuaRuntime ownership/loader, Editor references, sun settings и lights.

A2 ownership сохранён: постоянный m_SystemWorld владеет Physics/Render,
заменяемые author/runtime Worlds владеют отдельным ECS storage. World mode и
identity/generation guards не менялись. Не вводились другой Play mechanism,
runtime harness, renderer или solver.

Оставшиеся A2 blockers: публичные mode guards, запрет H Play, RootOnly sun
adapter, отсутствие compatibility preparation, проверка Lua prerequisites и
legacy mesh collider auto-fit при Application Run. Они решены локально ниже.

Новые приватные production entry points OpenSceneInternal, OpenProjectInternal,
SaveProjectSettingsInternal и NewSceneInternal используют те же A2 preparation,
publication и project commit. Политика allowHierarchy выбирается явным вызовом
закрытого API. AuthoringCandidate несёт controlledHierarchy; Install фиксирует
его только для HierarchyData. PrepareHierarchyUpgrade также явно вызывается,
а не подключается к открытию старой сцены.

Нет menu/CLI/preferences switch, открывающего этот доступ пользователю.
ApplicationTestAccess вызывает настоящий production API в отдельных tests.
Обычные OpenScene/OpenProject/Startup Settings остаются RootOnly и получают
диагностический отказ для hierarchy до разрушения текущей session.

## Application scene/project lifecycle

| Путь | RootOnly | Контролируемый HierarchyData |
| --- | --- | --- |
| New | Прежний публичный New Scene | Private NewSceneInternal создаёт новый пустой H World |
| Open | Missing marker/explicit rootOnly | Explicit hierarchy через private OpenSceneInternal |
| Save / Save As | Прежние методы | Те же production методы сохраняют hierarchy marker/IDs/Relationship/settings |
| Project Startup / Open | Прежние public paths | Private OpenProjectInternal подготавливает startup в его mode |
| Project Settings Startup | Прежний public validator | Private SaveProjectSettingsInternal валидирует H candidate без publication |
| Close Project | Browser + пустой RootOnly | Тот же lifecycle Stop/drain/reset, возвращение в Browser |
| Legacy Workspace | Явный RootOnly | Не конвертируется |
| Play | Прежний public contract | Реальный SetEditorPlayMode для явно активированной infrastructure session |

DetermineWorldMode/RestoreWorld A1 не менялись: старые сцены без marker остаются
RootOnly, hierarchy требует явного marker v4. Invalid marker/graph не меняют
активную author/runtime session. Публикуется подготовленный экземпляр с новым
World identity; stable SceneIdentity и Relationship сохраняются. Старые handles,
selection/history, transactions, drafts, pending mutations и world caches
очищаются существующей A2 политикой. Systems остаются у постоянного owner.

**Preflight/commit boundary A2 сохранена.** Scene parsing/validation выполняется
до Stop/publication. Project startup проверяется до ResolveProjectEdits,
resource drain/release/clear и context activation. Project commit начинается
перед явным Save, который может писать файлы; scene commit — перед lifecycle
cleanup. Postcommit failures имеют существующую диагностику `session may have
changed` / `world published`. Полный GPU/allocation/Lua callback rollback не
заявляется.

## Консервативный RootOnly → HierarchyData upgrade

PrepareHierarchyUpgrade разрешён только из RootOnly Edit session, создаёт
отдельный H candidate через CaptureWorld/RestoreWorld. Marker меняется только
в копии document в памяти. Source World, scene JSON и пользовательские Collider
не исправляются. Публикация — отдельное явное действие через A2 boundary.

Проверяются оригинальные persisted IDs, все сериализуемые компоненты и settings,
graph/runtime constraints, root world matrices. Для каждого Collider проверяются:
наличие Rigidbody и Transform, ровно unit authored scale и zero offset,
resolved H type/halfExtents/offset/center, матрица ориентации и неизменность всех
serialized Collider/Rigidbody/Trigger/material properties. World matrices и
resolved values сравниваются с относительным допуском 2e-5 для float
composition/decomposition noise. Unit-scale/zero-offset admission и копия
компонентов дают совпадение формы по построению; сравнение — дополнительная
проверка фактического адаптера. Это не проверка несколькими raycast samples.

Итоговый CaptureWorld candidate (кроме mode) должен целиком совпасть с исходным:
никакой скрытой нормализации Transform/Collider или смены primary light tie
selection. Политика консервативна и может отказать совместимому случаю.

| Случай | Решение |
| --- | --- |
| Пустая сцена | Candidate разрешён |
| Visual-only root TRS, включая nonunit scale | Разрешён при graph/runtime validity и world-matrix parity |
| Box/Sphere с Rigidbody, unit scale/zero offset | Разрешены при resolved geometry и serialized-property parity |
| Rotated static/dynamic Collider, Rigidbody Trigger | Та же проверка, без пересчёта свойств |
| Nonunit/negative/zero Collider scale | Отказ с Entity ID и Collider diagnostic |
| Nonzero Collider offset | Отказ: не предполагается, что world offset эквивалентен local offset |
| Nonuniform Sphere / неподдерживаемый dynamic body | Отказ по admission/runtime constraints |
| Collider без Rigidbody | Отказ: hierarchy добавляет immovable solver participation; parity не доказана |
| Mesh Collider с legacy autoFitFromMesh | Отказ: требуется готовая явная local geometry |
| Невалидные IDs/graph/PlayerStart/settings | Preflight failure |

Загрузка RootOnly сцен никогда не вызывает upgrade. Миграция размеров/offset,
сложная compatibility migration и UI подтверждения upgrade не реализованы.
Candidate — приватное подготовленное значение для немедленной явной публикации;
preview workflow с конкурентными edits/rebase candidate не добавлялся.

## Hierarchy-safe sun-driving

Hierarchy PrimarySun использует тот же stable-ID tie order, что LightGather и
persistence. RootOnly selection/formulas сохранены.

SkyDrivesLight и DayNight поддержаны для пути к солнцу с положительными
равномерными scales (допуск 1e-5 для decomposition noise). Здесь checked world
rotation согласуется с authored quaternion rotation chain Runtime LightGather.
Ориентация выставляется через TrySetWorldRotation, который вычисляет local TRS
от parent inverse и проверяет graph; безусловной записи desired Euler в child
Transform нет. После записи проверяется runtime geometry. При отказе восстанавливается
старый local Transform до изменения light properties или Application settings.

SceneSettings в H path сначала разбираются во временные значения; неподдерживаемая
sun operation не оставляет частично применённых settings. Nonuniform/reflected
ancestry, shear/singular world TRS или нарушение descendant physics вызывают
явный отказ. General affine sun orientation остаётся future расширением,
не применяется RootOnly adapter к неподдерживаемому H path.

Тесты проверяют parented uniform-scale sun против реального LightGather,
Save/Load, DayNight properties, отказ nonuniform ancestry и rollback уже
проверенной world rotation, которая вывела descendant Collider за physics range.

## Настоящий Application Play и интеграция

SetEditorPlayMode разрешает H только после controlled authoring activation.
Используется тот же PrepareRuntimeWorld: CloneData с mode/map, runtime validation,
sun validation на candidate, существующий SpawnPlayer, повторная runtime
validation и Lua startup prerequisites. Disposable LuaRuntime использует
существующий loader/VM для проверки файлов, limits, syntax, behaviour table и
schema. Активный LuaRuntime не останавливается и callbacks не запускаются при
этом preflight. Созданный Player controller тоже проходит проверку.

После preflight выполняется прежний Play commit, Lua Start и реальный Run.
Произвольные callbacks и повторное чтение файлов после commit не получают
ложную гарантию атомарности: изменение script file между preflight и Load или
ошибка on_start остаются commit/runtime ошибками существующего Lua contract.
RootOnly Lua loading behavior не ужесточался.

Pause/Resume сохраняют runtime identity/relationships/instances. Stop вызывает
существующий hierarchy Lua lifecycle, снимает ownership hook, уничтожает runtime,
очищает H render snapshots и возвращает authoring data/settings. Повторные
циклы не разделяют mutable storage и не оставляют runtime handles в authoring.

Application Run использует P0.3c affine snapshots/HierarchyGraph, тот же
RenderSystem main/shadow path, hierarchy physics adapter и checked Lua API.
Legacy world-scaled mesh auto-fit отключён для H; H candidates с такой
неподготовленной комбинацией отвергаются до publication. RootOnly auto-fit
и physics semantics сохранены. Legacy config hot reload не подменяет H session.

Ограничения P0.3c сохранены: hierarchy interpolation выключена, dynamic
Rigidbody/Character/Player только root, static Collider под dynamic ancestor
запрещён, Collider shear запрещён, PlayerStart root без детей, dynamic root
Collider unit scale/zero offset. Solver/renderer algorithms не переписывались.

## Публичный UI остаётся закрытым

Для H authoring session EditorLayer не строит старые hierarchy/Inspector/Gizmo,
toolbar, shortcuts и asset mutation controls; editor visual overlays также
не строятся. Run продолжает настоящий scene rendering через existing fallback
render path. Это закрытая infrastructure session, не новый пользовательский UI.
Обычный RootOnly Editor не менял layout/workflow. Никаких Tree/DND/Reparent,
parent-aware Gizmo, subtree duplicate или subtree Editor Undo не добавлено.

## Файлы и проверки

Изменено относительно A2:

- engine/core/Application.h, Application.cpp, ApplicationProject.cpp,
  ApplicationWorld.cpp;
- engine/editor/EditorLayer.cpp, SunSynchronization.h;
- engine/scripting/LuaRuntime.h, LuaRuntime.cpp;
- CMakeLists.txt;
- новый tests/ApplicationHierarchyActivationRegression.cpp;
- этот отчёт.

CPU test проходит реальные New/Open/Save/Project/Startup/Play APIs и явный
upgrade preparation/publication. Native test выполняет настоящий Application
Initialize/Run/Play/Pause/Resume/Stop/Shutdown, PlayerStart, Physics, Lua,
affine rendering, image/shadow depth readback, parent motion queries, subtree
lifecycle, resource-preserving refusal и возврат в RootOnly проект.
Альтернативный runtime harness не создавался.

Все logs находятся в игнорируемом out/. Исторические failed logs сохранены.

| Блок | Результат / источники |
| --- | --- |
| A — scene lifecycle | 3/3 PASS, 1.86 s; p03da3-a-fixed-build.log, p03da3-a-{ctest,detail}.log |
| B — compatibility | 3/3 PASS, 1.46 s; p03da3-b-build.log, p03da3-b-{ctest,detail}.log |
| C — settings/Play CPU | 5/5 PASS, 2.67 s; p03da3-c-build.log, p03da3-c-{ctest,detail}.log; activation 98 checks |
| D — первоначальный CPU/native profile | 6/7 PASS, 11.50 s; p03da3-d-{ctest,detail}.log; CPU activation 102 checks, native capture setup failure |
| Исправленный native capture path | 1/1 PASS, 4.48 s; p03da3-d-capture-{ctest,detail}.log; native activation 71 checks |

Первоначальный A compile отказал из-за отсутствующего explicit SceneIdentity
include в новом test; добавлен include, лог p03da3-a-build.log сохранён.
Первый native image capture использовал absolute UTF-8 path с кириллицей,
тогда как существующий SaveFrameImage открывает std::ofstream(string).
Test capture переведён на относительный ASCII path внутри собственного fixture;
renderer не менялся, проверка image/shadow не удалялась и не ослаблялась.

Финальная Release build: PASS, out/p03da3-final-release-build.log.
Финальный профиль: **32/32 PASS, 158.77 s**, activation **104 CPU / 71 DX12
checks**, ownership **101 / 20**, SceneWorldMode **406**. Источники:
out/p03da3-final-profile-ctest.log и out/p03da3-final-profile-detail.log.
Исходники после этого профиля не изменялись. В профиль входят Physics,
Lua scripts/events/gameplay/controller, Project Core/System/Settings CPU/DX12,
ECS/Runtime Hierarchy, Transform, MultiSelection/Gizmo, Editor2/Workflow и lights.

**Один полный CTest: 65/65 PASS, 292.53 s**, exit 0, без repeat/retry/skip.
Источники: out/p03da3-full-ctest.log и out/p03da3-full-detail.log.

| Обязательная проверка в полном CTest | Результат |
| --- | --- |
| Application Hierarchy Activation CPU / DX12 | 104 / 71 checks PASS |
| Application World Ownership CPU / DX12 | 101 / 20 checks PASS |
| SceneWorldMode | 406 checks PASS |
| ECS Hierarchy | 261 checks PASS |
| Runtime Hierarchy CPU / DX12 | 296 / 16 checks PASS |
| Transform Contract | 718 checks PASS |
| Physics | PASS |
| Lua Scripting | 193 checks PASS |
| Lua Physics Events CPU / DX12 | 95 / 76 checks PASS |
| Lua Gameplay API CPU / DX12 | 100 / 168 checks PASS |
| Lua Gameplay Controller CPU / DX12 | 62 / 226 checks PASS |
| Project Core / System CPU / System DX12 | PASS |
| Project Settings CPU / DX12 | 75 / 64 checks PASS |
| MultiSelection CPU / DX12 | 2550 / 395 checks PASS |
| ScalePlaneGizmo / Editor Workflow / Editor2 CPU и DX12 | PASS |

**DX12 validation: 0 errors.** Нулевые GetValidationErrorCount assertions
прошли в native activation/ownership/hierarchy/project/editor tests. Поиск
D3D12 ERROR/CORRUPTION, severity и DX12 validation-failure markers в финальных
profile/full detail logs: 0. Ожидаемые Lua negative-test diagnostics и
asset-loader warnings не приравниваются к GPU validation errors.

**Сохранность и whitespace:** из 830 файлов входного snapshot изменены только
девять существовавших файлов A3, перечисленных выше; добавлены новый regression
и этот отчёт. Все 90 protected assets/EventBus совпадают по SHA-256. Старые
regression sources и assertions, A1 persistence/picking/input fixes, A2 tests и
исторические отчёты сохранены. CMakeLists.txt дополнен регистрацией двух tests.
git diff --check и отдельная whitespace-проверка untracked source/report — PASS.
Источники: out/p03da3-start-hashes.json, out/p03da3-start-status.txt,
out/p03da3-final-integrity.log. Index/history не изменялись; commit/push/merge/
rebase/reset/clean не выполнялись.

## Решение и остаточные ограничения

**READY для A3:** оба DataMode проверены через соответствующие настоящие
Application lifecycle pathways; hierarchy activation остаётся закрытой
инфраструктурой. RootOnly regressions зелёные. Переход к B автоматически
не выполнялся, полноценный пользовательский Hierarchy Editor не заявляется.

- Upgrade намеренно отклоняет случаи за пределами консервативного admission;
  никакой автоматической Collider migration нет.
- General affine/reflected sun-driving не поддержан. Изменение ancestry из
  произвольного Lua callback может привести к последующей диагностике sun guard;
  rollback всего Lua callback не обещается.
- Hierarchy interpolation и ограничения P0.3c остаются прежними.
- После commit сохраняются ограничения A2 на device/allocation/preferences
  failures. Lua preflight не фиксирует внешний script file snapshot и не
  исполняет on_start заранее; callbacks относятся к commit/runtime lifecycle.
- Сохраняется borrowed World/component reference contract A2 и legacy loader
  reread; World references необходимо заново получать после publication.
- Длительный ручной UI soak не проводился. Native DX12 tests проверяли
  Application rendering и отсутствие активных unadapted hierarchy controls,
  а не готовность Tree/Inspector/Gizmo workflows B/C/D.
- В A3 RMB failure не воспроизвёлся: LuaGameplayControllerDx12Regression прошёл
  в профиле и единственном полном CTest с исходным assertion и bounded trace
  completeness check. Историческая intermittent RMB issue из A1/A2 остаётся
  техническим долгом без доказанного root cause; один успешный baseline не
  объявляется её окончательным устранением.
