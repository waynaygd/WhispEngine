# Camera System 1.0

## Architecture and audit

`GameplayCamera` previously held the legacy control pose and follow target.
`Application` resolved follow after physics using `RenderInterpolation`; the
renderer rebuilt a perspective view from yaw/pitch for meshes, light gathering
and collider overlays. There was no ECS camera. The registry already drives
Inspector properties, scene/prefab snapshots, duplication and Undo/Redo, so no
parallel component serialization or camera manager is needed.

The extended `scripting::GameplayCamera` is the **selection authority** as well
as the existing legacy control state. `ecs::CameraState` is a value snapshot,
not another mutable camera runtime. `Application::UpdateRenderSystemCameraAspect`
resolves exactly one source, then `RenderSystem::SetCameraState` supplies that
same view/projection to all mesh paths, debug geometry, lighting and fog. The
sky gets the same basis before background submission. There are no renderer
backend-specific camera selection rules.

During Play an eligible Camera Entity supplies its world pose and projection.
Without one, the existing legacy path supplies the view. Edit, Prefab and other
authoring workspaces keep their existing editor camera/preview lifecycles.
CameraComponent does not process input, simulate physics, move a player or
require a PlayerComponent.

## CameraComponent

Registered component key: `Camera`. Create with **Create > Camera**, or add
**Camera** through the existing Inspector Add Component menu. Hierarchy labels
use `[Camera]`; the ordinary entity icon also allows selection in Scene View.
Create Camera matches the current editor view. Creating, editing, duplicating,
deleting and resetting use the existing editor transactions.

| Field | Default | Contract |
| --- | --- | --- |
| enabled | true | Participates in game selection |
| projection | Perspective (0) | Orthographic is 1 |
| fieldOfView | 1.04719755 | Vertical radians; 1 through 179 degrees |
| orthographicSize | 10 | **Full** vertical span in world units, .001–100000 |
| nearClip | .1 | Positive distance, at least .001 |
| farClip | 2000 | Greater than nearClip, at most 1000000 |
| priority | 0 | Inspector range -1000000–1000000 |

Horizontal orthographic span is `orthographicSize * viewportAspect`.
Near/far are forward-axis distances. Projection is left-handed, column-major,
column vectors, depth [0,1], matching the existing rendering convention.
Perspective uses vertical FOV and the actual viewport aspect, not window aspect.

Registry setters reject nonfinite values and invalid ranges; cross-field clip
edits restore the old value on failure. Scene/prefab input is validated too.
Native C++ can access components directly as before; invalid projection data
makes a camera ineligible at selection, rather than entering GPU constants.
A Camera requires Transform. Registry Add supplies it when absent; removing
Transform is blocked until Camera is removed. Old scenes need no migration.

Scene/Prefab Save/Load, runtime world cloning, editor duplication and undo use
the existing registry snapshots. Selection handles are runtime-only and are not
serialized into scenes or prefabs.

## Selection and lifecycle

1. A valid explicitly selected Camera Entity wins, regardless of priority.
2. Otherwise choose the enabled, valid Camera with highest priority.
3. Ties use the lowest Entity index in the current World, independent of
   component iteration order. Normal unchanged scene loads retain file order;
   this is not a persistent GUID ordering guarantee after structural edits.
4. With none eligible, use the legacy camera.

Deletion, disable, removal of Transform/Camera or invalid settings invalidate
explicit selection. That explicit handle is forgotten: re-enabling a camera
participates in automatic priority selection, rather than restoring an old
manual choice. Foreign-world and stale-generation handles are rejected.

Camera selection resets on each Play/Stop and project transition. Play uses the
cloned Runtime World; authoring handles must not be passed directly to runtime
selection. Lua entity-reference fields already resolve to cloned entities.
Stop discards runtime changes and restores the editor pose. No required Camera
Manager Entity or project migration is introduced.

Switches are **cuts** to the newly selected camera's own current interpolated
pose, not blends between unrelated transforms or projection modes. The first
rendered Play frame resolves the selected source; cached poses of previous
cameras are not interpolated into it. A live higher-priority camera can change
the automatic source; explicit selection keeps its choice until invalidated.

## Transform, coordinates and interpolation

Camera local forward is **+Z**, right +X, up +Y. ECS Euler rotation is radians
and `Rz * Ry * Rx`, including roll. Position follows the existing world Transform,
parenting, pivot and scale semantics. View orientation composes local rotations
through the hierarchy, without inheriting scale or reflections. Parent scale
therefore scales an offset, but cannot stretch or invert the camera basis.

Existing hierarchy limits still apply: shear/singular transforms are rejected
by hierarchy operations. This stage does not add a separate permissive camera
transform tree.

