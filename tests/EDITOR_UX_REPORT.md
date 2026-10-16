# Editor UX / Viewport review

Branch: `scripting`. No commit, push, merge, reset or clean performed.

| Feature | Status | Primary files | Verification |
|---|---|---|---|
| Alt+3 / Alt+4 Lit/Unlit | Implemented, DX12 PASS | Application.h, RenderSystem.cpp/.h, EditorLayer.cpp | 20 roundtrips per forced material model; pixel equality, scene/dirty/history invariant; native shortcuts |
| Asset Browser Context Create | Implemented, PASS | EditorAssets.cpp, EditorImportUi.cpp, EditorLayer.h | Context destination, no overwrite/traversal, Lua/Prefab schema, Play guards; native scripts-folder creation |
| Game View auto-exit | Implemented, PASS | EditorTransactions.cpp, EditorUi.cpp, LightGizmos.cpp, EditorLevelWorkflow.cpp, EditorAssets.cpp | Technical/history/runtime selection, successful/empty viewport hits; native Hierarchy selection |
| Sun Disc Cleanup | Implemented, DX12 PASS | editor_visuals.hlsl, EditorLayer.cpp | Day/Night and known HDR captures, glow alignment, HDR preservation, legacy diameter independence |
| Viewport Create Menu | Implemented, PASS | EditorLayer.cpp, EditorUi.cpp, EditorTransactions.cpp, LightGizmos.cpp | Synthetic popup, existing Undo/Redo tests; native Create → Cube → Ctrl+Z |

## Architecture

- **Asset Browser:** creation lives in folder/empty-content context menus. The folder is passed directly to the action, independently of the current selection. Lua, Prefab and Material require `scripts`, `prefabs` and `materials` respectively, including subfolders. Incompatible actions are disabled with a destination tooltip. The existing Lua generator and Prefab serializer were moved into callable editor actions; material creation keeps its existing dialog and safety checks. New files become selected, and Lua/Prefab creation navigates to their folder. The existing import dialog receives the clicked folder. Refresh, Grid/List, Project Assets, card size and Locate selected moved into `...`. Search, type filter, navigation, asset actions and drag payloads remain intact.
- **Paths:** Lua/Prefab resolve through `scripting::SafeAsset`, material creation through `AssetCatalog::SafeMaterialPath`; canonical containment and symlink checks remain. New Folder now targets the active Asset Root. Import retains its existing validated project/staging publication workflow. No script copies are made on opening, and `ScriptEditor.cpp` remains the shared Visual Studio action.
- **Viewport Create:** the existing entity menu moved from Hierarchy into the local Viewport bar, calling the same creation/transaction actions. Hierarchy starts with search. Game View wraps onto a second row in narrower viewports. Creation is disabled during Play; entry points for Empty/Light/Duplicate also reject Play.
- **Selection:** `SelectEntityFromUser(Application&, Entity)` exits Game View only for a living entity in Edit. Hierarchy, mesh/light picking, create, duplicate, paste, prefab and imported-scene instantiation use it. `SelectEntity`, history restoration, startup selection and runtime mapping retain technical selection semantics. Mesh hit testing now also runs in Game View; empty hits do not exit it. Hidden light gizmos remain hidden.
- **Lit/Unlit:** `RenderSystem::m_ViewportUnlit` is transient and defaults to false. Application and the Viewport combo/Alt+3/Alt+4 route to it. Only local render constants are changed: lighting is disabled, IBL diffuse/specular are suppressed and the shadow pass is disabled. Environment enablement, exposure, tone mapping, material model overrides and authored Scene Settings are retained. Existing DX12 base-color shader fallback preserves albedo, UV, tint and depth. The flag has no serializer, preference or Undo entry. Shortcuts require Viewport keyboard ownership in Edit, and reject text input, popups, camera control, ImGuizmo and mouse navigation.
- **Sun:** the extra opaque layer was in `engine/shaders/dx12/editor_visuals.hlsl::SkyPS`: `lerp(c, displaySun * limb, disc * visible)` replaced the background while `(1-disc)` removed halo at the center. Both disc operations were removed; continuous additive/screen-blended halo, temperature, elevation attenuation, Day/Night scattering, HDR sky and light synchronization remain. Legacy JSON fields (`visibleSunDisc`, `sunRadius`, intensity/glow settings) remain loadable. UI describes glow and hides obsolete diameter. Existing HDR images can contain their own baked sun, which this procedural shader cannot erase.
- **Vulkan limit:** this branch's `VkRenderAdapter` exposes test primitives and shader hot reload, but has no scene-material/lighting/environment/editor-sky implementation. There is no equivalent sun shader to patch. Full Lit/Unlit and sky visual acceptance therefore applies to DX12; Vulkan scene parity is outside this patch.

## Regression evidence

