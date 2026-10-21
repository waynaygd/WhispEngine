# Editor.1 — Document tabs and workspace shell

## Architecture audit

The existing `EditorLayer` owns the editor UI, scene transaction history, material editing, UI document sessions and style sessions. `EditorWorkspaces` already provides native ImGui docking, separate serialized layouts, panel visibility and default layout construction. The existing Widget Designer already renders a large DX12 canvas alongside Widget Hierarchy and Widget Inspector. Its property adapter exposes UI.2 layout, UI.3 styles and UI.4 animations. Those implementations are reused.

Panel windows use `ImGui::Begin`, support docking and ordinarily specify only first-use sizes or minimum sizes. These are not fixed-size OS windows. Modal scene/project actions and the startup project browser have their own constrained geometry. The old Material Editor starts at 390 × 480 when floating; its document workspace now docks the same panel into the central region. Lua Runtime Inspector remains an existing diagnostics panel; Script Document adds a bounded read-only source preview and the existing external-editor command.

There is one authored Editor World and a separate Play clone. Scene loading stages and validates disk data before replacing that World. Selection contains World-specific handles, so document navigation records stable `SceneIdentityComponent` values instead. UI document sessions already use canonical source paths and own draft, saved baseline, selection and undo/redo history. Shared styles have separate sessions and dirty tracking. Project transitions already drain resource workers, stop Lua/Play, clear editor sessions and reset resources. Existing shutdown/project Save/Discard guards remain authoritative.

## Registry and identity

`EditorDocuments.h/.cpp` extends the existing EditorLayer; there is no additional editor layer or renderer. `EditorDocument` contains document type, canonical project identity, resource key and canonical resolved file path. Identity uses all three of type, project and canonical resource path. Windows paths are case-folded. Resource keys retain their full relative directory. `ui/one/HUD.ui.json` and `ui/two/HUD.ui.json` therefore open separate documents; reopening the same canonical resource activates its existing entry.

Every registry entry gets a session-unique ImGui ID independent of its filename. Tabs show filename, active highlight, close control and ImGui's unsaved indicator. Hover shows relative key and resolved path. Ctrl+Tab / Ctrl+Shift+Tab navigate when text input, modal UI and captured viewport input do not own the keys. Closing an active entry chooses the next entry or the previous entry at the end of the list. Closing the last entry displays Start Workspace.

## Shell and workspaces

Main Menu, Main Toolbar and Document Tabs form the shell. The docking area starts below both toolbar rows and responds to the main viewport's available size and DPI. All documents remain in the existing main window.

* Scene uses Level Editing: Viewport, Hierarchy and Inspector, with Asset Browser and Console below. Scene Validation remains available from View.
* UI Workspace uses Canvas Designer in the center, Widget Hierarchy and Add Widget palette on the left, Widget Inspector on the right, and Asset Browser below. Existing Inspector sections expose styles and animations. No duplicate layout/style/animation implementation is introduced.
* Material Workspace places the existing Material Editor properties in the center with ordinary utility docks. There is no material graph or new preview renderer.
* Script Workspace places Script Document in the center, Lua Runtime Inspector on the right, and Console/Asset Browser below. Source is read-only, supports horizontal scrolling and explicit Reload file, and is limited to 2 MiB. Open in Visual Studio uses the existing script path resolver and external editor integration. Lua hot reload is unchanged.

Level Editing, Scripting, Asset Management and Gameplay Debugging presets remain available. Level Workspace is an alias for Level Editing. UI, Material and Script have separate layout keys. Switching captures the outgoing native layout, then applies the requested saved layout or default. DockBuilder runs only for missing/reset layouts. Save Current Layout and Reset Current Layout retain their existing meanings. Native ini data lives inside the existing atomic `editor-preferences.json`; the global `imgui.ini` writer remains disabled to avoid competing persistence.

Selecting a different document family's workspace from the menu activates an open document of that family, or asks to open its resource. Selecting a scene preset returns to the resident scene. A menu preset therefore does not leave a Scene tab highlighted over an unrelated asset workspace.

## Opening resources

File → Open Document accepts a portable project-relative key. Supported documents are actual scene-directory JSON files, `.ui.json`, project material resources and `.lua`. Unrelated JSON outside supported resource domains is rejected. Scene loading still performs the serializer's validation.

Asset Browser double-click opens UI, Material, Lua and Scene Document tabs. Existing Prefab and imported-model actions remain separate. Scene entries come from the existing SceneCatalog, including the project's configured scene directory outside Assets. UI designer selectors and creation actions also register their document, reusing the same canonical draft session.

## State and persistence

UI drafts and undo history remain in the existing UI sessions across tab changes. Selection, zoom, pan and preview resolution are maintained per UI document. Scene-to-asset switching leaves the authored World, scene selection and history in place. Scene-to-scene navigation records camera pose, ordered stable selection, gizmo tool, grid and game-view settings, then uses the existing save/load path. No World is cloned for tab navigation.

Preferences contain a bounded `documentSessions` object indexed by canonical project identity. Each project session stores ordered document types/keys, active resource, scene camera/selection settings and UI view/selection settings. Draft contents, resource handles, ECS handles and Lua references are never serialized here. On reopening a project, available resources are registered; only the selected scene needs to replace the World. Missing files and malformed entries are skipped. Session restoration is deferred until the next normal editor frame so startup project selection and ImGui initialization have completed. A project transition persists the outgoing session before clearing resource state.

## Dirty state and transitions

Tab dirty indicators read the existing scene revision, UI draft dirty state, shared-style dirty set or material dirty state. The registry has no independent dirty bit. Lua source is read-only.