Legacy GameplayCamera positive pitch looks **up**; positive ECS Transform X
looks **down**. The legacy adapter builds rotation `{-pitch, yaw, 0}`. This is
why a legacy pitch copied to a Transform requires a minus sign; it is an explicit
conversion between two existing conventions, not a global change to physics or
Euler signs. A camera created from the editor applies the same conversion.

Camera translation and each ancestor rotation resolve through the existing
`RenderInterpolation` history. A camera child of a dynamic physical parent
therefore follows that parent's interpolated render pose, including Pause/reset
behavior. Nonphysical Transform edits remain immediate, as with other entities;
there is no new per-frame smoothing or world cloning.

Supported fixture arrangements: FPS child at eye offset, TPS child behind a rig,
fixed orthographic isometric camera and a top-down camera at the vertical pole.
No orbit/FPS/top-down controller is embedded in CameraComponent.

## Lua API and permissions

```lua
return {
    fields = { camera = { type = "entity" } },
    on_start = function(self)
        Camera.set_active(self.camera)
        assert(Camera.get_active() == self.camera)
        assert(Camera.is_active(self.camera))
    end
}
```

- `Camera.set_active(entity)` selects an eligible camera. Invalid, disabled,
  foreign, destroyed or missing-component entities produce a diagnostic.
- `Camera.get_active()` returns the selected/automatic Camera Entity, or `nil`
  for the legacy path.
- `Camera.is_active(entity)` returns false for a nonlive entity; otherwise checks
  the current selection, including fallback.

All three require the existing enabled Behaviour's **Allow gameplay camera**
permission (`cameraEnabled`). They do not require input permission, mouse
capture or the exclusive legacy camera lease, and never write arbitrary entity
Transforms. There is no new unlimited permission. Runtime Stop callbacks cannot
change selection. A later authorized script may intentionally select a different
camera; exclusive camera control and image-source selection are different APIs.

`Camera.acquire/release/follow/rotate/set_angles/set_position` retain their old
legacy-control semantics and permissions. They do not override Camera Entity
selection or silently write its Transform. Existing `Camera.position/forward/
flat_forward/right/angles` retain their legacy lease/read semantics too. To read
or move an ECS camera use `Camera.get_active()` with the existing `World`
Transform APIs. This explicit distinction preserves old controller behavior.

For example, a script controlling an ECS rig may use the existing API:

```lua
local camera = Camera.get_active()
if camera then
    World.set_position(camera, Vec3.new(0, 8, -8))
    World.set_rotation(camera, Vec3.new(0.65, 0, 0))
end
```

Parenting can be authored in the editor or through the existing World API.
A project choosing an ECS camera must drive its Transform deliberately. The
legacy FPS controller remains unchanged for scenes without a CameraComponent;
adding an ECS camera does not implicitly turn legacy `follow` into a rig driver.
No callbacks, serialized Lua properties, modules, sandbox or ownership rules
were replaced.

## Rendering integration

`CameraState` contains source Entity, unit-scale pose, right/up/forward vectors,
validated projection settings, aspect, view, projection and viewProjection.
Mesh instancing, individual packets and collider drawing use the same matrix.
Light bounds and fog use its actual view/projection. DX12 fog supports parallel
orthographic rays, origins and linear depth as well as the existing perspective
reconstruction. Directional shadow fitting includes orthographic receiver span.
Sky/background uses the final basis including roll; an orthographic sky uses
parallel directions. No weapon/viewmodel pass is added.

Vulkan receives the same backend-neutral matrices. Existing backend limitations
remain: Vulkan has test-primitive rendering and no equivalent complete DX12
material/editor/lighting/fog implementation. A Vulkan smoke test exercises both
projection types through RenderSystem collider drawing; it is not a claim of
DX12/Vulkan feature parity or pixel-identical output.

## Camera Attachments 1.1 boundary

The final camera snapshot is available from
`Application::GetRenderCameraState()` (and `RenderSystem::GetCameraState()`).
It is computed after gameplay/physics and interpolation setup, before scene and
sky submission; it is refreshed for the render target's aspect. Consumers should
copy/use its pose and basis for that render, not reconstruct from Lua yaw/pitch.
The snapshot is presented data; before the next render it can still describe the
last frame. It owns no ECS pointers.

Stage 1.1 can build attachment render poses from this source after camera resolve,
without another camera runtime. Synchronizing attachment render interpolation,
late rig updates and ordering, attachment offsets, and viewmodel-specific needs
remain for 1.1. This release does not promise removal of FPS weapon jitter.

Frustum visualization, camera-specific gizmos and selected-camera image preview
are optional follow-ups; ordinary entity icons, Transform gizmos and the Play
viewport are available now. No specialized gameplay Camera Rig or camera blend
system is added.

## Changed files

