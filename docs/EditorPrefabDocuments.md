# Editor.1.1 — Prefab document navigation

## Reproduced defect

The existing Asset Browser deferred a prefab double-click until mouse release, then entered `OpenPrefabEditor`/`StartPrefabEditor`. That path created a temporary Prefab World but never registered a document. The Scene tab consequently remained active. `ActivateDocument` unconditionally rejected every request while `IsPrefabEditing()`, so all Save/Stop and Discard/Stop buttons retried an action that could never succeed. `Application::OpenProject` and `CloseProject` also rejected Prefab Mode before reaching project dirty guards.

Before the fix, `PrefabDocumentRegression --reproduce` ran the actual DX12 editor and double-clicked an owned prefab fixture through Asset Browser. It verified the incorrect active Scene tab, the exact “Return from Prefab Editor” error and the retained Prefab Mode. Evidence: `out/editor11-reproduction.log`. Fixtures are isolated in `out/build/x64-release/prefab-document-regression`; user scenes, prefab sources and project descriptors are never modified for testing.

## Document lifecycle

`DocumentType::Prefab` extends the existing registry without changing the numeric values of existing document types. Identity uses the same project/type/canonical resource path tuple and unique ImGui ID. Double-click, Open Document and Open Source all use that registry. The prefab's real filename appears in its active tab; the resident Scene tab remains open. Reopening the same prefab activates its existing tab.

Only the existing `StartPrefabEditor` creates the editable context. The scene remains in the existing authored World; the temporary Prefab World becomes active through `BeginPrefabContext`. No World is copied per frame. Scene history, selection, settings and camera are retained by the existing context lifecycle. A clean prefab can leave immediately. Returning to a prefab navigation tab loads its saved source into that same existing editing mode. Because only one Prefab Mode is active, leaving a dirty prefab requires resolving its edits first.

The registry remembers the document from which each prefab was entered. Closing the active prefab returns there when it is still open, with a neighbouring-document fallback. Back to Scene in the toolbar and Prefab menu activates the resident Scene document directly; no Asset Browser search is needed. Prefab tabs and an active clean prefab are restored from project preferences after restart. Project switches clear registry IDs, prefab state and pending asset actions.

## Transitions and dirty state

`NeedsDocumentDecision` determines whether navigation needs to resolve a dirty context or Stop Play. `RequestDocumentNavigation` sends clean navigation straight through and queues only a concrete document action. Destination data is checked before leaving the source context. An unavailable or invalid destination produces an error with Close, without unrelated Save/Discard actions.

The transition modal identifies its destination and lists the affected dirty Prefab/Scene/Material source. Outside Play its buttons are Save, Discard and Cancel. A clean Play-to-Prefab transition offers Stop and Cancel. Save and Stop / Discard and Stop are used only when both dirty edits and a running Runtime World need resolution. An untitled scene without a defined save destination has a disabled Save action with an explanation.

* Save resolves the named source and performs the pending transition. A failed save keeps the source and modal intact and shows its concrete error.
* Discard closes the prefab context without writing its source, then performs the transition.
* Cancel clears the pending action and retains the active document, World, selection and history.

Prefab dirty state continues to use its existing revision history. `AuthoringSceneDirty` reads the suspended scene history during Prefab Mode and the existing scene/runtime history otherwise. The Scene tab therefore cannot acquire a dirty indicator from a prefab-only edit. Saving a prefab may update linked scene instances, as before; that update is a separate scene undo/file transaction and may make the Scene dirty legitimately. Prefab Undo/Redo operates on the prefab history; returning restores scene history.

## Save safety and failure recovery

Save Prefab in the toolbar/menu and normal Save while Prefab Mode is active call the existing prefab save operation with `stayOpen=true`. The tab remains active and the prefab revision is marked saved. The default `SavePrefabEditor` API retains its existing Save-and-Return behavior for current callers and regressions. Explicit Scene Save As still rejects a Prefab World; Scene JSON is never a destination for prefab serialization.

