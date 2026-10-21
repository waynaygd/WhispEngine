# Material Editor UX — Editor.2C

## Audit and scope

The previous Material Editor was a long form in `EditorLayer.cpp` that edited the shared loaded MaterialResource directly. It had no independent 3D preview or material Undo history. Material Documents, workspace layouts, the dirty guard, MaterialLoader atomic saving, ResourceManager versioning, texture/shader GPU handles and Inspector.2B property controls already existed and are reused.

MaterialComponent overrides, ordered material slots, MeshRenderer and GLB/GLTF material mapping remain authoritative. Preview does not create or change ECS components. Scene/Prefab serialization, material file format, shaders, RenderSystem and Job System are unchanged. Saving publishes the draft into the same shared material resource, so the normal scene material resolution continues to apply per-instance overrides and slots.

## Workspace layout

Double-clicking a material in Asset Browser opens/activates its existing Material Document. `Material Preview` occupies the center, `Material Editor` contains properties on the right, and Asset Browser/Console share the bottom. All panels use the existing ImGui dockspace. Old Material workspace layouts without the Preview panel are rebuilt once; unrelated workspace layouts are retained. The global workspace Focus action expands the preview and restores the docked layout afterwards.

The compact toolbar contains Save Material, Revert / Reload and View. View selects Sphere/Cube/Plane, Studio/Neutral/Unlit, gray/dark background, Reset Camera and Focus Preview. The caption identifies the material key, dirty state, mesh and lighting. Properties can close independently without stopping a visible preview. Window constraints and property rows scale with DPI and support narrow widths.

## Preview scene, rendering and camera

The preview is an isolated render context, with no authored World, physics or Lua. It uses three procedural in-memory meshes, uploaded once on first use: a 48-by-64 segment sphere with UV0/tangents, a cube and a plane. No preview meshes become game assets.

`IRenderAdapter` provides an optional preview entry point; DX12 implements it using the existing texture/mesh/shader infrastructure and ordinary DrawMesh material evaluation. The pass uses the material's existing shader, base color, shading model, metallic/roughness, normal scale, AO strength, four texture maps and double-sided state. Studio and Neutral use one directional light and ambient lighting with different settings. Unlit uses the renderer's existing lighting-disabled behavior. Shader color-pipeline detection follows the existing material convention. No separate PBR shader or BRDF was added.

The backend saves and restores the scene's camera, material bindings, lighting, environment/shadow settings, dimensions and submission statistics. Preview rendering occurs before the scene pass and does not alter Editor Camera or Scene Dirty state.

LMB/RMB drag orbits, wheel zooms, MMB pans. Input is local to the preview item; it creates no cursor capture and is disabled in Play. Reset Camera restores the initial view; Focus Preview recenters and fits the selected primitive. Camera, mesh, lighting and background remain in the material session across ordinary document switches.

## Property controls

The properties reuse `InspectorControls.h`, precision scalar inputs, LinearColorEdit and the existing Inspector resource picker:

| Section | Fields |
| --- | --- |
| Surface | Name, Base Color, Base/Normal/Metallic-Roughness/AO textures |
| Parameters | Lambert/PBR, Metallic, Roughness, Normal Scale, AO Strength |
| UV | Four UV set indices |
| Shader | Existing shader resource key picker |
| Advanced | Double Sided, Cast/Receive Shadows, Surface Mode, conditional Alpha Cutoff |

Pickers show readable names, full-key/type tooltips, indexed choices, clear/None, Find in Asset Browser and existing load/error status. Drag/drop accepts the normal `WHISP_ASSET` payload with existing kind/key validation. No new resource search system or filesystem validation runs during steady presentation.

## Draft, Save, Discard and Undo

Each material session contains base/draft data and bounded Undo/Redo history (128 transactions). Existing `m_MaterialDirty` and ResourceManager dirty protection remain authoritative. Preview reads the draft; sliders do not write disk, replace the shared resource or reload assets. One continuous edit produces one transaction; text/resource changes follow the same session history. Escape restores the unfinished transaction. Ctrl+Z/Ctrl+Y and document Undo/Redo route to the active Material session, independently of Scene Undo. Ctrl+S and Save Material use the same save helper.

Successful Save atomically writes through MaterialLoader, then publishes the draft to the loaded shared resource, updates the base/version and clears dirty. A save failure retains the draft, history, dirty indicator, active document and transition modal, with the concrete loader error visible. Reload loads successfully before replacing state; load failure preserves the old session. Successful explicit Reload/Discard clears material history. A watcher reload of identical data after our own Save updates the version without erasing Undo.

Ordinary Material-to-Scene/UI/Prefab navigation retains the session. Dirty Material A-to-B, close and project transitions continue through the existing destination-aware dirty guard: Save/Discard completes the requested action; Cancel leaves editing intact. Clean transitions do not prompt. Sessions and GPU references clear on project reset; restart restores Material Documents and builds fresh preview resources.

## Resource hot reload and GPU lifetime

Shader and map presentation reads use PeekIndexed. Unindexed nonempty keys request the existing asynchronous load once through ResourceManager indexing. Loaded resources register the existing hot-reload watcher. GPU handles and versions are shared with the normal renderer; version changes retire the previous handle using existing deferred destruction. A failed GPU creation attempt is version-cached to avoid retrying every frame. Missing maps retain their keys and use neutral fallback/status; unavailable shaders show a preview message and point to Console compiler diagnostics.

The preview owns one white fallback, lazily cached primitive meshes and one persistent color/depth target. Render target size changes only on resize (64–1536 pixels per dimension). Old targets, depth buffers and descriptor heaps retire through the existing frame ring. No additional GPU wait is introduced. Cleanup only destroys shared handles still matching the handle it created, so ownership changes/hot reload cannot double-free a replacement. Project reset and shutdown use the existing editor preview release path.

