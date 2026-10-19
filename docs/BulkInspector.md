# P1.4 — Bulk Inspector

## Existing mechanisms reused

SelectionSet, ComponentRegistry property descriptions and validation, the existing Inspector window,
EditorTransactions, and World.SetLocalTransforms/SetWorldTransforms. The single-entity Inspector,
group gizmo, ResourceManager and prefab formats retain their existing paths.

## Using the Inspector

Select multiple entities in Hierarchy or Viewport. The Inspector shows `N Entities Selected` and
only the intersection of their component types. Missing components are never added automatically.
Each visible field applies to the entire selection; one entity restores the ordinary Inspector.

Different values show `Mixed`, mixed checkboxes, or a Mixed enum preview. Numbers use an absolute
1e-6 plus relative 1e-5 tolerance. Vector axes are compared separately. Mixed numeric inputs stay
blank until typed; Enter or leaving an edited field applies the value, including when it equals
the first entity's old value. Merely showing a field never changes data. A mixed color opens an
explicit picker; choosing a color replaces that color on all targets. Light colors keep existing
linear/sRGB conversion. An optional material value has a mixed Override checkbox and inheritance.

Supported fields come from the existing registry:

- Transform: Position, Rotation (displayed in degrees), Scale, per axis.
- Directional/Point/Spot Light: Enabled, Color, Intensity, Cast shadows; Range/Near radius where
  present; inner/outer cone angles for Spot. Only identical concrete light components are shared.
- MeshRenderer: Mesh, Texture, Shader asset references and Visible.
- Material: asset, shader/base texture, tint, existing numeric/rendering/texture overrides.
- Other registered scalar, enum, string, vector, color and asset fields use the same batch path,
  with existing validation and read-only restrictions. LuaScript metadata edits preserve each
  entity's custom properties and entity references.

## Transform semantics

`World coordinates` selects Local or World; `Relative` selects offsets instead of absolute values.
Absolute changes only the edited axes on every selected target. Local values refer to each target's
own parent. World targets are solved together, including selected parents and children.

Relative Position/Rotation add offsets to the gesture's initial values (Euler angles for rotation);
Relative Scale multiplies by factors, starting at 1. Selected descendants follow their selected
ancestor once: only the selected hierarchy roots receive the relative operation. Rotation/Scale
are changes to each root's own TRS, without a shared pivot. The existing gizmo is unchanged.

Every target is staged and checked before mutation. World batch APIs preserve shear restrictions.
A component validation failure rolls the entire update back. Coupled setters that would silently
change another field are rejected with a diagnostic; use a compatible value or single selection.
Sky-driven sun fields remain disabled.

## History and prefab behavior

One completed gesture creates one history record; continuous drags share one baseline. Escape
cancels an active bulk gesture. Undo restores individual values; Redo repeats the group edit.
Records contain only touched components (or Transform data), never whole-entity snapshots.
Missing history targets/components block the whole restore before any surviving target changes.

Bulk changes remain local instance edits. Prefab links, member maps and individual Lua properties
are retained. No source file is written automatically. Select one instance for existing explicit
Apply/Revert. Inside the Prefab Editor, Save remains explicit. Existing structural override
restrictions and source-conflict checks remain applicable.

## Limits

- Bulk edits are disabled during Play.
- Material slot lists and custom Lua behavior fields require one selection.
- Persistent entity identities, prefab source links and choosing the primary sun are not batch-editable.
- No automatic component addition, advanced prefab overrides, new reflection or material system.

## Files for P1.4

- engine/editor/EditorBulkInspector.cpp: aggregate values, atomic batch mutation and partial restore.
- engine/editor/EditorInspector.cpp: multi-selection branch in the existing Inspector.
- engine/editor/EditorLayer.h: batch API/state and component-only history metadata.
- engine/editor/EditorTransactions.cpp: component-only inverse capture and bulk restore dispatch.
- engine/CMakeLists.txt and CMakeLists.txt: source and focused regression target.
- tests/BulkInspectorRegression.cpp: CPU and native DX12 coverage.

## Verification

Built WhispEngine and all related regression targets in shadows-dx12. BulkInspectorRegression
checks Mixed, 2/5/10 to 7 and individual Undo/Redo, colors, visibility/material inheritance, a single
record across live updates, cancellation, untouched components, per-axis transforms, parent/child
world and relative operations, shear/validation rollback, prefab links/Lua references, explicit
Apply/Revert and scene save/open. Native DX12 coverage drives actual Mixed numeric/boolean/vector
widgets and the single-selection path, and checks zero DX12 validation errors.

Related targeted suite: BulkInspector CPU/DX12, Prefab CPU/DX12, Hierarchy CPU/DX12,
MultiSelection CPU/DX12, PrefabPlacement, Placement, AssetBrowser. No stress tests or benchmarks.
Native snapshots: out/build/shadows-dx12/diagnostics/bulk-inspector/{mixed,final}.ppm.