Closing a changed document or replacing a dirty loaded scene/material offers Save / Stop, Discard / Stop and Cancel. Failed saves/reloads keep the document open and report the error. UI closing explicitly includes shared style drafts, which have a separate save domain; the transition dialog says so. Discard reloads disk data through the existing loader. Discarding a resident scene reloads it, so closing its tab cannot leave a hidden dirty authored World. A cancelled transition preserves drafts and runtime state. Normal Save and Edit-menu Undo/Redo route to the active document; UI-specific keyboard editing retains its existing routing. Prefab Save retains its existing save domain.

Materials retain the existing single editable material session: opening a different material requires resolving the current material's edits. Other material tabs remain navigation entries. UI documents have independent draft sessions. UI document save still protects against overwriting externally changed disk baselines.

## Focus Mode

Maximize Workspace / Restore Layout in Main Toolbar temporarily expands the active primary panel and hides secondary panels. Its normal native layout is captured first; the dock tree stays alive. Restore reapplies that layout and visibility. Drafts, selections and panel instances are preserved. Focus Mode is transient and separate from F11. F11 retains its existing maximized viewport and Clean Play behavior.

## Scene, project and Play limitations

Scene tabs are navigation over one authored World, not concurrent editable Worlds. Loading another scene requires Save/Discard when dirty and clears scene undo history through the existing loader. Returning restores stable authored selection and camera where possible. Inspector's transient controls are not serialized across a scene reload. Unsaved untitled scenes must be saved before durable restart restoration.

Play starts from a Scene document through the toolbar. Switching to an asset document does not replace the Runtime World. UI remains preview-only during Play using the existing authored-data guards. Switching to a different scene or closing the resident scene during Play requires explicit Stop confirmation; Cancel leaves the Runtime World running/paused. The existing scene browser also identifies its Stop consequence, and same-key scene reload activates the corresponding Scene tab. Stop focuses the Scene Viewport when a Scene document is active, bringing its dock tab forward; Clean Play retains its existing toolbar behavior. Existing project and shutdown guards cover all dirty sources. Project switching clears registry IDs, pending document transitions, UI sessions, source preview and resource references before activating the new project's resources.

## Verification

`EditorDocumentRegression` uses actual Application, ImGui frames, synthetic native input and the DX12 debug layer. Fixtures live in the isolated `document-regression` build directory and in newly created projects, without changing authored game scenes. `EditorDocumentRestartRegression` uses the saved fixture in a separate process.

The targeted regression covers resource types, actual UI asset double-click, canonical reopen/different folders, UI layout/animation edits, draft/selection/view preservation, Undo/Redo, dirty Save/Discard/Cancel, closing tabs, Focus Mode/native docking restoration, scene guards/camera/selection, paused Play navigation, explicit Stop, F11, utility visibility, project switching, stale document rejection, DX12 UI geometry/textures and validation errors. Shared style authoring and interactive designer gestures are additionally covered by existing UI regressions.

Release build of WhispEngine and the selected regression targets succeeded. The related CTest run passed **15/15 tests**: EditorDocumentRegression (149 checks), EditorDocumentRestartRegression (12), WidgetDesignerRegression (644), RuntimeUIRegression (800), InteractiveUIRegression (175), UI4Regression (194), ProjectSystemRegression, SceneValidationRegression, InteractiveRoomRegression (198), WorkspaceRegression (105), WorkspaceRestartRegression, WorkspaceCorruptionRegression, WorkspaceMigrationRegression, LuaInspectionRegression and LuaGameplayApiRegression. The native document and Interactive Room checks reported zero DX12 validation errors. The generated UI workspace capture was visually inspected. `git diff --check` passed.

Build/test evidence is in the ignored `out/editor1-build.log`, `out/editor1-final-tests.log` and `out/editor1-full-detail.log`; the native image is `out/build/x64-release/document-regression/workspace.ppm`. These are local validation outputs, not project resources. The full engine suite and performance/stress benchmarks were not run.

After the final workspace-menu integration, WhispEngine and the selected targets were rebuilt and all six Document/Workspace tests passed again (`out/editor1-workspace-final-tests.log`).

The existing Workspace restart regression establishes its persisted baseline after the new outgoing-layout capture before checking independent Reset. Interactive Room's ready-project check follows the descriptor's actual startup scene (`Scenes/interactive_room_ui.json`) while retaining the 72-entity assertion. Its capture fixture explicitly restores native window focus and its failure messages include the measured geometry. Project descriptors and game scenes are unchanged.

## Changed files

New: `engine/editor/EditorDocuments.h`, `engine/editor/EditorDocuments.cpp`, `tests/EditorDocumentRegression.cpp`, this document.

Updated: root and engine `CMakeLists.txt`; `engine/core/ApplicationProject.cpp`; `engine/editor/AssetCatalog.cpp`, `EditorAssets.cpp`, `EditorImportUi.cpp`, `EditorLayer.h/.cpp`, `EditorPreferences.h/.cpp`, `EditorProject.cpp`, `EditorTransactions.cpp`, `EditorUi.cpp`, `EditorWidgetDesigner.cpp`, `EditorWorkspaces.h/.cpp`; `tests/WorkspaceRegression.cpp`, `tests/InteractiveRoomRegression.cpp`; `docs/EditorWorkspaces.md`.

No changes to RenderSystem, ECS architecture, ResourceManager, RuntimeUI renderer, existing game scenes or lab work. Work remains uncommitted on `engine_workflow`, as authorized.