- New: `engine/ecs/components/CameraComponent.h`, `engine/ecs/CameraState.h`.
- `engine/ecs/ComponentRegistry.cpp`: properties, validation and dependencies.
- `engine/scripting/GameplayServices.h`, `LuaRuntime.cpp`: selection and Lua API.
- `engine/core/Application.h`, `Application.cpp`: lifecycle and render source.
- `engine/ecs/systems/RenderSystem.h`, `RenderSystem.cpp`: shared final matrices.
- `engine/editor/EditorLayer.h`, `EditorTransactions.cpp`,
  `EditorViewportCreate.cpp`, `EditorUi.cpp`: create, hierarchy and transactions.
- `engine/editor/EditorVisualSettings.cpp`: final sky basis.
- `engine/render/backends/dx12/Dx12Fog.cpp`, `engine/shaders/dx12/fog.hlsl`:
  orthographic fog reconstruction.
- `tests/CameraSystemRegression.cpp`, `CMakeLists.txt`: CPU/DX12/Vulkan coverage.
- `tests/FogPostHotfixRegression.cpp`: creation-menu coordinates account for
  the new Camera leaf; existing submenu assertions remain intact.
- This document. No game project files or authored assets require modification.

## Validation

- **Release build passed:** WhispEngine and the 22 related regression targets
  (`out/camera1-final-build.log`). DX12 and Vulkan are enabled in the existing
  x64-release configuration. No Debug result is claimed.
- **Final focused run: 5/5**, 42.93 seconds, after the final shader correction:
  CameraSystemRegression (**61 checks**), CameraSystemDx12Regression
  (**49 checks**), CameraSystemVulkanRegression (**5 checks**),
  FogDx12Regression and FogPostHotfixDx12Regression.
  Log: `out/camera1-final-camera-fog-tests.log`.
- Native camera coverage includes the actual Inspector, property Undo/Redo,
  duplication, Lua selection, Perspective/Orthographic image differences,
  movement through Lua World API, disabled/deleted-source fallback and repeat
  Play/Stop. Distance/Height/Volumetric fog runs with orthographic rays, finite
  pixels and image changes. A pixel assertion also preserves the legacy Distance
  Fog ramp on geometry beyond maxDistance. The camera and Fog tests assert
  **zero DX12 validation errors**.
- CPU fixtures cover Scene/Prefab disk roundtrips, registry properties, clone,
  clip validation and rollback, projection depth/aspect, parent rotations and
  scale, physical-parent interpolation, FPS/TPS/isometric/top-down poses,
  deterministic priority, stale/foreign handles, Lua permissions and legacy API.
- **Broader related run: 33/35**, 175.81 seconds
  (`out/camera1-final-tests.log`). Passed Physics/CharacterMovement,
  LuaPhysicsEvents CPU/DX12, UI4, Prefab/Hierarchy, Prefab and Document workflows
  including restart, Lua Inspector, GameplayEvents CPU/DX12, LuaGameplayApi
  CPU/DX12, all Camera tests, LuaGameplayController CPU, RenderInterpolation,
  instancing, LuaScripting (**247 checks**), LightSystem CPU/DX12,
  InspectorUX and MaterialEditorUX including restart. FogPostHotfix's menu test
  was updated for the new Camera row without removing any submenu assertion.

Two native capture failures occurred in that broad run:

1. RuntimeUIRegression: `real FPS capture with runtime HUD`. A separate run with
   fresh preference files passed **799 checks** on the current engine
   (`out/camera1-current-ui-test.log`). For comparison the same test was compiled
   with original HEAD headers and the saved pre-Camera Engine library; it passed
   **800 checks** (`out/camera1-baseline-ui-test.log`). Frame-dependent checks can
   vary in count. The camera-selection implementation marker was verified absent
   from the baseline executable and present in the current engine. The broad
   failure is recorded as an intermittent native-capture failure, not erased by
   the successful isolated run.
2. LuaGameplayControllerDx12Regression: `Edit RMB captures free-fly`. This was
   already recorded in Architecture 2.0, including its old-runtime comparison.
   A fresh run of that saved old controller executable also failed free-fly,
   at `Play without player retains RMB free-fly`
   (`out/camera1-baseline-controller-test.log`). These fixtures contain no
   CameraComponent. The assertion remains unchanged; the underlying input/focus
   issue is **not fixed**, and a green full native controller run is not claimed.

The initial focused CPU run also passed 4/4
(`out/camera1-core-tests.log`). Test fixtures use owned scratch files; no DOOM67
or other game projects were opened or modified. Temporary clean-layout runs
restore and verify exact original editor-preference/imgui.ini bytes in finally
blocks. Original working-tree files were never reset for baseline comparisons.
`git diff --check` passes; tracked engine assets remain unchanged. No commit,
scene migration or repository cleanup was performed.
