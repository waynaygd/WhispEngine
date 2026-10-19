# Parent–Child hierarchy (P1.1)

Relationships belong to `ecs::World`. They use the existing Entity handle,
including its World identity and generation. Scene files store the parent's
persistent scene ID in an optional `entity.parent` field (scene schema v4).
Old scenes without that field remain root-only. `.whispproject` is unchanged.

## Transform contract

`TransformComponent` contains **Local TRS**. For roots, Local equals World.
Use `World::LocalTransform`, `World::WorldTransform`, `SetLocalTransform` and
`SetWorldTransform`. Batch setters validate all results before publishing.
`SetParent(child, parent)` preserves World by default; `keepWorld=false`
preserves Local. An invalid Entity handle means the scene root.

Composition, inversion and TRS decomposition share the existing matrix math.
Shear, singular scale, cycles, foreign/dead handles and depths over 256 are
rejected. Editor property edits, Gizmo, Reparent and placement use the checked
setters. Engine code that writes Transform fields directly is responsible for
maintaining this contract; `ValidateHierarchy()` checks the graph at boundaries.

RenderSystem, lighting, editor markers, picking, placement, Focus Selected and
camera following resolve World poses. Render interpolation resolves a child
through its interpolated parent. Velocity and bounds-motion systems retain
their existing World-coordinate semantics.

Dynamic simulated Rigidbody entities must remain roots. Static/disabled bodies
and triggers can be parented; physics queries and static collision shapes use
World position/orientation and inherited scale. Root collider sizing conventions
are unchanged. In the Inspector, add a Rigidbody at root, make it Static, then
parent it. PlayerStart uses its World pose to spawn a root player; removing the
runtime marker preserves its children by unparenting them with Keep World.

## Editor

Drag an entity onto another row in Scene Hierarchy to parent it. Drop it on
**Scene Root** to unparent. The row context menu has **Create Child**,
**Unparent**, **Reparent...** and the optional **Keep Local Transform** switch.
Search retains matching descendants and their ancestor chain. Branches collapse,
selection highlights remain, and Shift selection follows the visible tree order.

Gizmo always edits World TRS. The Inspector's **World coordinates** checkbox
switches the presentation of its Transform fields; component properties remain
Local in the programmatic component API. Parent+Child group operations transform
only the upper selected roots, so descendants inherit the change exactly once.

Delete, Duplicate, Copy/Paste and Alt+Drag include the selected subtrees in one
history operation. Duplicate remaps internal persistent references and retains
external references. If a pasted external parent no longer exists, the pasted
root is detached while retaining its captured World pose. Isolation includes the
descendants of isolated roots. Drop to Floor uses whole-subtree mesh bounds.

## Lua

Existing `World.get_position/get_rotation/get_scale` and their setters use World
coordinates. Explicit Local APIs are `get_local_position`, `get_local_rotation`,
`get_local_scale` and corresponding `set_local_*` functions.
`World.get_world_transform(entity)` returns `position`, `rotation`, `scale`.
`World.get_parent(entity)` and `World.set_parent(child, parent, keepWorld)` expose
the same checked relationships; `nil` is the scene root. Existing single-entity prefab spawning remains
supported; nested prefabs and prefab variants are outside P1.1.

## Demo and verification

Open `scenes/parent_child_demo.json`, select **House**, and use Focus Selected.
It contains Walls, Door → DoorLight, CeilingLamp, a ground plane and another
root for testing Reparent. The build copies this small scene beside the editor.

`HierarchyRegression` exercises CPU hierarchy, transactions, serialization, Lua,
static physics, interpolation and motion. `HierarchyRegression --dx12` exercises
rendered World poses, actual ImGui Gizmo and Hierarchy drag/drop, isolation,
Runtime cloning, disk roundtrip and DX12 validation. Captures are written to
`diagnostics/hierarchy/` in the build directory.

## Changed files

- World/transform contract: `engine/ecs/World.h`, `World.cpp`, `Hierarchy.cpp`,
  `TransformMath.h`, `RenderPose.h`, `RenderInterpolation.h`,
  `components/TransformComponent.h`, `ComponentRegistry.cpp`.
- World-space consumers: `engine/ecs/LightGather.h`, `PlayerSpawn.h`,
  `systems/RenderSystem.cpp`, `PhysicsSystem.cpp`, `MotionSystem.cpp`,
  `BoundsBounceSystem.cpp`, `engine/core/Application.cpp`,
  `engine/scripting/LuaRuntime.cpp`, `engine/scene/SceneSerializer.cpp`.
- Editor: `engine/editor/EditorLayer.h`, `EditorLayer.cpp`, `EditorUi.cpp`,
  `EditorInspector.cpp`, `EditorTransactions.cpp`, `EditorSelection.cpp`,
  `EditorGroupTransform.cpp`, `EditorScalePlanes.cpp`, `EditorPlacement.cpp`,
  `EditorQualityOfLife.cpp`, `EditorEntityIcons.cpp`, `EditorFogGizmos.cpp`,
  `LightGizmos.cpp`, `SunSynchronization.h`, `TransformMath.h`.
- Build/demo/checks: root `CMakeLists.txt`, `engine/CMakeLists.txt`,
  `engine/assets/scenes/parent_child_demo.json`, `tests/HierarchyRegression.cpp`,
  and this document.