Timestamp/readback resources are persistent. Results are read from a completed frame slot after the existing BeginFrame fence, without another synchronization point. Inactive Material Documents and hidden/collapsed preview windows submit no preview draws.

## Performance

Native DX12 run on this machine, 2026-10-21: 12 warm-up frames, 24 measured frames per scenario. Both alternate base textures are warm before measuring. Means below are milliseconds; these short series are diagnostic, not portable performance guarantees.

| Scenario | CPU update | CPU submission | GPU pass | New target allocations | Scoped path probes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Idle | 0.0091 | 0.3119 | 0.1023 | 0 | 0 |
| Orbit | 0.0098 | 0.3310 | 0.0991 | 0 | 0 |
| Base texture change | 0.0088 | 0.2768 | 0.0971 | 0 | 0 |
| Normal scale change | 0.0087 | 0.2904 | 0.0956 | 0 | 0 |
| Mesh switch | 0.0091 | 0.3128 | 0.0697 | 0 | 0 |
| Hidden Material tab | no update | no submission | no pass | 0 | 0 |

CPU update measures draft/frame packing and cached resource preparation, not the entire ImGui properties window. CPU submission measures the backend preview call. GPU timestamps cover the target transition/clear/draw pass, with delayed frame-slot readback. Scoped path probes cover Material Editor presentation. Target memory reports `width * height * 8` bytes (RGBA8 color + D32 depth), excluding heap alignment, transient retired targets and shared resources. Resize allocates a new target; steady visible frames do not. Hidden-tab assertions check that the draw counter remains unchanged; the CSV's zero GPU value means no pass, not a measured zero-duration draw.

## Verification

`MaterialEditorUXRegression` runs a real native Application with ImGui and DX12 in an isolated executable/project directory. The final producer passed 309 assertions; the separate restart process passed, including automatic restoration of the active Material Document. It exercises layout/focus, sphere/cube/plane, orbit/zoom/reset, resize, lighting, live draft properties/maps, real resource-drop target, numeric input, keyboard Undo/Redo/Escape, dirty indicator, Save/Discard/Cancel, deliberate atomic-save failure, navigation to Scene/Material/UI/Prefab, close/reopen, project switch, hot reload and retained Prefab override. Pixel checks verify a shaded sphere, actual unsaved object tint and changed lighting output. DX12 validation error count is zero. Test fixtures and captured files stay under ignored `out/`; user assets are untouched.

The related suite passed 17/17 in 83.09 seconds: MaterialSystem CPU/DX12, EditorDocument/restart, PrefabDocument/restart, InspectorUX, AssetBrowserUX/restart, ProjectSystem CPU/DX12, Workspace/restart/corruption/migration and MaterialEditorUX/restart. After strengthening the object-tint and automatic-restart assertions, the final MaterialEditorUX pair passed again in 17.37 seconds. WhispEngine and all affected regression targets build successfully.

Local generated evidence:

- `out/editor2c-build.log`, `out/editor2c-final-tests.log`, `out/editor2c-native-final.log`.
- `out/build/x64-release/material-editor-ux-regression/performance.csv`.
- Native PPM captures and converted PNGs in that same directory: sphere, cube, plane, sphere-orbit, draft, mapped, neutral, unlit, resized.

## Known limits

- Preview uses the existing LDR shader path with direct light and ambient. Scene IBL, shadow passes, HDR tone mapping/postprocessing and AA are not reproduced; final scene appearance can differ with those enabled. Cast/Receive Shadows remain stored controls without a preview shadow pass.
- Existing rendering supports UV0. Nonzero stored UV sets disable that map and are explained in properties. No UV-transform or emissive controls are invented.
- Masked/Transparent remain existing unsupported surface modes; their values are preserved, the object is omitted and a warning is shown.
- Custom shaders must support the existing material bindings. Load/compile diagnostics use the existing resource/Console paths; no shader reflection or graph was added.
- Camera and preview choices persist across tabs within a live session, not across process restart. The restored document gets fresh defaults and GPU handles.
- The existing single active material authoring context remains: switching away to another material while dirty requires the normal Save/Discard/Cancel decision. This does not create several independently dirty material domains.

## Changed files

- `engine/editor/EditorMaterial.cpp`: workspace presentation, session transactions, preview frame/resource preparation, save/reload/release helpers.
- `engine/editor/EditorLayer.h`, `EditorLayer.cpp`: session state and active-document shortcut/panel routing; old material form removed.
- `engine/editor/EditorDocuments.cpp`, `EditorProject.cpp`, `EditorAssetPreview.cpp`: common save/discard, navigation and cleanup integration.
- `engine/editor/EditorWorkspaces.cpp`: Material dock layout and migration.
- `engine/editor/EditorInspector.cpp`: existing Open Material action routes to a Material Document.
- `engine/render/MaterialPreview.h`, `IRenderAdapter.h`: optional preview frame/metrics interface.
- `engine/render/backends/dx12/Dx12RenderAdapter.h`, `.cpp`: persistent isolated target, rendering/state restoration, lifetime and timestamp metrics.
- `tests/MaterialEditorUXRegression.cpp`: native workflow, pixels, errors, restart and measurements.
- `tests/EditorDocumentRegression.cpp`: existing dirty-material fixture edits the session draft.
- Root/engine `CMakeLists.txt`: new source and native producer/restart test fixture.
- `docs/MaterialEditorUX.md`: behavior, measurements, checks and limitations.

Work remains on `engine_workflow`, without a commit. No user Scene/Prefab/material assets or lab №3 files were changed.
