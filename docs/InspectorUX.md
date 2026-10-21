# Inspector UX — Editor.2B

The existing ECS Inspector uses a shared presentation layer over Component Registry descriptors. ECS, property setters, serialization, world/local transform mathematics, physics, LuaRuntime and document lifecycle remain authoritative and unchanged.

## Audit

`EditorInspector.cpp` already contained a descriptor-based Drawer, separate Bulk adapters, Lua schema adapters and editor transaction handling. Vectors already used precision-aware axis controls. Single-selection tables forced two columns at narrow widths, Mesh/Physics sections started collapsed, resources used raw key text boxes, and Transform secondary controls filled the primary view. `AssetState` called `ResourceManager::Get` during presentation. The Widget Inspector shares the existing UI document editor and its own transactions; it is retained rather than routed through ECS setters.

## Layout and controls

`InspectorControls.h` contains presentation-only case-insensitive matching, adaptive property tables, property rows and section headings. It owns no World, component metadata or transactions. The existing Drawer remains the adapter for float/int, bool, enum, vector, color, text, nullable overrides and material slots. Lua scalar/vector fields reuse it; entity references retain the specialized reference picker.

```text
Standard: Property label | Value / picker | Inline resource actions
Narrow:   Property label
          Value / picker + actions
```

Below 340 logical pixels of available content width, rows use one column with labels above values. Wider content uses a label column capped at 145 logical pixels and a value column. Single values are capped at 360 logical pixels. Widths and spacing scale with DPI; labels wrap and have full-label tooltips. Vector controls retain their existing precision-dependent minimum widths, placing axes on separate lines when necessary. X/Y/Z have restrained red/green/blue text accents. Controls use the existing Browser UI proportional font, including its Cyrillic glyphs, when available; no additional font atlas is created.

## Components and search

Transform exposes Position/Rotation/Scale immediately. Pivot is in Advanced; precision, snap and quarter-turn commands are in Transform Options. Mesh Renderer groups Resources, Rendering and Advanced. The existing Material component retains its resource/slot/override sections. Lights group General, Shape / Range and Shadows. Rigidbody and Collider group body and shape/collision fields, keeping advanced parameters collapsed. Lua groups Resources and Permissions, then displays existing runtime/schema status and exposed fields. No fields or capabilities are invented for unsupported component settings.

Component headers retain expand/collapse and the existing Reset/Remove context operations. General component Copy/Paste is not introduced. Property search matches known component names/keys and property names/keys, case-insensitively for the current English descriptor labels; Lua exposed names/labels are included. A matching component shows its properties. Search opens matching sections and Clear restores unfiltered content. Search never sets component values. Starting search finishes an existing live edit through the normal transaction owner, preserving that edit. Bulk filtering retains the same shared-component policy and Mixed inputs; it never adds missing components.

## Resource references

The common picker shows the resource filename, type/full-key tooltip, indexed choices, None, clear and Find in Asset Browser. Find uses the existing folder/selection/navigation state. Missing indexed references show a warning while preserving the key. Mesh, Material, Texture, Shader, Lua, UI and Prefab references use the same control where represented by existing descriptors. Material slot order and serialization are unchanged; slot removal remains explicit.

Drag/drop uses the existing `WHISP_ASSET` payload and `AcceptAssetDrop`, including kind, bounded string and key validation. Value changes still pass through the existing setter and transaction path. Status reads use `PeekIndexed`; rendering the picker does not load resources, validate filesystem paths, recreate GPU objects or introduce synchronization. Catalog changes and existing ResourceManager versions/status supply refresh information. Explicit selection/drop/save/open operations retain their normal validation.

Untyped Lua asset schemas retain a text-key adapter because they provide no authoritative resource kind; the Inspector does not infer a type or substitute Scene choices for them.

Point/Spot Cast shadows is disabled with an explanation that only the primary Directional light currently has a shadow pass. The stored reserved flag is preserved. No local shadow architecture was added.

## Prefab and Undo

Prefab source data is read into an Inspector-local JSON cache when the project/key/catalog revision changes, or after Refresh Source. The cache retains no World handles. Missing/invalid sources show the concrete read error. Comparable direct descriptor properties display a small `*` for a value differing from the source; the tooltip identifies inherited/overridden state. Root Transform is described as instance placement, which the existing Revert preserves. Nested material overrides retain their authoritative nullable override controls; unsupported comparisons are not labeled as inherited.

Open Prefab and Apply/Revert retain the existing document workflow and confirmation semantics. The UI does not write source files while inspecting or comparing. Source cache reads happen on invalidation, not each steady frame.

One drag or confirmed numeric input uses the existing pending transaction and one Undo record. Escape uses the existing cancellation path and precision control snapshot, restoring the value without adding an Undo. Rotation still displays degrees and stores radians. Bulk axis masks, relative/world editing and parent/child validation are unchanged. Lua exposed field changes use existing entity snapshots. Scene, Prefab and UI document histories remain separate.

