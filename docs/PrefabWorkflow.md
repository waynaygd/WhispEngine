# Prefab Workflow (P1.2)

## Use

- Select one root in Hierarchy. Its context menu **Create Prefab...**, the **Prefab** menu, or Asset Browser's **Create / Prefab** opens a path dialog. Choose a fresh `prefabs/name.prefab.json` key, including subfolders inside Assets/prefabs. The original subtree becomes a linked instance.
- Filter Asset Browser by **Prefab**. Double-click a card or choose **Open Prefab Editor**. **Create Instance** and dragging a card into Viewport instantiate the full subtree. Placement previews the complete cyan Mesh subtree with aggregate bounds, Surface/Grid Snap and Align to Normal, through the existing renderer. See `PrefabPlacementPreview.md` for the loading and cache behavior.
- Prefab Editor reuses Hierarchy, Viewport, Inspector, gizmos, Lua fields and component history in one temporary authoring World. Create children underneath its root. Extra roots and an absent root prevent Save. **Save Prefab** / Ctrl+S opens a confirmation; **Save Prefab and Return** saves and updates the linked instances in the original scene. **Return to Scene** warns before discarding edits.
- On return, the original World, scene settings/key, selection, camera, isolation, game-view setting and Undo/Redo context are restored. Play and scene/project transitions are blocked while a prefab is open. Prefab scene settings are temporary and are not part of the asset.
- Select an instance or descendant. Inspector **Apply...** writes supported component/Lua/child-local changes to the source and updates all instances of that asset. **Revert...** restores just the selected instance. **Prefab / Update All Linked Instances** and the asset context action refresh all linked instances after external source edits.
- Replacing local changes requires explicit UI confirmation. Root local TRS and its external Parent are retained, so root World position, rotation and scale remain unchanged. Updates affect only the open authoring scene and never modify the Runtime World or unloaded scene files.

## Identity and compatibility

`Prefab.cpp` extends the existing Lua spawn path; `World.spawn_prefab(key, position)` is unchanged. New source schema v2 contains `root` and `entities` with stable prefab-local `id`, optional local `parent`, and registry `components`. Scene schema stays v4.

Every instance allocates fresh persistent SceneIdentity values. Its root has a non-editable `PrefabInstance` component: an asset key and a prefab-local ID to scene `$entity` reference map. Normal component serialization, scene save/load, runtime cloning, duplication and subtree deletion include this metadata. Matching members retain their scene IDs during an update; newly added source children receive new IDs. Typed internal Lua references are remapped per instance. Different instances share resource keys, not script state.

Existing single-entity v1 prefabs still spawn. Their historical scene-local entity references retain the existing resolver semantics (missing or ambiguous targets fail closed). They are not silently converted to internal references. Saving a v1 source without external references through Prefab Editor produces v2; creating/applying a compound source with external references is rejected explicitly. Clear or replace such references before saving a reusable source.

## Transactions and limits

Create, instantiate, delete, Apply, Revert and source Save/Update use the existing history. Source Save returns to the original scene and contributes one record containing the file change and affected entities. Undo/Redo restores both, including source structural additions/deletions. File writes use SceneSerializer's atomic replacement; the expected JSON content is checked before writing or undoing. An external file change blocks the transaction before scene mutation rather than overwriting another tool's work. File history, like scene history, is scoped to the current editor session/context and is cleared on scene/project changes.

A proposed graph is validated in a temporary World before applying it. This happens per explicit operation, never every frame. Invalid hierarchy/TRS/components and removal of a child referenced by another scene entity abort the batch. Failed saves retain the prefab edit context and its edited contents. Prefabs have the existing 1 MiB size limit. GPU resources continue to use the shared ResourceManager/renderer.

Supported overrides are component parameters, component add/remove, Lua fields, child Local TRS and reparenting between existing members. Local child additions/deletions in a placed instance are diagnosed: undo them before Apply/Revert/Update. Add/delete children in **Prefab Editor** instead; source structural edits update instances transactionally. If source topology changed outside the instance, update it before Apply. Existing P1.1 restrictions on dynamic Rigidbody children, singular transforms, hierarchy depth and shear still apply. Nested prefabs, variants and per-property override tracking are outside P1.2.

## Demo and checks

Open `scenes/prefab_cabinet_demo.json`: three linked Cabinet roots, each with Body, DoorLeft/HandleLeft and DoorRight/HandleRight. Source: `prefabs/cabinet.prefab.json`. DoorLeft demonstrates an entity field and configurable open angle in `scripts/cabinet_door.lua`; a `toggle` event changes its rotation independently per runtime instance.

`PrefabRegression` covers creation/overwrite protection, internal and external references, three instantiations, complete root World TRS preservation with an ordinary external Parent, component Apply/Revert, structural source changes, failure atomicity, file/scene Undo/Redo, source-editor isolation/conflicts, scene disk roundtrip, duplication/deletion, legacy v1 and Lua compound spawning. `PrefabRegression --dx12` drives real ImGui card dragging/double clicking, renders the source and instances through the existing renderer, checks context history, updates, Play isolation and DX12 validation. It restores preferences and the staged demo source after running.

## Changed files

- `engine/scripting/Prefab.h`, `Prefab.cpp`: subtree schema, validation, capture, remapping, spawn and instance update plans.
- `engine/ecs/components/PrefabInstanceComponent.h`, `engine/ecs/ComponentRegistry.cpp`: persistent root linkage.
- `engine/core/Application.h`, `Application.cpp`, `ApplicationProject.cpp`: temporary prefab context and transition guards.
- `engine/editor/EditorPrefabs.cpp`, `EditorLayer.h`: workflow, confirmations and shared panel context.
- `engine/editor/EditorTransactions.cpp`: atomic file history and validated prefab graph batches.
- `engine/editor/EditorAssets.cpp`, `EditorPlacement.cpp`, `EditorInspector.cpp`, `EditorUi.cpp`, `EditorLayer.cpp`, `EditorProject.cpp`: browser, drop target, actions and transition UI.
- `engine/assets/prefabs/cabinet.prefab.json`, `scenes/prefab_cabinet_demo.json`, `scripts/cabinet_door.lua`: demonstration content.
- `tests/PrefabRegression.cpp`, root and engine `CMakeLists.txt`: targeted checks and asset/build wiring.
