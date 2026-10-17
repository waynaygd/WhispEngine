# P0.3c — Runtime Hierarchy Integration

## Объём и исходный аудит

Ветка `project_system`. Исходный baseline P0.3b: Release PASS, 58/58 CTest,
Hierarchy 261 checks, Transform 718 checks. Изучены отчёты P0.3/P0.3a/P0.3b и
непосредственные runtime consumers. Существующие незакоммиченные правки
сохранены; git history/index агентом не изменялись.

В P0.3a TransformMath/TransformAccess уже отделены от Editor. P0.3b даёт
авторитетные local TRS + Relationship, итеративный HierarchyGraph, full affine
матрицы, staged persistence и checked world writes. Root-only consumers ещё
использовали RenderPose и BorrowRootWorldTransform; Motion/Bounds выполняли
запись из ForEach, несовместимую с graph mutation validation.

Аудит GPU-пути подтвердил, что RenderInstanceData уже содержит model/normal/MVP,
shader использует inverse-transpose для normal и model linear part +
orthogonalization для tangent, determinant — для handedness, MaterialState —
для reverse winding. Shadow pass также уже принимает model matrix. Поэтому
несовместимая перестройка renderer/solver не потребовалась. Второго Transform
service, authoritative cache или копии physics solver не добавлялось.

## Runtime World и безопасная граница

DataMode сохраняется при CloneData. `RequireRuntimeWorld` валидирует hierarchy
и заявленные runtime ограничения перед запуском/фазами. RootOnly остаётся
default Application/Editor/Project/Legacy режимом. RootOnly по-прежнему не
позволяет добавить Relationship или принять HierarchyData через ReplaceData.
Загрузка hierarchy-сцены обычным редактором завершается ошибкой до публикации;
Relationship не отбрасывается и World не преобразуется в плоский.

Закрытый integration harness в RuntimeHierarchyRegression запускает реальный
World system pipeline с Physics, Motion, Lua, EventBus и interpolation boundary.
Он staged-клонирует author World, валидирует его, подготавливает PlayerStart,
запускает Lua и только потом заменяет предыдущую session. Ошибка подготовки
оставляет прежнюю session и исходный author World. Проверяются Pause/Resume,
повторный Start/Stop, World identity, Clear и generation fences.
Публичный hierarchy workflow остаётся закрытым до P0.3d.

Motion/Bounds имеют отдельный hierarchy path вне ForEach mutation scope.
Обход — deterministic preorder; чтение перед каждой записью заново учитывает
изменения родителя. Motion требует представимый world TRS и диагностирует
shear; Bounds изменяет world translation, сохраняя affine linear part.
Root-only loops и формулы сохранены.

## Affine rendering, bounds и interpolation

Hierarchy RenderSnapshot несёт optional full float model matrix, полученную
из HierarchyGraph. В root path остаётся прежний RenderPose/compose.
PrepareRenderPacket передаёт affine matrix без decomposition в model/MVP и
inverse-transpose normal. Она же используется для main, instanced и shadow
submissions, determinant/winding и world caster bounds. Bounds получаются из
преобразованных углов local AABB; CPU regression сверяет их с независимым
обходом вершин cube. Hardware clip/frustum путь получает фактический MVP.
Нового CPU frustum culler нет.

GPU-проверка использует настоящий RenderSystem + Dx12RenderAdapter: pixels
обычных/instanced draws для shear/reflection, видимую геометрию, shadow depth,
движение родителя и отсутствие draws после cascade. Upload/fences/resource
ownership используют существующий renderer без изменения layout shaders.
Матрицы вне поддерживаемого диапазона ±1e8 и nonfinite world bounds дают
явную диагностику вместо некорректного GPU transform.

**Hierarchy interpolation не реализована и намеренно отключена для всего
HierarchyData World**, включая dynamic roots, их visual children, lights,
camera follow и collider debug. Рендер получает текущую полную affine matrix.
Это исключает раздельную интерполяцию родителя/потомка, потерю shear и stale
parent pose. Count интерполируемых hierarchy объектов — 0. Spawn/destroy,
reparent и frame edits видны через свежий graph. RootOnly interpolation,
alpha clamping, quaternion/lerp и числовой путь не менялись.

Editor picking/selection/Gizmo остаются RootOnly: public hierarchy Editor
ещё закрыт. Внутренний runtime harness не изображает поддержку Editor picking.

## Physics и collider policy

Все physics entry points проверяют graph/geometry до solver writes.
Для HierarchyData Collider dimensions/offset определяются в local пространстве:
full world matrix задаёт центр/offset, checked TRS — ориентацию и масштаб.
RootOnly сохраняет прежние world-sized dimensions/world offset.

Поддержано:

- Dynamic Rigidbody, Character и Player только на root.
- Dynamic collider root: unit authored scale (допуск 1e-5 для matrix roundtrip)
  и нулевой offset. Его Transform остаётся прямым solver input/output.
- Visual descendants динамического root наследуют фактическое движение.
- Static BoxCollider: checked world TRS, positive/negative/nonuniform scale,
  rotation, transform local offset через полную matrix; extents берутся по abs.
