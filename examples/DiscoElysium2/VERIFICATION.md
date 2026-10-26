# Verification — 2026-10-26

Project installed at `D:\WhispEngineProjects\DiscoElysium2`.
Branch: `engine_workflow`. Initial Git status was clean. No commit was created.

## Launch

Open `DiscoElysium2.whispproject` through Project Manager, then Play.
Alternatively, from this engine checkout:

```powershell
& .\out\build\x64-release\WhispEngine.exe --project 'D:\WhispEngineProjects\DiscoElysium2\DiscoElysium2.whispproject'
```

LMB moves or approaches an NPC/object. J opens the journal, C opens skills,
Z/X zoom. The dialogue's Leave button closes it. Escape stops Play.
The README provides a short investigation walkthrough.

## Final results

| Check | Result |
|---|---|
| MSVC x64 Release WhispEngine and regression targets | Passed |
| Related CTest run | 20/20 passed, 71.50 seconds |
| New CPU data/navigation/gameplay integration | 1,869 checks passed |
| New DX12 application integration | 100 checks passed |
| New Vulkan project rendering smoke | 9 checks passed |
| Direct opening of installed D: project | 99 checks passed |
| DX12 validation, enabled through WHISP_DX12_DEBUG=1 | Zero errors |
| Vulkan Khronos validation layer | Enabled, zero errors |
| git diff --check | Passed |
| Protected files/preferences/workspace SHA-256 comparison | 425 checked, zero changes |
| Installed authored files compared with example source | Identical |

CPU integration loads the real project and runs its real Lua Behaviours, camera
rays, A* routes, Character movement, physics, Canvas and pointer events. It checks
approach to all three NPCs and the receipt, response selection, success and failure
checks, the alternate testimony route, both endings, journal/skills toggles, stopping
at interaction range and fresh-session reset. The scene is saved/reloaded in an
owned copy; the v3 inherited NPC prefab is resolved, instantiated and serialized.
Data tests include type/range/enum/resource validation, malformed schemas on empty
tables, duplicate IDs, CSV quoting/multiline Cyrillic, invalid CSV, ResourceManager
caching/hot reload/last-good data, Vec3 in Lua and mutation isolation.

DX12 opens the project through Application/Project Manager, opens both data
Inspectors, enters Play, selects the orthographic camera and reads actual rendered
pixels. Injected pointer input drives the real scheduler and physics to approach
Marta, open her portrait/dialogue, select a response and close it. The test checks
UTF-8 text wrapping, repeat Play/Stop, restored authored scene, retired UI handles
and project reopening. These operations are repeated on the installed D: copy.

Related tests cover ProjectCore/ProjectSystem, CharacterMovement, PrefabInheritance,
RuntimeUI, WidgetDesigner, PlayerWorkflow, CameraSystem, LuaScripting and AssetBrowser
(including restart). Initial runs with user-specific preferences had GUI fixture
failures. The final run uses the repository's established isolated-preference
approach and restores/verifies the original bytes. ProjectCore's junction/symlink
fixture requires execution outside the restricted sandbox.

Vulkan is a real scene/HUD rendering smoke with the validation layer, rather than
the full narrative interaction suite on that backend. Level Travel is not used.
The full repository test suite and Debug configuration were not run.
Existing C4828 warnings in CharacterMovementComponent's legacy comment encoding
remain; this work did not rewrite that unrelated header.

## Engine changes

- `engine/resources/DataResource.h/.cpp`: immutable typed JSON definitions,
  schema/value validation, atomic save, bounded CSV interchange.
- `engine/resources/ResourceManager.h/.cpp`: data caches/load/reload/watch/clear,
  last-good snapshots and cached CPU-byte estimates.
- `engine/ecs/NavGrid.h`: bounded flat static-world A* with actor clearance.
- `engine/ecs/CameraState.h`: projection-consistent normalized screen rays.
- `engine/scripting/GameplayServices.h`, `LuaRuntime.cpp`, `core/Application.cpp`:
  viewport pointer capability, Data API, navigation API, camera rays and ortho zoom.
- `engine/editor/EditorData.cpp`, `EditorLayer.h`, `EditorInspector.cpp`,
  `EditorProject.cpp`, `EditorAssets.cpp`, `AssetCatalog.cpp`: creation/indexing,
  typed Inspector, row editor, schema editing and project draft lifecycle.
- `engine/core/ProjectContext.cpp`: data folder for newly created projects.
- CMake registration, OBJ tracking exception for examples, new regression target,
  reproducible asset generator and isolated verification runner.

At delivery Git contains 15 modified tracked files and 39 new files (engine
extensions, example project, test and tools). Nothing is staged or committed.
No DOOM67 or other user project was edited. The protected hash set covers all
existing files under DOOM67, ProjectA and test_projects, plus found editor
preferences/workspace files. No separate Whisp Woods directory was present in
the requested D: projects directory; no operation targeted it.

## Evidence and reproduction

Engine checkout evidence:

- `out/narrative-build.log`
- `out/narrative-verified-tests.log`
- `out/narrative-installed-tests.log`
- `out/narrative-protected-verification.json`
- `out/narrative-final-git-status.txt`
- `out/narrative-dialogue.png` and raw `narrative-dialogue.ppm` in the build folder

`tools/RunNarrativeVerification.py --installed <descriptor>` reproduces the
isolated run after the listed targets have been built. It restores preferences
in a finally block, including on test failure.

## Limits

Primitive-based artwork, fixed camera rotation, flat static navigation, four
choices per node, and the latest three history excerpts. No savegame, character
animation, voice acting, combat, stairs/multilevel navigation or dynamic obstacle
avoidance. Progress resets on Stop. Native input tests are deterministic injected
actions through the real runtime, not manual OS mouse interaction.
See ENGINE_API.md for the complete format, API and architecture audit.
