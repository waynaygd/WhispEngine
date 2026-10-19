# Prefab Placement Preview (P1.3)

Drag a Prefab card from Asset Browser into Viewport. Every visible Mesh in its subtree is drawn in the existing cyan placement style before any game-world entities exist. Surface Snap, Grid Snap and Align to Normal use the same placement controls and triangle queries as ordinary Mesh dragging. Root source rotation/scale, child local TRS and offset pivots contribute to the combined bounds. Escape, releasing outside the target, switching context or completing the operation clears the transient preview.

A successful drop instantiates the cached, validated prefab graph through the existing prefab system, then assigns the exact preview root Transform before recording one history entry. Source linkage, fresh IDs, internal references, selection and subtree Undo/Redo remain intact. In isolation, new members are included in the visible set. Prefab double-click opening waits for button release without dragging, so a quick retry after Escape does not switch edit contexts. Selection filters do not hide the preview or prevent selecting the placed root. Prefab Editor rejects prefab placement to preserve the existing restriction on nested prefabs; ordinary Mesh placement remains available there.

## Reused mechanisms

- `EditorPlacement.cpp` triangle Surface Snap, Grid Snap, ground/camera-distance fallback and quaternion-based normal alignment.
- `ReadPrefab`, registry/scene validation and World hierarchy TRS composition during one-time preparation.
- ResourceManager asynchronous Mesh loading on the existing Job System, resource versions and the renderer's normal GPU cache.
- RenderSystem's existing cyan placement snapshots, extended from one item to a vector of `PlacementRenderItem` values. There is no separate renderer, physics world or Lua runtime for previews.
- P1.2 instance creation and existing transaction/selection/history handling.

## Loading, caching and lifetime

One drag caches source JSON, root defaults, resolved relative poses and local Mesh bounds. Cursor movement updates placement and composed render poses; it does not reread source JSON or recreate the temporary hierarchy. Source metadata (canonical path, write time and size) is checked to invalidate the cache. Resource owner/version changes refresh affected Mesh bounds and the aggregate bounds. A new drag or cancellation discards prepared data. As with other metadata-based asset detection, external edits that preserve both file size and write time are detected on the next operation rather than within the current drag.

Mesh loads are asynchronous. Available parts are rendered while others are loading or have failed; status/tooltip gives counts and the first resource diagnostic. Drop stays disabled until all visible Mesh parts have usable geometry, avoiding an incomplete instance or inaccurate final placement. A malformed source never creates an instance. Source/resource metadata is checked again at confirmation to reject a stale preview.

Temporary data owns no GPU allocation and no live game-world entity. Shared Mesh/shader/texture allocations belong to the ordinary ResourceManager/RenderSystem caches and remain reusable after cancellation. No per-drag unload destroys resources shared by the scene. Nonvisual prefabs preserve root-only placement with a status message and no ghost geometry.

## Boundaries

Placement uses a root-space aggregate AABB of the visible Mesh bounds. This prevents penetration of the supporting plane but can leave a conservative gap on slopes or concave objects; it does not solve geometry interlocking or collision clearance. Invisible Mesh parts do not affect preview/support. Existing P1.1 shear, hierarchy and transform limits remain enforced. Source JSON preparation is synchronous once per new/changed source, within the existing 1 MiB prefab limit; Mesh loads do not block UI. No nested prefabs or variants are added.

## Verification and files

`PrefabPlacementRegression` uses `prefab_cabinet_demo.json` and Cabinet, including actual browser dragging, cyan rendered pixels and all five Mesh submissions. It checks flat/sloped surfaces, alignment, grid, shifted pivots, source root TRS, partial/failing loads, mesh/source invalidation, Escape, exact preview/instance transforms, references, isolation/filter independence, one-step Undo/Redo and scene disk roundtrip. Existing Placement/AssetBrowser tests verify ordinary Mesh placement; Prefab/Hierarchy/MultiSelection regressions cover the shared workflows.

P1.3 changes: `engine/editor/EditorPlacement.cpp`, `EditorLayer.h`, `EditorLayer.cpp`, `EditorPrefabs.cpp`, `EditorAssets.cpp`; `engine/ecs/PlacementPreview.h`, `systems/RenderSystem.h`, `systems/RenderSystem.cpp`; the placement bridge in `engine/core/Application.cpp`; `tests/PrefabPlacementRegression.cpp`, root `CMakeLists.txt`, and the prefab workflow documentation. Existing P1.2 work is retained.
