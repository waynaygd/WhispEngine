# Editor Layout Presets

Use **View -> Workspaces** to select **Level Editing**, **Scripting**, **Asset Management** or **Gameplay Debugging**. The active workspace is checked in the menu. **Save Current Layout** saves its current docking, floating-window geometry and panel visibility. **Reset Current Layout** restores only that workspace's builtin arrangement and required visibility. The existing **View -> Reset Layout** is an alias for that reset.

Level Editing emphasizes the central Viewport, with Hierarchy left, Inspector right and Asset Browser/Console below. Scripting adds Lua Runtime Inspector on the right and Console/Gameplay Events below. Asset Management gives most space to the existing Asset Browser and places a smaller Viewport above the Inspector/Hierarchy tabs on the right. Gameplay Debugging exposes Events below, Lua Runtime Inspector right, and Scene Validation/Console as regular tabs. All windows remain closable, reopenable through View, resizable and dockable. Scripting is also usable in Edit Mode, where the runtime panel explains that Play is required.

Storage uses ImGui's native `SaveIniSettingsToMemory` / `LoadIniSettingsFromMemory`, embedded as strings in the existing `editor-preferences.json` beside the executable. Each of the four workspace keys has its own native ini and visibility map; the active key is also persisted. Saving uses the existing atomic preferences writer. No scene/project format is changed and no Entity handles or Lua references are stored. Other editor preferences, drafts, selection, Undo, World and gameplay history are independent of workspace application.

Explicitly save custom changes before switching away. Switching restores the saved version, or the builtin preset if none exists. The active normal layout is also captured when preferences are persisted outside the UI frame, including shutdown. A reset removes only the selected workspace's saved record; rebuilding it is deferred to the next normal docking frame. DockBuilder is used only for missing/default/reset arrangements, rather than every frame. Switching does not create/destroy panel windows manually.

For existing installations, a valid old `imgui.ini` is imported once into Level Editing. The original file is left unchanged. Automatic global ini loading/writing is disabled afterward, so it cannot overwrite workspace layouts. The migration marker prevents an intentional reset from importing an old layout again. Bad workspace records are ignored independently of healthy layouts and other preferences; malformed native data falls back to a preset. Inspection limits are 1 MiB per native record, 256 dock nodes and 32 parent levels. Validation accepts legitimate ImGui references from hidden windows to merged split ancestors. Native floating-window geometry is clamped when restoring to keep panels reachable on a smaller viewport. Panels missing from an older snapshot receive an ordinary docking slot.

F11 remains a temporary maximized viewport. The last normal layout is captured before entering it; saving a workspace while maximized is disabled. A workspace request made while maximized is deferred until the normal viewport returns. No workspace action changes game cursor capture, Tab, Escape, Player Controller or Lua state.

Verification passed:
- `WorkspaceRegression`: 105 checks, including native menus, custom geometry/visibility, four presets, paused Runtime/selection/Lua/event preservation, smaller window and zero DX12 validation errors.
- `WorkspaceRestartRegression`: 26 checks in a separate process, including active-workspace restoration, floating Inspector and independent reset without clearing unrelated preferences.
- `WorkspaceCorruptionRegression`: 11 checks for real startup with a damaged active native record.
- `WorkspaceMigrationRegression`: 14 checks for old ini import, a missing/new panel and unchanged legacy file.
- `InteractiveRoomRegression`: 198 checks, including workspace switching while the real FPS cursor is captured, F11/Tab/Stop, movement, interaction and unchanged authored World/prefab links.
- `SceneValidationRegression`: 49 checks.
- `EditorQualityOfLifeRegression`: 162 checks, including preference roundtrips/bookmarks and restart.

Workspace tests run their executable from an isolated `workspace-regression` directory, keeping their preferences separate from the developer's real settings. Their working directory remains the build directory for existing DX12 shader lookup. The saved native layout screenshot was visually checked. No benchmarks or broad stress suites were run.

P2.5 files added: `engine/editor/EditorWorkspaces.h`, `engine/editor/EditorWorkspaces.cpp`, `tests/WorkspaceRegression.cpp`, `docs/EditorWorkspaces.md`.

P2.5 files extended: `engine/editor/EditorPreferences.h/.cpp`, `engine/editor/EditorLayer.h/.cpp`, `engine/editor/EditorUi.cpp`, `tests/InteractiveRoomRegression.cpp`, root `CMakeLists.txt` and `engine/CMakeLists.txt`. Prior Scene Validation and Lua Runtime Inspector changes remain intact in the working tree. No commit or branch switch was performed.
