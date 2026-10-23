# Camera Attachments 1.1

## Architecture

`CameraAttachmentComponent` is an ordinary registry component (`CameraAttachment`,
Inspector label **Camera Attachment**). It contains `enabled` (default true),
`localPosition` and `localRotation` (both default zero). Transform is required;
registry Add supplies one when missing and prevents its removal while attached.
Offsets must be finite, within +/-1000000 per axis. Rotation uses radians,
column vectors and ECS `Rz * Ry * Rx` order.

The source is Camera System 1.0's **current final CameraState**, including
interpolated position, rotation and roll. No second camera runtime, transform
storage, smoothing history or weapon pass is introduced.

`Application` enables attachments only during Play and supplies the same camera
snapshot used for rendering. During mesh gather, `CameraAttachmentPose` replaces
an attached root's visual position/rotation with camera-relative values. Both
ordinary and instanced packets then use that pose to generate model, normal and
MVP matrices. Failed instancing falls back to the same ordinary packet.

For a root without a pivot, the visual model matrix is:

```
CameraState.pose(unit scale) * T(localPosition) * R(localRotation) * S(authored world scale)
```

The existing pivot representation remains supported. Position/rotation of the
root and its ancestors are replaced visually, while its existing interpolated
world scale is retained. Descendants compose their existing local TRS/pivot
through the attached root. The nearest valid enabled attachment wins for nested
attachments; an attachment on a child starts a new camera-relative root.

The resolver borrows ECS data and builds transient render poses. It does not
write Transform, Rigidbody or Collider, clone a World, or retain entity-to-pose
overrides between frames. Detach, disable, destruction and generation reuse
therefore cannot leave a cached attachment pose behind.

## Coordinates and synchronization

Local +X is camera right, +Y up, +Z forward. A local rotation uses the same signs
as any ECS Transform. Do **not** copy legacy yaw/pitch with a manual sign change
into an attachment every frame: CameraState already contains the legacy pitch
conversion. Its quaternion is composed directly with the local rotation.

A physical camera/follow target resolves through the existing interpolation
before packet gathering. Attachments consume that exact current-frame snapshot,
not previous Lua angles or the previous camera pose. Changing cameras is a cut
to the new camera-relative pose; it uses the new projection and aspect as well.
Perspective, Orthographic and the legacy camera share this path.

Attachments are available while Play is paused, because rendering continues.
Edit/Prefab workspaces display ordinary authored poses even when a component is
present. Without an ECS Camera during Play, attachments follow the final legacy
camera. There is no dependency on editor-camera entities or on Camera.acquire.

## Editor and persistence

Use **Inspector > Add Component > Camera Attachment**. The existing property
widgets edit Enabled, Local Position and Local Rotation (rad). The Inspector
explains that the override affects meshes only. Scene/Prefab Save/Load, clone,
duplication, property edits, reset, removal and Undo/Redo use registry snapshots.
No scene migration or asset rewrite is required.

A component already enabled in a Prefab works immediately after that Prefab is
spawned during Play. It does not require a Behaviour or repeated Camera.attach
calls. Attach at the Prefab root to include its visual children.

## Lua API

```lua
-- Called from a Behaviour with Allow gameplay camera enabled.
local model = World.spawn_prefab(
    "prefabs/my_visual.prefab.json",
    World.get_position(self.entity)
)
Camera.attach(model, Vec3.new(0.25, -0.35, 0.8), Vec3.new(0, 0, 0))
-- Later, optional offset change:
Camera.attach(model, Vec3.new(0.2, -0.3, 0.9), Vec3.new(0, 0.1, 0))
-- Restore ordinary ECS rendering:
Camera.detach(model)
```

`Camera.attach(entity, localPosition, localRotation)` adds or updates the enabled
component. `Camera.detach(entity)` removes that entity's component; removing an
absent component is harmless. Both require a live same-world Entity with Transform
and the existing enabled Behaviour's `cameraEnabled` permission. Invalid,
destroy-pending, foreign or missing-Transform handles produce Lua diagnostics.
Stop does not permit these calls. No input permission or legacy control lease
is required, and no camera permission is relaxed.

Component changes are queued through the existing Lua MutationQueue and applied
at its normal boundary before render submission. Multiple attach/detach calls
are ordered. A target destroyed before application is safely ignored. The
commands do not grant physical movement access or change camera ownership.
Existing Camera System 1.0 API and all old callback signatures remain unchanged.