- **Release:** full build of the existing `x64-release` configuration PASS. Final build log: `out/ux-build-review.log`.
- **Full CTest:** **32/32 PASS**, 158.03 seconds (`out/ux-ctest-final.log`). Includes Editor, lighting, materials, PBR, IBL, shadows, physics and Lua.
- **Final focused rerun:** **3/3 PASS**, 23.06 seconds, after the last UI refinements and new viewport-picking assertion (`out/ux-verify-review.log`). Lua retains **193 checks PASS**. Both runners save and restore exact layout/preference bytes and use the existing Lua diagnostic fixture.
- **New CPU coverage:** context-aware Lua/Prefab creation, distinct filenames instead of overwriting, wrong-folder/traversal rejection, existing prefab JSON spawning, Play guards, explicit selection versus technical/history/runtime selection.
- **New DX12 coverage:** 20 Lit/Unlit roundtrips for each forced Lambert/PBR model; textured/tinted base-color oracle, exact Lit pixel restoration, zero Unlit shadow submissions, unchanged scene JSON/settings/Dirty/Undo. Synthetic ImGui tests exercise Alt+3/Alt+4, Inspector shortcut blocking, successful mesh picking from Game View, empty-hit preservation and both popup entry points. No DX12 ERROR/CORRUPTION messages.
- **Native mouse/keyboard smoke PASS:** Alt+3 and Alt+4; Game View checkbox then Hierarchy selection restoring gizmo; right-click scripts while Project root was current → Create Asset → Lua Script, correct folder navigation/selection; Viewport → Create → 3D Object → Cube → Ctrl+Z. Native snapshots below are distinct from synthetic-input captures. The temporary Lua script was removed and original layout/preferences restored.
- **Sun rendering smoke:** before/after actual DX12 captures; after cases Day, known-sun Cubemap, sunset and night all exited successfully. Captures were visually inspected. This is rendered-frame evidence, not a claim of native mouse-driven sun rotation.
- **NOT VERIFIED in this run:** exhaustive native drag-and-drop, all folder/material/import modal interactions, repeat Visual Studio double-click, native resize and all Play combinations. Existing regressions and unchanged action paths cover parts of these; the manual checklist remains available below. Vulkan full scene shading/sky acceptance is unavailable for the backend implementation described above.
- **Git review:** `git diff --check` PASS. Protected LuaRuntime/bindings, ScriptEditor and SceneSerializer have no diff. All 16 changed/new tracked-source paths are listed below; generated diagnostics are under ignored `out/`.

The old HDR-sun test expected an opaque green procedural disc to replace a bright authored sun. Its oracle now checks preservation of authored HDR radiance and independently checks alignment of the isolated continuous glow. Existing Day/Night, rotation and lighting checks remain. The diameter-independence assertion rejects reintroduction of the old disc.

## Screenshot evidence

| Capture | File |
|---|---|
| Native Asset Browser / Create Asset submenu | [native-asset-context.png](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor-ux/native-asset-context.png) |
| Native Viewport / primitive menu and Lit control | [native-viewport-create.png](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor-ux/native-viewport-create.png) |
| Native Lit / textured scene | [native-lit.png](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor-ux/native-lit.png) |
| Native Unlit / same scene | [native-unlit.png](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor-ux/native-unlit.png) |
| Native Game View exit / selected entity and gizmo | [native-selection.png](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor-ux/native-selection.png) |
| Sun before, opaque disc | [ux-before-day.png](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/ux-before-day.png) |
| Sun after, continuous glow | [ux-after-day.png](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/ux-after-day.png) |
| Authored Cubemap sun after | [ux-after-known-sun.png](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/ux-after-known-sun.png) |
| Sunset / night after | [sunset](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/ux-after-sunset.png), [night](C:/Users/Тимофей/source/repos/WhispEngine/out/diagnostics/editor25/ux-after-night.png) |

## Manual smoke (five functions)

1. Open a textured PBR scene, focus Viewport, press **Alt+3**, then **Alt+4**, and use the Lit/Unlit combo. Repeat 20 times; compare Dirty indicator before/after. Repeat with forced Lambert. Check text fields, popup/modal, camera navigation and Play do not route these shortcuts to the editor.
2. Right-click `scripts`, `prefabs`, `materials`, their content folders and empty content. Use **Create Asset** and verify the displayed folder. Verify incompatible types are disabled with a tooltip; Prefab requires an entity. Exercise New Folder, Import, search/type filter and `...`. Double-click Lua to open Visual Studio; drag Lua/Material/Mesh to their existing targets.
3. Enable **Game View**, select a living entity in Hierarchy or click a visible mesh. Selection remains and Game View turns off. Reenable it, click empty space and move the camera; Game View remains on. Repeat a gameplay click in Play.
4. Open a Day/Night scene, look toward the sun and rotate the Primary Directional Light. Verify continuous glow without an opaque inner circle, correct light direction and daylight/twilight/night. Repeat Gradient and Cubemap with an HDR without a baked sun; compare before/after captures.
5. Use **Viewport → Create → 3D Object → Cube**. Verify selection, Inspector, Dirty, one Undo and Redo, then Save/Load in a disposable scene. Check Empty and all three Light types. Resize Viewport and confirm controls remain inside its bar; check Create is disabled in Play.

## Changed files

- `engine/core/Application.h`
- `engine/ecs/systems/RenderSystem.cpp`, `.h`
- `engine/editor/EditorAssets.cpp`
- `engine/editor/EditorImportUi.cpp`
- `engine/editor/EditorLayer.cpp`, `.h`
- `engine/editor/EditorLevelWorkflow.cpp`
- `engine/editor/EditorTransactions.cpp`
- `engine/editor/EditorUi.cpp`
- `engine/editor/LightGizmos.cpp`
- `engine/shaders/dx12/editor_visuals.hlsl`
- `tests/EditorUsabilityRegression.cpp`
- `tests/EditorLevelWorkflowRegression.cpp`
- `tests/Editor25Regression.cpp`
- This report.

LuaRuntime, Lua bindings, prefab schema, scene/material serialization, resource manager, physics and gameplay input bindings are unchanged.