- SphereCollider: uniform absolute world scale; radius сохраняет прежнее
  определение max(local halfExtents), умноженное на этот масштаб.
- Static collider без Rigidbody в hierarchy получает локальный immovable
  BodyRef для того же solver. Исходные ECS компоненты не дублируются в World.

Явно запрещено:

- Child dynamic Rigidbody/Character/Player.
- Collider с shear, singular/nonfinite geometry, ellipsoid sphere.
- Collider под любым dynamic Rigidbody ancestor: статический snapshot не мог
  бы следовать за solver substeps без отдельной compound-body реализации.
- Nonunit scale/nonzero offset динамического root collider.
- Collider position/offset/center/extents вне physics диапазона ±1e6;
  большая позиция и компенсирующий offset также отклоняются, согласованно
  с ограничениями существующего raycast.

Resolved static geometry живёт только в value buffers текущего query/step;
буферы резервируются до выдачи BodyRef, не изменяют resource ownership и не
зависят от component pointers исходного parent. Dynamic roots используют
исходные components. Narrowphase/impulses/solver/substeps не переписывались.
Dynamic-ancestor validation выполняется линейно в preorder, без depth recursion
и без обхода ancestor chain для каждого collider в штатном physics path.

Broadphase, contact geometry, raycast, triggers и debug используют одинаковую
resolved geometry. Query после parent edit строит свежий graph. Debug sphere
сохраняет существующее представление bounding cube, не выдаётся за sphere mesh.
Native low-level authoring может подготовить неподдерживаемую layout; её
отвергает runtime preparation/consumer. Lua setters дополнительно откатывают
candidate local Transform, если он нарушил runtime geometry policy.
Raycast/radius query используют один graph snapshot на весь вызов, без
повторного graph build для каждой Entity. Radius читает affine translation,
включая shear visual entities; позиции вне ±1e6 диагностируются явно.

## Lua world API, spawn и lifecycle

get_position не требует TRS decomposition и работает при shear. set_position
меняет world translation, сохраняя linear part. Для root authored rotation и
scale сохраняются без лишней decomposition. get_rotation/get_scale и их setters
дают protected callback error при непредставимом world TRS. Failed setters не
оставляют частичный local Transform; runtime physics constraints проверяются
с rollback до возврата ошибки из Lua API. Parent edit виден следующему чтению.
Новый публичный parenting Lua API не добавлен.

Существующий single-entity prefab entry создаёт roots и в HierarchyData.
Requested pose/components/runtime geometry проверяются в отдельном staging
World до создания Entity в активном World; ID выбирается без collision.
Relationship в таком prefab отклоняется явно. Это поддержка существующего
runtime spawn, не новый Prefab Editor или hierarchy prefab workflow.

World имеет внутренний lifecycle owner hook, не сериализуемый и не клонируемый.
Lua Start проверяет подготовку World и доступность owner до Stop старого runtime.
Обычный Lua destroy помечает unique closure и прекращает дальнейшие callbacks
этих Entity; фактическое удаление выполняется на phase boundary. World hook
ретирает существующие instances children-first при ещё живых components:
on_stop, on_destroy, timer cancellation, input/cursor/camera ownership cleanup.
После этого удаляются components/slots children-first.

Повторные/пересекающиеся requests дедуплицируются. Destroy, запрошенный из
retirement callback, добавляется в тот же drain; плановое завершение не
подменяется map-order retirement из Sync. Native World destroy и MutationQueue
проходят через тот же hook. Очереди событий удалённых target/other очищаются,
pending owners не получают timer/event/update deliveries. Native Clear также
ретирает instances до storage removal. Stop обрабатывает обратный preorder и
снимает hook; ReplaceData живого owned World требует сначала Stop.

Структурные операции во время retirement закрыты; destroy можно поставить
в очередь, но spawn/reparent прямо из on_stop/on_destroy hierarchy runtime
диагностируются. Обычный spawn в Update/Start работает. RootOnly callback и
single-entity destruction path сохраняет прежнюю семантику.

## PlayerStart, camera и lights

До любых spawn/cleanup проверяются **все** PlayerStart: marker должен быть root
без детей. Сценарий marker с игровыми детьми отклоняется до модификации World.
Корректный marker создаёт root Player со SceneIdentity и удаляется безопасно;
controller/input API не переписывались.

Camera follow вынесен в общий host helper, используемый Application и тестом.
Hierarchy target position берётся из свежей world matrix без interpolation;
stale target выключает following. RootOnly использует прежний interpolated pose.

Lights берут translation из full world matrix. Ориентация — нормализованная
цепочка authored local rotation quaternions; scale/shear/reflection никогда
не применяются напрямую к направлению rays. Параметры range/intensity остаются
world-sized. Политика одинаковая для Spot и Directional; Point получает world
position. RootOnly gather/orientation path сохранён.
При равных primary preferences HierarchyData использует stable ID order,
согласованный с persistence P0.3b; порядок Entity в JSON не меняет primary.
Double world position света, не представимая GPU float, даёт явную ошибку,
вместо молчаливого исключения light из gather.

