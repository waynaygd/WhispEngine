# Project System MVP — implementation and validation log

## Architecture audit (before implementation)

Branch: `project_system`. Existing changes in `ConcertController.lua`,
`FirstPersonController.lua`, and `EventBus.h` are user changes and are preserved.

* Application owns editor/runtime worlds, LuaRuntime, ResourceManager, physics,
  input and scene settings. Scene opening already stages a World before replacement.
* AssetPaths is the common resource resolver. Legacy roots are discovered from
  working-directory/executable ancestor chains, with source/staged fallback.
* AssetImport independently finds source `engine/assets`; AssetCatalog merges it
  with staged assets. This must stop for an active independent project.
* Scene management assumes `scenes/*.json` below the asset root. Scene schema v4
  serializes ComponentRegistry data, stable entity IDs and scene settings; retain it.
* Prefab, LuaRuntime, ScriptEditor and resource loaders already call AssetPaths.
  Lua Stop retires instances/ownership but previously retained compiled programs.
* Material -> shader/textures; mesh -> material slots/textures (including embedded
  textures); scenes/prefabs -> mesh/material/script/prefab keys. References use
  portable asset-relative keys; shaders use their own engine root.
* ResourceManager caches by relative key and has async finalization/watchers.
  RenderSystem additionally caches CPU handles, GPU allocations, failures, material
  and environment state. Asset preview/editor drafts/catalog/navigation also persist.
* DX12 DestroyMesh/Texture/Shader use frame retirement while recording and GPU
  waits otherwise. Keep that ownership path through ReleaseGpuResources.
* CMake stages engine assets/shaders and generates regression fixtures. Legacy tests
  run without a descriptor and must keep their current discovery behavior.
* Preferences and ImGui layout currently live beside the running editor. Project
  history belongs to those editor preferences, never a game's descriptor.

Design: immutable shared ProjectContext, prepared/validated before publication;
ProjectManager handles descriptor/create/path validation. Application coordinates
world/Lua/input/resource transitions. Existing AssetPaths delegates project content
to the active context, without changing current_path or adding a resource manager.
Project scene keys are root-relative; asset keys remain asset-relative, with the
existing lowercase category names. Shaders remain engine-owned. No fallback from
missing project content to unrelated legacy content.

Known unrelated issues requested by the specification: Group Scale X/Z and F11
ImGui warning are outside scope; validation results below must distinguish any
observed baseline failures from project-system regressions.

## Implemented architecture

`core/ProjectContext.{h,cpp}` provides an immutable published context: name,
descriptor path/root, format version, asset/scene directories, startup scene,
original descriptor JSON, and Resolve/ResolveAsset/ResolveScene/ScriptRoot APIs.
ProjectManager prepares descriptors, creates Empty Projects, atomically updates
startupScene, and publishes the active shared context. Unknown descriptor fields
survive settings updates. External descriptor edits are detected before replacement.

Project creation requires a fresh directory beneath an existing parent. Failed
creation cleans only the directory it created; pre-existing directories/files are
never adopted or overwritten. Windows descriptor writes use CREATE_NEW temporary
files, WriteFile/FlushFileBuffers/CloseHandle, then MoveFileExW. Format version 1,
mandatory fields, file size, directory separation, startup location/existence and
portable paths are validated. No process-directory changes implement switching.

Project paths reject rooted/drive-relative keys, traversal, backslashes, ADS,
reserved device names, invalid characters and symlinks/junctions/reparse points.
Read and output paths share the same boundary checks. Asset roots and scene roots
are separate; the actual existing scene extension is `.json`, schema v4.

`core/ApplicationProject.cpp` owns the transition. It prepares the complete next
World and SceneSettings before the destructive phase. Cancel does nothing; bad
descriptors, missing/invalid startup scenes and failed saves preserve the current
world/mode/selection. Transitions require an owner-thread ECS phase boundary.
Once preflight succeeds, the commit phase has no required project file reads:

