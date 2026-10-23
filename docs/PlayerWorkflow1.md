# Player Workflow 1.0

## Audit and chosen extension

Before this change `ecs::SpawnPlayer` selected the first enabled PlayerStart with
Transform in Entity-index order. It created a new root with Transform, Player,
CharacterMovement, dynamic Rigidbody (mass 70, locked rotation), box Collider and
Tag `Player`, and optionally a LuaScript with input/camera permissions. Marker
position was the feet; body center was raised by half Character Height. Only yaw
affected the upright body; marker X supplied legacy positive-up camera pitch.
Eye Height was measured from the feet. Marker scale was ignored. All runtime
markers, including disabled ones, were removed, preserving children in world space.
No eligible marker meant no implicit character. Those rules remain the legacy path.

`Application::SetEditorPlayMode` already cloned the authoring World, called this
spawn function, and then started Lua. The previous camera initialization assumed
the result always had Player and Collider. This assumption is now limited to the
legacy path. Physics discovers components during the existing runtime pipeline;
CharacterMovement acts on entities having that component and Rigidbody, rather
than on a hard-coded player type. No new physics registration path is required.

The existing Prefab implementation validates the entire subtree, creates fresh
persistent IDs, remaps prefab-local `$entity` references, restores hierarchy and
rolls back failed instantiation. Runtime cloning preserves persistent IDs, so
scene-authored Lua references resolve in the cloned World as before. Existing
Prefab Editor, ComponentRegistry, resource picker, snapshots and Undo/Redo already
support ordinary component composition. There was no separate Player Prefab system
to replace. This change extends the existing PlayerStart spawn function using
`SpawnPrefab`, without another Player Runtime or serializer.

## PlayerStart modes

New field: `playerPrefab`, asset kind Prefab, default empty.

- Empty: existing Controller Script, Character Height and Eye Height retain their
  meanings and defaults. No automatic scene migration occurs.
- Assigned: instantiate that ordinary Prefab. Its components, Lua permissions,
  controller and cameras define behavior. Legacy fields remain serialized but
  their Inspector controls are disabled with an explanation.
- Missing, unsafe, corrupt or otherwise invalid assigned Prefab: fail the Play
  attempt with the assigned key and concrete error. Never create a legacy fallback.

Selection remains the first enabled marker with Transform by current Entity
index. Disabled markers do not participate. Other markers are removed from the
runtime copy. This is a single local-player spawn policy, not multiplayer or a
persistent GUID ordering guarantee. A Player Prefab containing PlayerStart markers
is rejected rather than retaining editor-only recursive spawn markers.

## Spawn transform

The new mode has this explicit root contract:

```
root.position = PlayerStart.WorldTransform.position
root.rotation = rotation(PlayerStart.WorldTransform) * rotation(PrefabRoot)
root.scale    = PrefabRoot.scale
root.pivot    = PrefabRoot.pivot
```

Rotation is composed as matrices, then represented in existing ECS Euler radians
(`Rz * Ry * Rx`). Full pitch/yaw/roll are retained; no global upright rule or
legacy camera pitch conversion is applied. The Prefab root's saved placement
translation is replaced, rather than added twice. Marker scale does not resize
the player; a marker parent's scale can still affect the marker's world position.
Child local transforms are unchanged, so camera/collider offsets follow the root.

Position and pivot retain the existing Transform representation. In particular,
a nonzero prefab pivot still affects the model matrix through
`T(position + pivot) * R * S * T(-pivot)`; spawn does not rebase or remove it.
Existing hierarchy, shear, scale and physics/pivot validity rules apply. Unsupported
transforms produce a spawn error. Rigidbody and CharacterMovement retain their
own rotation and collision policies; PlayerStart does not change them.

## Runtime lifecycle and failure safety

1. Finish the current authoring transaction and clone the existing World into a
   local staging World and clone map.
2. Run the existing PlayerStart selection/spawn against that copy. Assigned
   Prefabs are loaded through canonical, asset-root-contained `SafeAsset` and the
   existing bounded/validated Prefab loader.