Detach returns the object to its existing authored/interpolated World pose; it
does not freeze it at the last displayed camera pose. Detaching a child inside
an attached ancestor still inherits that ancestor normally. Detach the ancestor
root or reparent the child if it should leave that visual hierarchy.

## Other render paths and limitations

- Meshes and their material/submesh packets, instancing and ordinary fallback
  use the same override. Cast-shadow models and caster bounds use the same visual
  pose, and materials retain their existing receive/cast-shadow rules.
- There is no separate depth layer, FOV or near-clip exception. An attachment can
  intersect world geometry, be occluded, receive/cast shadows or be clipped by the
  active camera. This is a universal visual attachment, not an FPS viewmodel pass.
- Physics, raycasts and collider debug geometry continue to use the physical ECS
  Transform. Attaching a physical entity intentionally separates its mesh from
  its collider; gameplay collision is never teleported to the camera.
- Editor picking and Transform gizmos remain at the authored pose in Edit Mode;
  Play retains its existing restrictions on editor picking/gizmos.
- Lights, camera sources, Fog volumes and UI Canvases do not acquire visual pose
  overrides. Only the mesh render paths and their shadows are affected.
- Existing hierarchy depth (256) and representable TRS restrictions remain.
  Invalid native-written attachment data is ignored. An unrepresentable
  descendant result (shear/singular/out-of-range scale) falls back to its ordinary
  pose rather than adding another affine Transform system.
- Vulkan's existing lack of the complete DX12 mesh/material path remains; this
  stage does not add backend feature parity. CPU packet checks and native DX12
  verification cover the implemented mesh path.

Recoil, sway, weapon animation, different viewmodel FOV, a dedicated render pass,
attachment picking in Play and editor camera-preview controls are possible later
work, not APIs implemented by this stage.

## Changed files

- New `engine/ecs/components/CameraAttachmentComponent.h` and
  `engine/ecs/CameraAttachments.h`.
- `engine/ecs/ComponentRegistry.cpp`: registration, fields and Transform dependency.
- `engine/ecs/systems/RenderSystem.h/.cpp`: Play activation and pose resolution
  before common mesh/shadow packet preparation.
- `engine/core/Application.cpp`: supplies Play activation alongside CameraState.
- `engine/editor/EditorInspector.cpp`: scope/physics explanation.
- `engine/scripting/LuaRuntime.cpp`: permission-checked queued attach/detach.
- New `tests/CameraAttachmentsRegression.cpp`, plus `CMakeLists.txt` registration.
- This document. No DOOM67, other game projects or source assets were changed.

## Validation

Verified on Windows, 2026-10-23, using the existing `x64-release` configuration:

- Release `WhispEngine`, the new regression executable and related regression
  targets built successfully. Logs: `out/attachments1-tests-build.log` and
  `out/attachments1-related-build.log`.
- Targeted CPU run: **4/4 passed** (`CameraAttachmentsRegression`,
  `CameraSystemRegression`, `LuaScriptingRegression`, `GpuInstancingRegression`).
  Camera Attachments completed **2044 checks**.
  Log: `out/attachments1-cpu-tests.log`.
- Native `CameraAttachmentsDx12Regression`: **70 checks passed**, including
  **zero DX12 Validation Errors**, with `WHISP_DX12_DEBUG=1`.
  Log: `out/attachments1-dx12-tests.log`.
- Related regression run: **27/28 passed** in 112.39 seconds, including repeat
  attachment CPU/DX12 checks, Camera System CPU/DX12/Vulkan smoke, physics,
  character movement, interpolation, Lua, Scene/Prefab documents and restart,
  Inspector, Material Editor, Runtime UI and lights.
  The sole failure was the explicitly known
  `LuaGameplayControllerDx12Regression`: `Edit RMB captures free-fly`.
  Its assertions and implementation were left unchanged as requested.
  Log: `out/attachments1-related-tests.log`. This is not an all-green suite.

The CPU regression checks actual submitted model/MVP matrices over varying
interpolation fractions, camera movement/rotation and both projections, with
legacy and ECS sources. It also covers instancing, fallback, shadows, descendants,
world scale, lifecycle, persistence, permissions and physical-data preservation.
The DX12 regression checks actual images and the editor, dynamic Prefab spawning,
camera changes, offset changes, detach, replacement and repeated Play/Stop.

The native test uses the real Application, ImGui Inspector and DX12 rendering,
with synthetic input and an isolated generated Scene/Prefab fixture. Source game
assets are untouched. Existing editor preferences and ImGui layout bytes are
restored after the runs. Debug has not been built for this change; Vulkan mesh
attachment image parity is not claimed.
