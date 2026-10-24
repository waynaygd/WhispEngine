# Level Travel 1.0

## Audit and architecture

Ordinary Play already clones the authored World and stages PlayerStart spawning;
Game Startup Map Play loads a project scene before the same spawn step. SceneSerializer
validates/restores data transactionally. ProjectContext.ResolveScene provides portable
project-relative path, scene-directory, JSON extension, containment and link checks.
PlayerSpawn already implements Legacy, Prefab Override and GameMode Default, including
Auto Spawn and no-marker behavior. These paths are reused, not replaced by a manager.

`PreparedRuntimeLevel` / `PrepareRuntimeLevel` in `core/RuntimeLevel.h` extract that
data-only preparation for all three entry points. No runtime systems are touched
until scene load, player spawning and hierarchy validation succeed. The Application
owns one pending path, independently of the requesting Behaviour's lifetime.

## Lua API and request policy

```lua
local accepted = World.load_scene("Scenes/Level02.json")
```

This requests a complete replacement of the runtime level. It never destroys a
World inside a Lua callback. It requires an active Play host and open project;
there is no new file access permission or sandbox escape. It is rejected during
whole-runtime Stop, for dead/destroy-pending owners, and without a Play host
(for example, module inspection). Existing World mutation rights,
camera/input permissions, Script.require and hot reload are unchanged.

Paths are relative to the current project (not Assets); use its configured scene
directory and `.json` files. Invalid/unsafe paths raise a Lua diagnostic immediately.
Missing or corrupt files and spawn failures are diagnosed when staging at the frame
boundary. Request validation and staging both use the existing safe resolver.

The first path-valid request wins until the next boundary and returns true. Later
path-valid requests return false, even for the same path. Invalid paths still raise
an error and do not replace a pending request. A missing first scene does not fall
back to a later request. A request remains accepted if its Behaviour is subsequently
destroyed. Callback iteration/delivery order is the existing Lua runtime order;
the queue does not introduce another ordering policy.

At the start of the next GameUpdate phase, before gameplay input, UI event dispatch,
Lua Update and fixed physics ticks, Application processes the request. Requests from
Update, timers, events, UI and fixed callbacks therefore wait for that boundary.
Paused Play can process an already queued request; pause remains set. Stop clears
pending travel. Project changes use the existing Stop lifecycle and cannot carry a
pending request into the next project.

## Preparation, publication and callbacks

1. Move the pending path into a local value; load a new data-only World through
   SceneSerializer. The running World and settings remain published throughout.
2. Apply the current project's GameMode through the same SpawnPlayer function as
   ordinary Play. Create the complete Player Prefab and remap references before Lua.
   Validate hierarchy. No marker means no implicit player; Auto Spawn can suppress
   creation. A player is recreated, with no inventory/HP/session transfer.
3. If preparation fails, discard staging, log the concrete error, retain the old
   level, Lua, physics, camera, UI and Play state. `GetLevelTravelError()` retains
   the diagnostic until a successful travel or Play/Stop. A new request can retry.
4. On success call old Lua Stop while the old World still exists. Existing retirement
   calls on_stop then on_destroy per instance and cancels timers/control. Callback
   errors follow normal Lua diagnostics and do not abort teardown. Stop clears
   events, timers, module/instance caches, mutations, postfx and Runtime UI, including
   its handle session. No callback is running while the World pointer changes.
5. Clear application mutations, interpolation, physics contacts/triggers/broadphase,
   collision pairs and fixed clock; release capture and restore pre-Play action
   bindings. Drop runtime editor selection/history while preserving authored history.
6. Publish the prepared World and scene settings; invalidate old clone mapping,
   player/follow/selected camera handles and debug/stress entity lists. Initialize
   legacy eye pose using the existing PlayerStart convention when applicable.
7. Start Lua on the new World through its existing Start/Sync lifecycle. It discovers
   instances and calls on_start at the next Sync (normally UI dispatch in this
   same frame, then Update/FixedUpdate), including for the newly spawned player. As with ordinary Play, Lua callback errors are per-Behaviour
   diagnostics after publication, not a rollback of a successful data load.

World identity changes on each successful transition, including A -> B -> A.
Entity handles are never remapped from the previous runtime. Old references cannot
resolve against the new World even if an Entity index is reused.

## Editor, camera and rendering

Authoring data, document path, saved settings and Undo/Redo are never replaced by
travel. The editor remains in Play and displays the new runtime World. Stop restores
the original authored scene and editor pose; existing runtime Save guards remain.
Ordinary Play and Play From Game Startup Map keep their existing meanings.