3. Complete the root pose, hierarchy and references. Add PlayerComponent to the
   root if absent; preserve an existing PlayerComponent and its values.
4. Only after success publish the Runtime World/map and enter runtime editor
   context. Initialize legacy eye pose only for a legacy-created character.
5. Start Lua through its existing lifecycle, then use existing physics, camera
   selection, interpolation and rendering.
6. Stop stops Lua, resets physics/camera/interpolation/mutations, discards the
   runtime copy and restores the editor scene, selection/history and camera.

A failed preparation discards the staged World, leaves authoring data, selection
and history in place, does not start Lua and does not publish partial entities.
The editor shows a persistent **Play failed** window with the concrete error and
logs it. Close the window or correct the assignment and retry Play; a new attempt
clears the old error. Missing references are also handled by existing Scene
Validation; an unused legacy Controller Script is not inspected in Prefab mode.

## Player identity, Lua and Camera

PlayerComponent is the existing local-player identity marker. The spawned Prefab
root receives it if needed. It does not confer input ownership, camera permission,
movement, a Rigidbody or a Camera. Its Eye Height remains a legacy-controller
setting, not an automatic child-camera offset. No TagComponent is injected and
authored tags are preserved.

`World.find_by_tag("Player")` retains literal tag matches and additionally returns
live entities having PlayerComponent, once each, in existing Entity-index order.
This compatibility alias makes an untagged spawned root discoverable without
introducing another identity flag or requiring authored tags. Other tag queries
are unchanged. Multiple manually authored PlayerComponents can therefore return
multiple results; this stage does not define multiple local input owners.

LuaScript components start normally after the whole Prefab has been instantiated.
Prefab-local reference fields point to the new runtime children. Existing
`Script.require`, module isolation, hot reload, sandbox and input/camera permissions
are unchanged. Scripts can use the established API, for example:

```lua
return {
    fields = { camera = { type = "entity" } },
    on_start = function(self)
        Camera.set_active(self.camera) -- requires Allow gameplay camera
    end
}
```

An enabled child Camera participates in Camera System 1.0's automatic priority
selection even without this call. Explicit selection and fallback rules are
unchanged; Camera is optional. Drive ECS camera transforms deliberately using an
authored controller. The old FirstPersonController still drives the legacy camera;
adding a child Camera does not convert that script into a new rig controller.
With no eligible ECS camera the existing legacy camera remains available; Prefab
mode does not invent an eye offset or camera for a generic object.

Physical-parent interpolation and Camera Attachments 1.1 consume the existing
current-frame CameraState. Destroying the player destroys descendants; references
to them become invalid and existing camera fallback/attachment resolution applies.
Repeated Play creates fresh World identities and fresh Prefab persistent IDs.

## Editor instructions

1. Create an ordinary root Entity and add the components your controller needs.
   Rigidbody, Collider, CharacterMovement, Lua Script and Camera are optional.
2. Create a child Entity, parent it to the root, add Camera and set its local eye
   or rig offset if your game uses an ECS camera. Configure camera priority or a
   Lua entity-reference field as usual.
3. Assign a Lua Script and only the input/camera permissions it needs. Assign any
   entity-reference fields to children before saving the Prefab.
4. Select the root, use the existing **Create Prefab** action, and save under
   `prefabs/*.prefab.json`. Edit and save later through the existing Prefab document.
5. In the Scene create **Player > Player Start**. Choose **Player Prefab** in
   Inspector using the standard indexed resource picker, or drag a Prefab from
   Asset Browser onto the field. The field also supports clear/reveal actions.
6. Position/rotate PlayerStart, then Play. Runtime Hierarchy and diagnostics show
   the instantiated root/children. Stop returns to the authoring scene.

Prefab-mode gizmos show the spawn origin and full-rotation direction, without
claiming legacy body dimensions or eye height. Legacy marker gizmos are unchanged.
Assignment and property edits participate in ordinary Undo/Redo and Scene Save/Load.
Runtime play never applies changes back to the Prefab or scene files.

## Scope and future extension