Prefab saving still uses the existing `WritePrefabAndUpdate` transaction, source-baseline conflict check, reference/topology validation and atomic source replacement. The new `SuspendPrefabContext` temporarily moves ownership of the existing Prefab World while that transaction operates on the resident scene. It does not clone or reconstruct the World. Its scene-context restoration is shared with `EndPrefabContext`.

On save failure, the same World, root/selection handles, camera, revision and undo/redo stacks resume. On successful Save-and-stay, they also resume, with an updated saved baseline. On successful Save-and-Return, the temporary World is released and the existing scene context remains active. This avoids the previous failed-save path that reconstructed a new prefab World and lost its history.

Play remains unavailable in Prefab Mode, with an explanation on the disabled toolbar action. Entering a prefab from Play requires explicit Stop; Cancel retains the Runtime World. Project Open/Close now reaches the existing Save/Discard/Cancel guards. Saving there first saves/exits the prefab, then resolves scene/UI/material/project edits in their existing domains. A failed prefab save aborts the project switch.

## Verification

`PrefabDocumentRegression` uses actual Application/ImGui/DX12 frames, pointer double-clicks in Asset Browser, real Document Tab clicks/close buttons and native modal/toolbar button activation. It covers Scene ↔ Prefab, A ↔ B, Material ↔ Prefab, Prefab → UI → Scene, clean return, Back to Scene, Save-and-stay, dirty navigation and closing with Save/Discard/Cancel, reentry, selection/camera restoration, separate dirty/undo contexts, Play Stop/Cancel, project Save/Discard/Cancel and stale-ID rejection.

Two save failures are injected only into owned fixtures: an external disk baseline change and a source file held without replacement sharing. The latter forces the actual atomic replacement to fail after context suspension. Tests assert the identical Prefab World identity and undo count, unchanged Scene JSON, a visible concrete error, and successful Save retry after the lock is released. `PrefabDocumentRestartRegression` runs separately to restore the active prefab and return to the scene through its native tab. The native target checks for zero DX12 validation errors.

Release build of WhispEngine and the selected regression executables succeeded. **14/14 related tests passed**: PrefabRegression, PrefabDx12Regression, PrefabPlacementRegression, PrefabDocumentRegression, PrefabDocumentRestartRegression, EditorDocumentRegression, EditorDocumentRestartRegression, WidgetDesignerRegression, WorkspaceRegression, WorkspaceRestartRegression, WorkspaceCorruptionRegression, WorkspaceMigrationRegression, ProjectSystemRegression and InteractiveRoomRegression.

After the final disabled-Save guard for an untitled Scene, both native Prefab document tests passed again: **285 workflow checks and 12 separate-process restart checks**. This includes clicking the disabled Save action and verifying that the modal, Prefab World and source context remain unchanged. The native workflow reported **zero DX12 validation errors**. `git diff --check` passed. The full engine suite and unrelated benchmarks were not run.

Local evidence: `out/editor11-reproduction.log`, `out/editor11-build.log`, `out/editor11-tests.log`, `out/editor11-full-detail.log`, `out/editor11-prefab-final-tests.log`, `out/editor11-prefab-final-detail.log`. The native workspace image is `out/build/x64-release/prefab-document-regression/workspace.ppm` (also converted to PNG for visual inspection). These are ignored validation artifacts, not user resources.

## Changed files

New: `tests/PrefabDocumentRegression.cpp`, this document.

Updated: root `CMakeLists.txt`; `engine/core/Application.h/.cpp`, `ApplicationProject.cpp`; `engine/editor/EditorDocuments.h/.cpp`, `EditorLayer.h`, `EditorPrefabs.cpp`, `EditorProject.cpp`, `EditorAssets.cpp`, `EditorUi.cpp`, `EditorWorkspaces.cpp`; the Editor.1 workspace documentation.

No new prefab system, editor layer, ECS architecture, renderer or GUI framework was introduced. Work remains uncommitted on `engine_workflow`.
