# Camera API 1.2

## Audit and implementation

Camera System 1.0 already selects an ECS Camera and resolves its world translation
and hierarchy rotation, with optional physical interpolation. The Lua Camera table
also contains the older lease-controlled GameplayCamera interface. Those are two
different image/control sources; redirecting legacy getters would break existing
controllers. World local/world Transform setters and Physics.raycast already
provide the required control and query operations.

The existing camera composition was extracted into `ecs::ResolveCameraPose`.
`ResolveCameraState` retains its selection/projection validation and uses that same
helper with its optional RenderInterpolation. New Lua spatial getters call it
without interpolation and reuse the existing basis construction. No second
camera runtime, retained pose cache, camera controller or Transform override was
added. Camera Attachments still consume the final renderer CameraState.

## Lua API

```lua
Camera.get_position(entity)
Camera.get_forward(entity)
Camera.get_right(entity)
Camera.get_up(entity)
```

The target must be a live same-world, current-generation Entity with Transform and
CameraComponent. It need not be selected, enabled or projection-eligible: these
are spatial queries, not selection operations. Missing/invalid/foreign/destroyed
or destroy-pending targets produce Lua diagnostics. Native foreign handles cannot
be constructed by sandboxed Lua; the existing World ownership checks also reject
them before resolving a pose.

All four require the enabled Behaviour's existing **Allow gameplay camera**
permission. They do not require the legacy/selection host, acquire a
lease, require input permission, mouse capture or change camera selection.
Existing Stop/owner-liveness checks remain. Reads grant no Transform write rights.

The getters return **current logical gameplay pose**, not a snapshot from the
last render or an interpolated pose. A physical parent's rendered translation
can therefore differ between fixed ticks. Attachments intentionally follow the
interpolated render pose; gameplay raycasts use the simulation pose and existing
physics state. This distinction avoids introducing render timing into gameplay.

## Coordinates and setters

Position obeys the same world hierarchy, pivot and scale rules as the camera's
render origin. +X is right, +Y up, +Z forward. Directions are normalized and use
the composed local rotations of every parent and the camera, including roll.
Scale affects the position of an offset child but cannot stretch or reflect the
camera basis. Existing representable TRS/shear/depth restrictions still apply.

Use existing `World.set_local_position` / `World.set_local_rotation` for a rig
child and `World.set_position` / `World.set_rotation` for world-space edits.
Their existing validation, dynamic-body restrictions and application semantics
remain unchanged. Transform value setters are applied synchronously in their
allowed gameplay callback context; existing structural operations continue to
use MutationQueue. These getters observe an accepted setter immediately and the
next render resolves the resulting camera through the same hierarchy math.

For rotation data use `World.get_world_transform(entity).rotation` or
`World.get_local_rotation(entity)`. Angles are radians, ECS `Rz * Ry * Rx`.
World Euler values follow general Transform decomposition; with reflected scale
their representation can differ from the scale-free optical basis. Use the new
direction getters for optical axes rather than rebuilding them from that Euler value.
Positive ECS X looks down; positive legacy pitch looks up. No sign convention was
changed. No new get_rotation, get_flat_forward or duplicate ECS setters were added.
The existing legacy `Camera.set_position` keeps its old meaning.

## Player Prefab and examples

Author a child Camera in the ordinary Player Prefab, expose an entity field in its
Behaviour, and assign that field to the child before saving the Prefab. PlayerStart
and GameMode spawning remap the reference as before. For example:

```lua
return {
    fields = { camera = { type = "entity" } },
    on_start = function(self)
        Camera.set_active(self.camera)
    end,
    on_update = function(self, dt)
        local camera = self.camera -- may also use Camera.get_active()
        if not camera then return end
        local origin = Camera.get_position(camera)
        local forward = Camera.get_forward(camera)
        local right = Camera.get_right(camera)
        local hit = Physics.raycast(origin, forward, 50, self.entity)

        local length = math.sqrt(forward.x*forward.x + forward.z*forward.z)
        if length > 0.0001 then
            local flat = Vec3.new(forward.x/length, 0, forward.z/length)
            -- Feed flat/right into your existing movement/controller API.
        end
        -- Drive local camera rotation from your controller's state as needed:
        -- World.set_local_rotation(camera, Vec3.new(pitch, yaw, roll))
    end
}
```

At a vertical view, horizontal forward is undefined; the example guards its length.
With roll, right may have a vertical component too; project/normalize it in game
code when planar movement is desired. A parent rig may already provide yaw, so a
game controller should deliberately choose which axes belong to parent and child.

`Camera.get_active()` still returns nil when rendering falls back to legacy; no
fake Entity is created. Existing acquire/release/follow/rotate, position/forward/
right/flat_forward/angles and other legacy functions retain their previous lease,
permission and yaw/pitch behavior.

To migrate FirstPersonController.lua, a project must explicitly bind a child camera
reference, select it, drive its local/world Transform, and replace legacy spatial
reads with the entity getters. Convert legacy positive-up pitch once to ECS X
(`-pitch`) if preserving old controller state. Replace legacy follow behavior with
an authored parent/child offset or game-script logic; merely selecting an ECS
camera does not redirect legacy rotate/follow. No shipped controller or game
project is automatically modified.

## Changed files and verification

- `engine/ecs/CameraState.h`: shared logical/interpolated pose composition.
- `engine/scripting/LuaRuntime.cpp`: four permission-checked read-only getters.
- `tests/CameraSystemRegression.cpp`: logical spatial, setter, ray and native checks.
- `tests/PlayerWorkflowRegression.cpp`: getters/local rotation from spawned Prefab.
- This document.

Verified on Windows x64, Release, 2026-10-24:

- `WhispEngine` and all related regression targets built successfully with MSVC
  in `out/build/x64-release` (`-j 1`). Build logs:
  `out/camera12-build.log`, `out/camera12-final-build.log`.
- Initial focused CPU run: **6/6 passed** (`out/camera12-core-tests.log`).
- Final related CTest run: **33/33 passed**, 125.39 seconds
  (`out/camera12-tests.log`). Includes ProjectSystem/Maps & Modes,
  SceneValidation, Prefab/Document/Inspector/Material editor, Physics,
  CharacterMovement, Lua gameplay/modules/inspection, interpolation, instancing
  and lighting compatibility.
- Camera System: **499 CPU checks**, **67 native DX12 checks**;
  Vulkan smoke: **5 checks**. The DX12 test checks actual HDR image changes,
  Lua origin/forward agreement with renderer state and a real gameplay ray hit.
- Camera Attachments: **2044 CPU**, **70 DX12 checks**; Player Workflow:
  **70 CPU**, **62 DX12 checks**, including a spawned child camera queried and
  rotated from its existing module-using Behaviour.
- Native camera, attachment and player tests assert **zero DX12 validation
  errors**, with validation enabled. Existing repeat Play/Stop, projection switch,
  interpolation and serialization checks remain enabled.
- Test runners restored `editor-preferences.json` and `imgui.ini` and verified
  their original bytes. Git changes contain no game projects or game resources.

Debug was not built. Vulkan verification is the existing camera smoke regression,
not the full new Lua spatial suite on Vulkan. The intermittent RMB free-fly
regression passed this run; its underlying issue was not investigated or fixed.
Existing shear/TRS limitations and dynamic-body setter restrictions remain.
No orbit, camera rig, blending/shake/collision avoidance, screen picking, weapon
pass, input framework or controller is part of this API.