There is no GameMode, GameState, separate PlayerController Entity, Project Default
Player Prefab, travel/startup maps, input mapping framework, multiplayer, new camera
controller or separate Player editor. A later resolver can choose a Prefab by
PlayerStart override, then GameMode, then Project default before entering this same
spawn branch. Empty assignment currently always means legacy, not an implemented
project-default setting. Existing Prefab restrictions (including no nested Prefabs)
and existing one-Behaviour-per-Entity limits remain.

## Changed files for this stage

- `engine/ecs/components/PlayerStartComponent.h`: optional Prefab asset key.
- `engine/ecs/components/PlayerComponent.h`: identity contract comment.
- `engine/ecs/PlayerSpawn.h`: existing spawn path extended with Prefab mode.
- `engine/ecs/ComponentRegistry.cpp`: typed asset property and legacy grouping.
- `engine/core/Application.h/.cpp`: staged Play preparation and error reporting.
- `engine/scripting/LuaRuntime.cpp`: Player discovery compatibility alias.
- `engine/editor/EditorInspector.cpp`: mode explanation and disabled legacy fields.
- `engine/editor/EditorUi.cpp`: concrete failed-Play diagnostic window.
- `engine/editor/LightGizmos.cpp`: Prefab-mode spawn origin/direction.
- `engine/editor/SceneValidation.cpp`: skip unused legacy controller dependency.
- `tests/PlayerWorkflowRegression.cpp`, `CMakeLists.txt`: CPU/native regressions.
- This document. Earlier Camera Attachments changes remain in the working tree.

## Validation

Verified on Windows, 2026-10-23, in the existing `x64-release` configuration:

- Release WhispEngine, PlayerWorkflowRegression and related regression targets
  built successfully (`out/player1-related-build.log`).
- Initial related CPU run: **7/7 passed** (CharacterMovement, legacy Lua controller,
  LuaScripting including modules, Prefab, Hierarchy, Camera System and Attachments),
  `out/player1-core-tests.log`.
- Dedicated CPU regression: **62 checks passed**, `out/player1-cpu-tests.log`.
  Covers full world spawn position/rotation, root scale/pivot, remapped references,
  module-backed on_start, real physics integration, untagged identity, generic
  body-less Prefab, disabled/multiple/missing markers, legacy dimensions/controller,
  unsafe/missing/corrupt Prefab and post-instantiation rollback.
- Dedicated native DX12 regression: **62 checks passed**, **zero DX12 Validation
  Errors**, `out/player1-dx12-tests.log`. Uses the actual Inspector resource picker,
  Undo/Redo, Scene Save/Load, Prefab document editing/save, child Camera selection,
  repeat Play/Stop, deletion/fallback, failed-Play diagnostic and retry, exact file
  preservation, and loading a generated project before spawning the Player Prefab.
  Safe-path checks for generated projects require running outside the filesystem
  sandbox: an initial sandboxed project run failed on a parent-path access check;
  the subsequent unrestricted targeted run passed.

- Final related regression run: **33/33 passed**, 162.09 seconds,
  `out/player1-related-tests.log`. Includes ProjectSystem CPU/DX12, SceneValidation,
  physics/movement, Prefab/Hierarchy, Scene/Prefab documents and restart, Lua API,
  inspection/modules, Player Workflow CPU/DX12, Camera Attachments CPU/DX12,
  Camera System CPU/DX12/Vulkan smoke, interpolation, instancing, Runtime UI,
  lights, Inspector and Material Editor including restart.
- `LuaGameplayControllerDx12Regression` passed in this run. The previously known
  intermittent RMB free-fly issue was not changed or declared fixed; its assertions
  remain intact. One successful run does not establish a fix for that issue.
- Original preference/layout bytes were restored and verified by the runner.
  `git diff --check` passed; tracked engine assets remain unchanged. No branch
  switch, commit, game-project edits or repository cleanup was performed.

No Debug or new Vulkan-specific Player rendering result is claimed.
The dedicated regression uses generated owned fixture files under the build asset
root. No DOOM67, other game projects or source assets are edited for verification.