## Проверки и состояние

Изолированные блоки: A — 10, B — 166, C — 47, D — 43,
закрытый runtime harness — 34 checks PASS (включая mode validation).
CPU regression целиком — **296 checks PASS**, DX12 hierarchy — **16 checks PASS**.
Сумма отдельных CLI counts отличается из-за отдельного mode validation в каждом
запуске. Старые Hierarchy — 261, Transform — 718 checks PASS.

Release build — PASS (`p03c-release-build.log`); финальная incremental Release
после stable primary-light refinement — PASS (`p03c-final-build.log`).
Профильный CTest — **30/30 PASS**, 108.54 s. После последней правки света
профиль hierarchy/light/Transform — **6/6 PASS**, 13.60 s.
Существующий `GpuInstancingRegression --dx12` — PASS (реальные 501 → 1 draw,
идентичные pixels, resize/slot reuse/growth/resource retirement).
DX12 error markers в финальном profile/standalone GPU log — **0**.
После уточнения query/geometry range guards: Release — PASS
(`p03c-guards-build.log`), C — **47 checks PASS**, профиль Physics/Lua/Hierarchy/
Light — **9/9 PASS**, 32.91 s. Первый полный CTest до этих уточнений —
**60/60 PASS**, 314.31 s; его журналы сохранены как
`p03c-before-range-guards-full-{ctest,detail}.log`. Финальный полный CTest на
последнем бинарнике — **60/60 PASS**, 256.19 s
(`p03c-final-full-{ctest,detail}.log`). Это полный прогон, без rerun-failed,
исключений или отключённых assertions. Новый CPU test — 296 checks, новый
DX12 — 16, старые Hierarchy — 261 и Transform — 718 checks PASS также в нём.
DX12 ERROR/CORRUPTION/validation-failure markers в final full/profile и
standalone GPU logs — **0**; GPU regression проверяет validation count == 0.

`git diff --check` — PASS. SHA256 всех 89 исходных asset files и EventBus
(90 защищённых файлов) совпадает с началом этапа, включая повторную проверку
после полного CTest. Исходных asset files по-прежнему 89. Runtime fixtures создаются только в уникальных временных
каталогах test asset root и удаляются RAII cleanup.

Новые сценарии включают реальные Worlds/CloneData, system pipeline, Lua
protected callbacks и prefab API, MutationQueue/World destroy/Clear, Physics
query/solver/triggers, Camera/Light gather, RenderSystem main/instance/shadow
submissions, bounds oracle и GPU pixel/depth readback. Assertions старых
регрессий не отключались и не ослаблялись. Ожидаемые Lua negative-test errors
отделяются от DX12 validation errors.

## Файлы этапа

- `CMakeLists.txt` — RuntimeHierarchy CPU/DX12 targets и fixtures.
- `engine/ecs/World.h`, `World.cpp` — runtime validation и lifecycle ownership.
- `engine/ecs/Hierarchy.cpp` — reparent retirement guard.
- `engine/ecs/HierarchyPhysics.h` — временная resolved geometry и runtime guards.
- `engine/ecs/TransformAccess.h` — root position без потери authored TRS.
- `engine/ecs/RenderInterpolation.h` — явный hierarchy no-interpolation path.
- `engine/ecs/systems/MotionSystem.cpp`, `BoundsBounceSystem.cpp` — hierarchy writes.
- `engine/ecs/systems/RenderSystem.h`, `RenderSystem.cpp` — affine snapshot,
  model/MVP/bounds/debug и diagnostics.
- `engine/ecs/systems/PhysicsSystem.cpp` — value geometry adapter для старого solver.
- `engine/ecs/LightGather.h` — authored rotation chain/world positions.
- `engine/ecs/PlayerSpawn.h` — leaf marker policy и runtime Player identity.
- `engine/scripting/GameplayServices.h`, `engine/core/Application.cpp` — camera follow.
- `engine/scripting/LuaRuntime.cpp` — checked world API, closure lifecycle/cleanup.
- `engine/scripting/Prefab.cpp` — staged hierarchy runtime root spawn.
- `tests/RuntimeHierarchyRegression.cpp`, этот отчёт.

Application.cpp редактировался точечной byte-preserving заменой camera block:
существующие non-UTF8 comments/незакоммиченные правки не перекодировались.
Пользовательские Lua/сцены/модели и EventBus на этом этапе не редактировались.

## Вердикт

**READY для P0.3c в заявленных поддерживаемых сценариях.** Affine rendering,
строго ограниченная physics geometry, Lua world-space API и subtree lifecycle
согласованы через существующий HierarchyGraph; полный CTest зелёный.
Hierarchy interpolation намеренно отключена, не объявляется реализованной.
P0.3d UI/Gizmo/subtree duplicate не реализованы и публичный workflow закрыт.
Ручной пользовательский hierarchy UI-сеанс не проводился: workflow закрыт;
проверка рендера выполняется автоматическим настоящим DX12 readback.
Пользовательские Lua/сцены/модели и EventBus сохранены. Commit/push/reset/clean/
merge/rebase не выполнялись.