## Validation

`InspectorUXRegression` uses the real Application/ImGui/DX12 editor with an isolated executable, preferences and owned project. It captures 280/380/550 pixel layouts, exercises native numeric input and pointer drag, Undo/Redo, Escape, component disclosure, Transform Advanced, search, resource picker/drop type checking and Lua bool editing. Resource drop tests draw the actual shared picker through a small test-only ImGui context hook; existing Asset Browser native regressions cover the payload source and browser drag workflow.

Generated captures, fixture files, measurements and logs live under ignored `out/`. They do not edit gaming scenes/prefabs or the user's saved editor layout. The full engine suite is outside this targeted UI change.

## Results and measurements

The final native Inspector run passed **380 checks** (including frame/run assertions), with **zero DX12 validation errors**. It additionally verifies Tab navigation between axes, suppression of F11 during numeric text input, the actual rendered Prefab override marker and its removal after Revert, Scene Save/Load, Prefab Save without changing Scene bytes, UI Document return, Play/Stop, F11 restore and 150% DPI.

The compatibility run passed **24/24 tests**: ProjectSystem CPU/DX12, Physics, BulkInspector CPU/DX12, EditorQualityOfLife, EditorDocument and restart, PrefabDocument and restart, LuaGameplayApi CPU/DX12, MaterialSystem CPU/DX12, LightSystem CPU/DX12, EditorWorkflow CPU/DX12, EditorUsability CPU/DX12, AssetBrowserUX and restart, existing AssetBrowser and InspectorUX. Log: `out/editor2b-final-tests.log`. After the final small changes to untyped resource references, bulk component-key search and the added native assertions, the directly affected Inspector/Bulk/Document/Prefab/BrowserUX tests were rerun: **9/9 passed** (`out/editor2b-final-adapters-tests.log`). Earlier failed diagnostic runs are not counted as passing results.

The standalone PhysicsRegression target initially failed to link because it omitted the existing `engine/ecs/Hierarchy.cpp`. Adding that source to its target restores the requested regression without modifying physics or ECS implementation.

DPI testing reproduced an existing DX12 ImGui allocation failure: only descriptor zero was available when dynamic font atlas growth needed another live texture. The backend now reserves eight additional ImGui descriptors after the existing material-table range and frees the actual allocated index. Existing game texture indices/capacity and material tables stay intact. This is a bounded allocation fix for the existing atlas; there is no new atlas, renderer pipeline or per-frame resource creation. The 150% DPI test now completes with zero validation errors.

CPU measurements use the existing Inspector timer, a floating 550×580 panel, DX12 validation, 15 warm-up frames and 32 measured frames per scenario. Scenes and resources belong to the regression fixture. Mesh includes its existing separate Material component; Lua includes Transform and one script. The three-light multi-selection uses the existing intersection of supported common fields.

| Selection | Mean Inspector UI CPU, ms | Steady Inspector path probes |
|---|---:|---:|
| None | 0.00501 | 0 |
| Transform | 0.03326 | 0 |
| Mesh Renderer + Material | 0.05314 | 0 |
| Spot Light | 0.05756 | 0 |
| Lua Script | 0.05504 | 0 |
| Multi-selection | 0.04319 | 0 |

These short local series measure UI construction rather than GPU frame time and impose no FPS threshold. Raw results: `out/build/x64-release/inspector-ux-regression/performance.csv`. `Statistics::inspectorPathProbes` counts project link/path safety probes only while DrawInspector executes. An initial whole-frame counter also counted unrelated renderer work even with no selection; it was replaced with the scoped metric. The counter does not claim to count every OS operation in the whole editor.

Native frames were visually inspected for narrow, standard and DPI layouts:

- [280 px](../out/build/x64-release/inspector-ux-regression/width-280.png)
- [380 px](../out/build/x64-release/inspector-ux-regression/width-380.png)
- [550 px](../out/build/x64-release/inspector-ux-regression/width-550.png)
- [150% DPI](../out/build/x64-release/inspector-ux-regression/dpi-150.png)

## Files

`engine/editor/InspectorControls.h`, `engine/editor/EditorInspector.cpp`, `engine/editor/EditorLayer.h`, `engine/render/backends/dx12/Dx12RenderAdapter.cpp`, `tests/InspectorUXRegression.cpp`, `CMakeLists.txt`, and this document. Work remains on `engine_workflow`; no branch switch or commit is performed.

## Limits

The change reuses the supported bulk fields and existing specialized Material/PostFX/UI adapters. Search indexes descriptor/schema properties rather than arbitrary custom action captions. Prefab comparison covers direct comparable fields, not a new structural override engine. Explicit resource/catalog operations and first Prefab source-cache population may access disk; steady Inspector construction is measured separately from the rest of the frame. Global toolbar/window redesign at large font scales remains outside this Inspector change.
