# Maps & Modes / GameMode 1.0

## Architecture and audit

Existing `startupScene` remains the Editor Startup Map, loaded by the existing
project-open transaction. Ordinary Play clones the current authored World.
Player Workflow already stages the clone and spawn before publishing runtime
state or starting Lua. Prefab loading, reference remapping and rollback remain
in the existing Prefab implementation. The Project Settings document retains
its one draft, manifest save and project/close guards.

The project manifest now supports a `mapsAndModes` object:

```json
{
  "gameStartupMap": "Scenes/Game.json",
  "defaultGameMode": "Default",
  "defaultPlayerPrefab": "prefabs/player.prefab.json",
  "modes": {
    "Default": { "defaultPlayerPrefab": "", "autoSpawnPlayer": true }
  }
}
```

GameModes are typed named configurations in the manifest, not ECS entities or
another resource/runtime system. Old manifests default to no selected mode,
no project Player Prefab and no separate Game Startup Map. Unknown JSON fields
are retained on save. Paths use existing project containment/extension checks;
assigned resources are checked at save, with full scene/prefab load at runtime.

## Player resolution

PlayerStart `spawnMode` values:

- 0 Compatibility: an assigned Player Prefab means override, empty means legacy.
  Missing fields in old scenes retain this value. Defaults never reinterpret
  an old empty assignment as GameMode inheritance.
- 1 Legacy: uses Controller Script / Character Height / Eye Height only.
- 2 Prefab Override: requires this marker's assigned Prefab.
- 3 GameMode Default: selected mode's nonempty Player Prefab, then project fallback.
  Missing effective Prefab is an error, never a legacy fallback.

The first enabled PlayerStart with Transform by Entity index is selected.
No enabled marker means no implicit player, including with Auto Spawn enabled.
A selected GameMode with Auto Spawn disabled suppresses player creation, including
legacy/override markers. Runtime markers are still removed as before. A selected
unknown mode is an error before Lua. Unselected mode configurations do not control
runtime spawning. With no selected mode, explicit GameMode Default may still use
the project fallback. Spawn transform, identity, physics, Lua references, child
cameras and Camera Attachments use Player Workflow 1.0 unchanged.

## Editor and game maps

Project Settings > Project / Maps & Modes edits both maps with the existing
scene catalog, offers named mode selection, creates a Default mode configuration,
edits Auto Spawn and Prefab defaults with the existing resource picker/clear/reveal
controls, and displays the effective default. Editor Startup Map also remains
accessible in General; both controls edit the same `startupScene` value.

Ordinary Play always uses the current scene. The Scene menu additionally
offers Play From Game Startup Map. This loads the game map into the staged runtime
copy and applies its scene settings only after preparation succeeds. Empty Game
Startup Map falls back to Editor Startup Map. Authoring scene data, settings and
Undo are retained for Stop. No standalone launcher, packaging or Level Travel is
claimed. A missing/corrupt game map or failing spawn leaves Play stopped and the
authoring World intact with the existing Play failed diagnostic.

## Scope and validation

Scene GameMode Override is deferred: no scene serialization fields or second
resolution policy were added. Multiplayer, GameState, PlayerController, Level
Travel and camera/input controllers remain outside this stage.

Verified on Windows, 2026-10-24, using the existing x64-release configuration:

- Release WhispEngine and related regression targets built successfully:
  `out/maps1-build.log`, `out/maps1-final-build.log`. The final ProjectSystem
  executable including additional preview assertions was rebuilt successfully
  (`out/maps1-project-build.log`). No Debug build result is claimed.
- Focused CPU run: **6/6 passed**, 3.70 seconds (`out/maps1-core-tests.log`).
  Player Workflow completed **70 checks**, covering mode/project fallback,
  explicit override, explicit Legacy, old empty assignments, Auto Spawn disabled,
  no-marker behavior and missing-mode diagnostics, together with existing real
  Prefab/Lua/reference/physics tests. Camera Attachments completed 2044 checks,
  Camera System 61 and LuaScripting 247 (including module regressions).
- Related CPU/native run: **33/33 passed**, 148.54 seconds (`out/maps1-tests.log`).
  Includes ProjectSystem, SceneValidation, document/Prefab workflow and restart,
  Player Workflow CPU/DX12, Camera System CPU/DX12/Vulkan smoke, Attachments CPU/DX12,
  physics/movement, interpolation/instancing, Lua/API/inspection/modules, Runtime UI,
  lights, Inspector and Material Editor. Native tests retain their zero DX12
  Validation Error assertions. The periodic RMB test passed in this run; no fix
  for that unrelated issue is claimed.
- Final stronger ProjectSystem CPU/DX12 run: **2/2 passed**, 16.88 seconds
  (`out/maps1-project-tests.log`). Covers Maps & Modes document Save/reopen,
  unknown nested mode JSON preservation, unsafe paths and read-only manifest,
  a distinct configured game map with a unique Entity, restored authoring data
  and Undo, ordinary Play ignoring the game map, and failed preview of a missing
  game map preserving the authored World with a concrete diagnostic.
- The first focused run failed the pre-existing Unicode-directory move check:
  editing changed the test source's string encoding. The directory name is now
  expressed with explicit UTF-8 bytes, preserving the actual Unicode-path test.
  The corrected run passed; no assertion was removed or weakened. Initial log:
  `out/maps1-core-tests-initial.log`.
- Original preference/layout bytes were restored and verified by the runners.
  `git diff --check` passed. Source assets and user game projects were unchanged;
  no branch switch, commit or repository cleanup was performed.

Tests use owned generated projects and Prefabs, never user game resources.
Vulkan coverage is the existing Camera smoke test, not a claim of a native
Maps & Modes preview test or DX12/Vulkan feature parity.

Changed files: `engine/core/ProjectContext.h/.cpp` (typed manifest configuration),
`engine/core/Application.h/.cpp` (staged game-map preview and mode-aware spawn),
`engine/ecs/components/PlayerStartComponent.h`, `engine/ecs/PlayerSpawn.h`,
`engine/ecs/ComponentRegistry.cpp` (compatible modes and resolution),
`engine/editor/EditorProjectSettings.cpp`, `EditorInspector.cpp`, `EditorUi.cpp`,
`LightGizmos.cpp`, `SceneValidation.cpp`
(configuration, relevant properties and launch action),
`tests/PlayerWorkflowRegression.cpp`, `tests/ProjectSystemRegression.cpp`,
and this document.

Build note: the first four-job Release attempt was unusually slow. Read-only
inspection reported about 750 MB free of 8 GB physical memory. The verified build
process and its compiler children were stopped, and the same incremental build
was restarted with one job (`out/maps1-build.log`). This is a build-resource
adjustment, not a successful validation result.
