# Scriptable Gameplay Controller

Gameplay behaviour is an ordinary `LuaScriptComponent` on an Entity. C++ owns
physics, collision/grounded, input sampling, cursor safety and interpolated camera
placement. Lua owns bindings, movement decisions, jump requests and camera look.
There is no C++ FPS controller running alongside the script. Each new Play
session reads the current active script assets. During Play, use the existing
Reload Script action; a rejected reload keeps the previous valid program.

## Quick start in the Editor

1. Create a Lua asset in Asset Browser, or open
   `scripts/FirstPersonController.lua` in Visual Studio.
2. Use **Create → Player → Player Start**. Its Position denotes the feet.
3. In Player Start, choose **Controller script**. The default is
   `scripts/FirstPersonController.lua`. Empty means no script/controller.
4. Play. The runtime player receives the selected asset in a normal
   LuaScriptComponent, with input and camera permissions enabled. The marker is
   removed only from the runtime copy.
5. Click the game Viewport to capture the mouse. WASD moves, Space jumps, Tab
   releases the mouse, Escape stops Play. Stop restores the editor scene, camera,
   selection/history and input bindings.
6. Replace the controller asset with `scripts/KeyboardTurnController.lua` and
   Play again: WASD moves, Q/E turns, mouse motion does nothing. This controller
   does not capture the cursor; keep the pointer in the game Viewport for input.

For an existing character Entity, add Transform, CharacterMovement, dynamic
Rigidbody and a manual Box Collider. Use an upright body, enable gravity and use
world-sized half extents (the default spawned player uses 0.30/0.85/0.30).
Attach Lua Script, choose the asset, and explicitly enable **Allow gameplay
input** and **Allow gameplay camera** where needed. Neither permission is
automatically granted to arbitrary scripts. Autonomous NPC scripts can issue
Character commands without input or camera permissions.

The FPS script exposes Move Speed, Jump Speed and Mouse Sensitivity through the
existing Lua `fields` schema. On authoring Entities, edit these in Lua Script's
Inspector properties. PlayerStart's minimal spawn configuration selects the
script asset and uses its field defaults; it does not yet select a character
prefab or store a second set of Lua properties. Edit the selected script's
defaults for such spawns. All this works without recompiling C++.

## Lifecycle and execution order

Scripts return a behaviour table, using `self.entity` and optional `fields`:

```lua
return {
    fields = { speed = {type="number", default=4, min=0, max=30} },
    on_start = function(self)
        Input.bind("MoveForward", "W")
        Input.bind("Jump", "Space")
        Character.set_move_speed(self.entity, self.speed)
    end,
    on_update = function(self, dt)
        local z = Input.is_down("MoveForward") and 1 or 0
        Character.move_local(self.entity, 0, z)
        if Input.was_pressed("Jump") then Character.jump(self.entity) end
    end,
    on_fixed_update = function(self, dt)
        -- Optional decisions using the previous completed tick's grounded state.
    end,
    on_stop = function(self)
        if World.is_alive(self.entity) then Character.stop(self.entity) end
        Input.capture_cursor(false)
        Camera.release()
    end
}
```

Per frame: poll devices once → editor input safety → Lua `on_update` → zero or
more fixed ticks → interpolated camera placement → render. Each fixed tick:
Lua `on_fixed_update` → apply Character commands → PhysicsSystem → contacts and
grounded → capture render pose. Collision events retain the existing `on_event`
convention and are delivered in the next Lua update.

`on_stop` and the existing `on_destroy` run when an instance is removed,
disabled, replaced/reloaded, destroyed, or stopped. Commands and owned camera/
cursor leases are cleared even after a Lua exception. Structural destruction
still uses the existing deferred mutation boundary, never a second ECS or VM.

## Input API

| Call | Result |
|---|---|
| `Input.bind(action, keyName)` | Bind a runtime action; requires input permission |
| `Input.is_down(action)` | Held state from this frame's device snapshot |
| `Input.was_pressed(action)` | Press edge |
| `Input.was_released(action)` | Release edge |
| `Input.mouse_delta()` | Two numbers, relative X/Y; zero when unavailable |
| `Input.cursor_captured()` | Whether this script owns the captured game cursor |
| `Input.capture_cursor(true/false)` | Request capture/release; returns success |

Key names: `A`–`Z`, `0`–`9`, `Space`, `Tab`, `Escape`, `Shift`, `Ctrl`,
`Up/Down/Left/Right`, `MouseLeft`, `MouseRight`. C++ can also configure actions
with InputManager's existing BindAction. Action names are shared within a Play
session; bindings are restored on Stop. Tab/Escape remain reserved editor
safety controls regardless of script bindings.

Legacy `is_action_held/pressed/released` remain aliases, subject to the same
explicit input permission. The bundled wave controller has that permission.