1. Resolve scene/material Save or Discard; save authoring snapshots during Play.
2. Stop Play/Pause, Lua and state-machine callbacks; release cursor and mouse deltas.
3. Drain resource work/finalization to a fixed point, including material dependencies.
4. Release preview, editor sky, RenderSystem GPU resources through existing backend
   retirement/fence paths; clear queued GPU finalizations and snapshots.
5. Clear ResourceManager caches/watchers/dirty flags and increment its cache epoch.
6. Clear mutation/physics/interpolation/runtime handles and editor selection/history,
   gizmo sessions, clipboard, property/import/material/background/rename drafts.
7. Publish the new context, replace World data with a fresh World identity, apply
   scene settings and record recent project metadata.

There is one ResourceManager. Context publication is an Application lifecycle
operation; callers must not bypass it with ProjectManager::Activate during loads.
ClearAll now advances cache epoch. DrainProjectLoads waits for jobs and pumps
finalization until no newly scheduled dependency work remains. Old handles remain
valid CPU shared objects but are removed from lookup and relinquish GPU allocations.
RenderSystem's independent caches and pending uploads are reset, as is the DX12
editor-sky cache keyed by source path/version (otherwise identical keys could leak
the previous project's sky). DX12 retirement mechanisms remain intact.

Normal asset keys retain their existing asset-relative semantics, including glTF
embedded texture identifiers. No project miss falls through to engine/source assets.
Explicit read-only `@engine/` keys supply primitive geometry/materials. Engine root
search starts at the executable, independently of a game's working directory;
legacy searches remain unchanged. Shader resolution remains engine-owned.
Built-in loads do not write mesh/texture sidecar caches. File editing rejects
built-in assets and the project Asset Browser omits engine shader entries.

## Assets, scenes, prefab and Lua

AssetImport::ProjectRoot now uses the active context. AssetCatalog no longer merges
foreign source roots for a project and includes the project's separate SceneRoot in
dependency audits. Browse/import/folder creation, preview, assignment and drag/drop
reuse existing implementations. Added conservative Move for editable resource files:
same category and extension, existing destination folder, no overwrite, dependency
and dynamic-Lua-risk checks. Imported model-package files remain protected.

Save/Open/Save As and SceneCatalog use ProjectContext::ResolveScene. Startup Scene
selection validates the scene before descriptor replacement. Legacy scenes retain
their previous keys and resolver. Empty-scene Save protects an existing Untitled
destination. SceneSerializer and ComponentRegistry formats were not replaced.

Prefab::SafeAsset delegates to ProjectContext in project mode. Material and model
dependency keys continue using the same resolver, keeping references portable.
Tests cover scene components, project material/texture, prefab and Lua references
after relocation to a Unicode directory with spaces.

LuaRuntime/ScriptEditor already shared the asset resolver. Stop additionally clears
compiled programs/errors, alongside existing instances/timers/events/input/camera
ownership teardown. New project code therefore cannot reuse old compiled scripts
with an identical relative name. PlayerStart defaults retain `scripts/` naming.
The UI can copy the engine's current FPS controller into project-local scripts,
without overwriting an existing file. Lua Gameplay API/physics are unchanged.

## Project Browser and preferences

Normal no-argument executable startup shows the integrated ImGui Project Browser.
New/Open Project and Project Settings expose it through File. New Project contains
name, existing parent location and Empty template. Open includes a wide Windows
file picker. Recent stores up to 12 descriptor paths and names in editor preferences,
marks missing files and supports removal/Continue. It never auto-opens missing
content. The command line also accepts `--project` with a UTF-8 descriptor path.

Preferences and ImGui layout use the executable directory, outside game content,
so changing process cwd does not select a different preference file. Project scene
recents are cleared at transitions rather than merged across projects. Save/Discard/
Cancel handles scene and material edits, including authoring history retained during
Play/Pause. Closing the window with dirty active-project content invokes the same
confirmation. No new global keyboard shortcuts or top-menu entity creation were added.

## Changed source files

New: `core/ProjectContext.h`, `core/ProjectContext.cpp`, `core/ApplicationProject.cpp`,
`editor/EditorProject.cpp`, `tests/ProjectCoreRegression.cpp`,
`tests/ProjectSystemRegression.cpp`, this report and `docs/PROJECT_SYSTEM.md`.

Modified: root/engine CMakeLists and WhispEngine.cpp; Application.h/.cpp,
AssetPaths.h/.cpp; ResourceManager.h/.cpp, MeshLoader.cpp, TextureLoader.cpp;
RenderSystem.cpp; AssetImport.cpp, AssetCatalog.h/.cpp, EditorAssets.cpp,
EditorImportUi.cpp, EditorLayer.h/.cpp, EditorPreferences.h/.cpp,
EditorTransactions.cpp, EditorUi.cpp, SceneCatalog.h; IRenderAdapter.h,
Dx12RenderAdapter.h, Dx12EditorVisuals.cpp; LuaRuntime.cpp and Prefab.cpp.

The pre-existing changes in the two controller Lua files and EventBus.h were not
edited/reverted. No commit, push, merge/reset/clean, asset migration or user-project
test mutations were performed. Test fixtures use unique owned directories under the
build working directory and are removed by scope cleanup.

## Validation chronology

Release build: Visual Studio 2022 Community 17.14.29, MSVC x64, CMake/Ninja,
existing `out/build/x64-release` configuration (`WHISP_BUILD_PHYSICS_TESTS=ON`,
`WHISP_BUILD_LUA_TESTS=ON`, DX12). Logs are in ignored `out/`.

* Project Core initial validation found Windows `/absolute` root-directory semantics;
  corrected the check to has_root_path. Core then passed.
* Assets/scenes CPU integration + ResourceRegression: 3/3 passed, 1.12 s.
* Initial Lua checks in sandbox: controller passed, API/Scripting failed on
  weakly_canonical Access is denied while preparing fixtures. Unsandboxed repeat:
  LuaScripting passed; API reached an actual inherited script expectation failure.
* Initial Project System CPU/DX12/UI run: 3/3 passed, 5.56 s.
* Additional descriptor-conflict protection exposed a Windows open-reader replacement
  error in the first full CTest. Fixed by closing the validation stream before
  MoveFileExW. Also corrected test error-message lifetime so future errors are readable.
* Expanded final profile: 3/3 passed, 10.54 s, including real project-local FPS
  controller/camera, Lua hot reload, Save from Pause, and retired GPU texture handles.

First full CTest (before the open-reader fix): **49/54 passed, 207.67 s**.
Failures: ProjectSystemRegression and ProjectSystemDx12Regression (descriptor lock,
fixed); LuaPhysicsEventsDx12Regression (`native Trigger Exit once`);
LuaGameplayApiRegression (`Concert rotates runtime referenced globe`);
LuaGameplayApiDx12Regression (`Hierarchy drag starts`).
Preserved log: `out/project-ctest-full-first.log`.

The API CPU failure is explained by the existing user change: ConcertController
now starts inactive and waits for `start_concert`; the old test expects immediate
rotation of 0.05 radians after Update(0.1). No test assertion or user script was
weakened to hide that mismatch. Trigger/drag native failures are timing/input-related
candidates, to be reported with their repeat outcomes rather than assumed fixed.

Second full CTest: **52/54 passed, 217.65 s**. All Project System tests passed.
LuaPhysicsEventsDx12Regression passed in this complete repeat. The two remaining
failures were LuaGameplayApiRegression (Concert expectation above) and
LuaGameplayApiDx12Regression (`Hierarchy drag starts`, again before its subsequent
Concert assertion). This was a full repeat, not an aggregate of isolated passes.
Log: `out/project-ctest-full-final.log`.

Final review added a regression for the actual window-close action: Cancel retains
the scene; Discard/Save confirmation ends Application::Run. Also added filesystem
identity checks against Windows case aliases of overlapping asset/scene roots and
an oversized numeric version rejection. Final Release rebuild succeeded with
exit code 0 (`out/project-release-verified-build.log`). The expanded profile passed
3/3 in 10.69 s (`out/project-profile-verified.log`), with zero DX12 validation
ERROR/CORRUPTION messages. A final complete CTest follows these last changes.

Final complete CTest on the delivered source: **50/54 passed, 215.46 s**, exit 8.
`out/project-ctest-full-verified.log` preserves the complete run. ProjectCore,
ProjectSystem CPU and ProjectSystem DX12 all passed (0.08 / 0.77 / 10.00 s).
ResourceManager, scene/prefab/editor workflows, multi-selection, Fog, controller,
character movement, materials, shadows, IBL and other named tests passed except:

| Test | Final complete run | Isolated repeat(s), unchanged binaries |
| --- | --- | --- |
| LuaPhysicsEventsDx12Regression | Fail: native Trigger Exit once | Fail 4.35 s, then pass 4.79 s |
| LuaGameplayApiRegression | Fail: Concert rotates runtime referenced globe | Fail 3/3 (0.43 / 0.43 / 0.46 s) |
| LuaGameplayApiDx12Regression | Fail: Hierarchy drag starts | Fail 3/3 (3.53 / 3.42 / 3.53 s) |
| Editor25Dx12Regression | Fail: randomized ordinary/instanced equivalence | Pass first isolated repeat, 15.39 s |

Repeat log: `out/project-ctest-isolated-repeats.log` (until-pass, maximum 3 attempts).
The trigger test also passed in the second complete run; Editor25 passed in the
first and second complete runs. Their observed intermittency is established;
wall-clock/input/async-finalization sensitivity is suspected, not proven as the
root cause. The hierarchy-drag failure is reproducible and unresolved. It runs in
legacy/no-project mode; no before-change baseline was captured, so this report
does not certify that native failure as pre-existing. The CPU Concert mismatch
is directly explained by the preserved user-script diff.

These repeats are **not** reported as a clean full suite. Feature implementation
and profile verification are complete, but full regression acceptance is **not
green**. No assertions/tests were disabled and no unrelated physics/rendering or
user-controller behavior was changed to force a pass.

Final `git diff --check`: exit 0 (only Git CRLF normalization notices). The working
branch remains `project_system`; there are no commits or pushes. An existing
EditorLevelWorkflow regression left one uniquely named `editor24-hospital-*`
fixture below source assets during its second full run. Its absence from the
initial working tree, matching fixture constructor, creation time and untracked
contents were verified; only that generated directory was removed after all tests.
All new project-test fixture directories were removed automatically.

## Limits and remaining manual checks

Empty Project is the shipped template; optional 3D Starter is not implemented.
Project name/directory editing is through descriptor with the project closed;
the UI displays these settings and changes startupScene. Preferences require a
writable executable directory. Untitled save while in Play uses the default fresh
filename; a collision requires Stop/Save As. Recents retain stale moved paths until
removed. Legacy workspaces are not automatically migrated.

Automated relocation uses a different Unicode path on the same volume. A physical
C-to-D move and a different machine were not available. Real permission denial,
process termination during creation and device-removal/OOM fault injection are not
claimed tested. Windows symlink fixture creation lacked the required privilege;
path code rejects reparse points, but a successful symlink/junction fixture remains
a manual check. New loading has all descriptor/scene I/O before commit; arbitrary
device-loss/allocation failures during commit do not have a transactional GPU rollback.

Existing controller tests cover WASD/mouse/input ownership. Project tests separately
execute the real copied FPS controller and verify runtime camera ownership; they
also test same-name different project Lua programs. They do not claim a full manual
gameplay playthrough. Group Scale X/Z was not changed. Existing multi-selection,
Fog and editor workflow tests are retained; F11 is not independently certified by
a new manual test here.

Future work: dedicated descriptor settings editing, optional starter templates,
safe explicit legacy migration, per-project recent scenes, package-aware model
relocation, crash-recovery journals, read-only installation preference directories,
and export consuming the context's name/startup/content/script roots. No packaging
pipeline, asset bundle system or runtime executable export was added.