Camera selection/follow state and the cached CameraState are reset before the new
level is rendered. Selection is resolved from the new ECS World or legacy fallback.
Camera Attachments still consume that final render state; interpolation starts empty.
Fog, lighting and scene settings come from the newly loaded scene. Lua Start rebuilds
Runtime UI against the new World. The existing RenderSystem rebuilds frame snapshots
and packets before drawing. ResourceManager and renderer-owned GPU caches remain
shared within the same project: no stale World/component pointers are transferred,
and no per-travel resource manager or GPU destruction policy is introduced.

## Example

```lua
return {
    on_event = function(self, event)
        if event.type == "exit_to_B" then
            World.load_scene("Scenes/B.json")
        elseif event.type == "exit_to_A" then
            World.load_scene("Scenes/A.json")
        end
    end
}
```

The receiving scene must contain its own Behaviour to request a return transition.
Code following load_scene in the current callback still runs in the old level.

## Scope and validation

This is synchronous staged loading at a frame boundary, not async loading or
streaming. There is no loading screen, persistent player/world, session framework,
savegame, multiplayer, sublevel or state transfer. Asset loading retains the existing
asynchronous ResourceManager behavior; travel does not wait for every texture upload.

Verified on Windows x64, 2026-10-24:

- Release `WhispEngine`, `LevelTravelRegression` and related targets built with
  MSVC, `-j 1`. Logs: `out/travel1-build.log`, `out/travel1-final-build.log`,
  `out/travel1-test-fix-build.log`, final `out/travel1-compat-build.log`.
- Initial focused CPU run: **7/7 passed** (`out/travel1-core-tests.log`).
- Corrected focused Level Travel run: **2/2 passed**, 8.66 seconds
  (`out/travel1-target-tests.log`).
- Final related CTest run: **35/35 passed**, 160.13 seconds
  (`out/travel1-tests.log`). Includes ProjectSystem/Maps & Modes, Player Workflow,
  Camera System/API, Camera Attachments, Lua runtime/modules/gameplay/inspection,
  Physics/CharacterMovement, hierarchy, Scene/Prefab serialization, document and
  editor restart workflows, Runtime UI, interpolation, instancing and lighting.
- Level Travel completed **33 CPU checks** and **135 native DX12 checks**.
  CPU covers shared staging, GameMode/default/legacy/no-marker spawn, load/spawn/path
  failures, new World identities, Lua requests/retirement/timers/events and physics
  reset. Native checks exercise the actual Application frame boundary, Update/timer/
  event requests, first-request-wins, requesting Behaviour destruction, running and
  paused Play, failures retaining the old camera/level, source-file integrity,
  pending mutation/UI handle invalidation, repeated Play/Stop, project changes,
  authored Undo restoration and HDR image differences.
- Native Level Travel asserts **zero DX12 validation errors**. Related camera,
  attachment, player and project tests retain their validation assertions.
  Camera System: 499 CPU / 67 DX12 checks; Attachments: 2044 CPU / 70 DX12;
  Player Workflow: 70 CPU / 62 DX12; LuaScripting: 247 checks.
- The first focused DX12 attempt failed because the test used `pcall`, which the
  existing sandbox does not expose. The test now verifies the normal diagnostic
  path and directly checks that Stop-time requests never reach the host; no sandbox
  permission or assertion was weakened (`out/travel1-target-tests-initial.log`).
- The first compatibility run was **34/35**: shared staging had lost the existing
  Game Startup Map diagnostic label. The production error context was restored,
  then all 35 tests passed (`out/travel1-tests-initial.log`).
- Test runners restored and verified the original `editor-preferences.json` and
  `imgui.ini` bytes. Generated fixtures belong to the tests; game projects/resources
  were not edited. `git diff --check` passed; no branch switch, commit or cleanup.

Debug was not built. Vulkan coverage is the existing Camera System smoke test,
not a native Level Travel Vulkan regression. The intermittent RMB free-fly test
passed in these runs; no fix for that unrelated issue is claimed. Lua on_start
errors after publication follow ordinary Behaviour diagnostics, not a level rollback.

Changed files:

- `engine/core/RuntimeLevel.h`: shared scene/clone/spawn preparation.
- `engine/core/Application.h` / `.cpp`: request queue, boundary publication,
  diagnostics and runtime teardown/reset.
- `engine/scripting/GameplayServices.h`: deferred scene-load host capability.
- `engine/scripting/LuaRuntime.cpp`: `World.load_scene` binding and lifecycle guard.
- `tests/LevelTravelRegression.cpp`: owned CPU and real DX12 fixtures/workflows.
- `CMakeLists.txt`: Release-compatible regression target and serial CTest entries.
- `docs/LevelTravel1.md`: architecture, API, limitations and verification.