All scripts see the same edge within a phase. Frame edges are visible in
`on_update`; fixed edges accumulate until the first fixed tick, then are cleared
after **all** fixed callbacks. Subsequent ticks do not repeat them. A press and
release on separate frames with no intervening tick are both available at the
next tick. Bind fixed-only actions on start, or configure them in InputManager.
Do not implement the same behaviour in both callbacks unless intentional.

Text editing/active ImGui controls, modal tools, focus loss and Pause suppress
game input and player commands. Cursor capture additionally requires the
focused game window and a valid Viewport. Capturing clears the mouse baseline
to avoid cursor-warp look jumps. Lua cannot force capture over Inspector text.

## Character API

| Call | Meaning |
|---|---|
| `Character.move_world(entity, Vec3.new(x,0,z))` | Horizontal world-space desired movement |
| `Character.move_local(entity, x, z)` | Horizontal movement relative to body yaw |
| `Character.jump(entity)` | Queue one jump request for a valid ground contact |
| `Character.stop(entity)` | Clear horizontal command and pending jump |
| `Character.is_grounded(entity)` | Valid supporting contact from C++ physics |
| `Character.get_move_speed(entity)` / `set_move_speed(entity,n)` | Speed, 0–30 |
| `Character.get_jump_speed(entity)` / `set_jump_speed(entity,n)` | Jump speed, 0–20 |
| `Character.velocity(entity)` | Current horizontal Rigidbody velocity as Vec3 |
| `Character.eye_offset(entity)` | Offset from body origin to the eye point |

Commands apply to the calling script's own living Entity. Required components:
CharacterMovement, Transform, Rigidbody and Collider. Dead, stale, different-
World or other characters are rejected with a protected Lua error. Numbers
must be finite and within bounds. Movement strength is capped at one, so
diagonal input cannot accelerate the character; smaller values support analog
strength. Y is ignored: gravity and jump own vertical velocity. Commands persist
through multiple fixed ticks in the frame. Jump survives frames without ticks.

No Transform.position writes are needed for locomotion. Disabling/removing or
failing the script clears its commands and horizontal speed; gravity and
collisions continue. Rotation locking and the previous anti-tipping fix remain
inside the existing C++ solver. NPC scripts without input permission may still
run autonomous behaviour; editor input gating does not pause the whole world.

## Camera API

Camera functions require **Allow gameplay camera**. There is one explicitly
claimed game camera, not one implicit controller for every character:

| Call | Meaning |
|---|---|
| `Camera.acquire()` | Claim camera if free/already owned; returns boolean |
| `Camera.release()` | Release this script's camera lease |
| `Camera.angles()` | Yaw, pitch in radians |
| `Camera.set_angles(yaw,pitch)` | Set orientation |
| `Camera.rotate(deltaYaw,deltaPitch)` | Relative orientation change |
| `Camera.forward()` | Full forward Vec3 |
| `Camera.flat_forward()` / `Camera.right()` | Horizontal navigation vectors |
| `Camera.position()` / `Camera.set_position(Vec3)` | Read/set position; setting exits follow |
| `Camera.follow(entity, worldOffset)` | Follow a living target with world-space offset |

Yaw wraps, pitch is clamped to the existing ±89° camera limit. Follow resolves
the target's RenderInterpolation pose **after** physics. Lua can change the
offset each frame for a TPS camera, or use explicit positions for a platformer.
The API never consumes mouse input or assumes FPS behaviour. Example:

```lua
if Camera.acquire() then
    Camera.follow(self.entity, Character.eye_offset(self.entity))
    if Input.cursor_captured() then
        local dx, dy = Input.mouse_delta()
        Camera.rotate(dx * self.sensitivity, -dy * self.sensitivity)
    end
end
```

Only one script can hold the camera/cursor lease; failure or destruction frees
it. Scripts should retry acquire if another controller previously held it.
Stop always releases the native cursor and restores the saved editor free-fly
camera. The host capability boundary can later provide raycasts/interactions
without introducing a parallel Lua runtime. No Interact system is added here.

## Validation and limitations

Automated tests exercise real Lua scripts, InputManager snapshots and the real
PhysicsSystem: FPS/alternative control, frame/fixed edges, zero/multiple ticks,
diagonal speed, held jump, script removal/disable/errors, input gating,
permissions, entity lifetime and camera lease release. A DX12 integration test
runs Lua movement, landing, interpolated following, Pause and isolated Stop.

Manual desktop tests are still needed for physical mouse capture/raw input,
WASD/Space/Tab/Escape, text-field editing and native focus transitions on the
user's display. The controller remains the existing dynamic Box: no stairs,
capsule sweeps, moving-platform attachment, camera collision or automatic
prefab selection is introduced.
