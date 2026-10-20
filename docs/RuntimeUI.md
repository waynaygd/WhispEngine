# Runtime Canvas and Core Widgets (UI.1)

Game UI is an independent CPU document/layout subsystem plus a DX12 renderer. It does not use ImGui widgets, draw lists, glyph data or font-atlas lifetime. ImGui is used only for the editor's authoring controls. The regression also renders Cyrillic UI with a raw DX12 adapter without initializing EditorLayer or ImGui.

## Create a HUD

1. Use the Viewport's **Create → UI → Canvas**, or add **UI Canvas** to an existing entity with Add Component.
2. A newly created Canvas references a new `ui/Canvas-<unique-id>.ui.json` in the active Project Asset Root. With an empty component choose **New UI Document**, **Choose UI Document**, type a `.ui.json` key, or drag an existing UI Document asset onto the Inspector's document control.
3. In the Canvas Inspector, choose **Add Widget**: Panel, Text, Image or ProgressBar. Select a widget in the tree/list, choose its parent Panel, then edit anchors, pivot, position, Size Delta, local Z, visibility, opacity and colors. Text adds multiline text, size and left/center/right alignment; Image adds a texture reference and UV rectangle; ProgressBar adds value/fill color. Panel can clip descendants.
4. Use **Undo UI / Redo UI** for document changes and **Save UI** to write the separate document. Scene Undo/Redo remains for entity/component changes. Save the scene separately to persist the Canvas link.
5. **UI Preview** in the Viewport toolbar hides/shows the overlay in Edit. In Play UI always renders. There are no widget hit targets or input events, so UI never captures scene picking or gameplay input.

Open `scenes/interactive_room_ui.json` for the demonstration, or assign `ui/PlayerHUD.ui.json` to a Canvas in another scene. This is a copy of Interactive Room with a single Canvas; the original room, scripts, prefabs, story and gameplay remain unchanged. Health/Sanity values and `[E] Interact / Нажмите E` are static examples, without additional gameplay systems or Lua bindings. For a standalone project copy the UI document (and the room's normal assets if using the whole sample) into its Asset Root.

## Document format

A version-1 `.ui.json` is a project resource loaded/cached by ResourceManager. Its tree is represented by stable widget IDs and parent IDs, entirely separate from ECS parenting. Exactly one parentless root Panel is identified by `root`; an empty Canvas can instead contain `widgets: []` and `root: ""`.

```json
{
  "version": 1,
  "name": "PlayerHUD",
  "enabled": true,
  "referenceResolution": [1920, 1080],
  "scaleMode": 0,
  "sortOrder": 0,
  "root": "Root",
  "widgets": [
    {"id":"Root", "type":"Panel", "anchorMax":[1,1], "size":[0,0], "color":[0,0,0,0]},
    {"id":"Background", "parent":"Root", "type":"Panel",
     "position":[32,32], "size":[440,100], "color":[0.03,0.04,0.06,0.85], "clipChildren":true},
    {"id":"Prompt", "parent":"Background", "type":"Text",
     "position":[16,8], "size":[400,44], "text":"[E] Нажмите E", "fontSize":28, "color":[1,1,1,1]},
    {"id":"Health", "parent":"Background", "type":"ProgressBar",
     "position":[16,60], "size":[400,24], "value":0.8,
     "color":[0.08,0.09,0.1,1], "fillColor":[0.15,0.85,0.28,1]}
  ]
}
```

Common defaults: `visible: true`, `opacity: 1`, `anchorMin/anchorMax/pivot/position: [0,0]`, `size: [200,60]`, `zOrder: 0`, white `color`. Text defaults to 24 pixels at reference scale and `textAlignment: 0` (left; 1 center, 2 right). Image uses `texture: "textures/icon.png"` and `uv: [0,0,1,1]`; it stretches the specified UV rectangle to the widget rectangle. ProgressBar fills left to right, from 0 to 1. Colors are display-referred RGBA, not HDR radiance. Opacity and visibility propagate down the tree; background color alpha affects only that background, not the children's opacity.

The document has no persistent GPU handles or World/entity pointers. IDs are stable across save/load and editing; the simple editor creates unique IDs and preserves them when editing other properties. Removing a widget removes its subtree. Cycles, duplicate/missing IDs, unknown types, non-Panel parents and nesting beyond 64 ancestors are rejected. Documents are limited to 2048 widgets, 16 KiB UTF-8 per text and a total draw budget of 131072 vertices per frame. Invalid numeric ranges, NaN/Infinity, reversed anchors/UVs and negative fixed sizes are diagnosed.

UI document paths use the active Project Asset Root, portable forward slashes and `.ui.json`; absolute paths, `..` and canonical paths escaping the root are rejected. Image keys use the existing texture resource-key rules. Missing/loading textures draw a magenta placeholder. ResourceManager caches CPU decoding and RenderSystem reuses its existing texture upload/finalization queue and GPU-resource ownership; images are not decoded or uploaded every frame.

## Layout and Canvas scaling

Only **Screen Space Overlay / Scale With Screen Size** is implemented (`scaleMode: 0`). The document's Reference Resolution defaults to 1920×1080. A single uniform scale is `min(viewportWidth/referenceWidth, viewportHeight/referenceHeight)`. The logical root area is the actual Viewport size divided by that scale, so aspect-ratio changes add logical room rather than stretching glyphs. Anchors still reach actual Viewport edges; positions/sizes scale uniformly. Inspector and other editor panels have no input into layout except the resulting Viewport render size.

For each axis, with parent start P, parent size L, anchor span A = L×(anchorMax−anchorMin):

- evaluated size = max(0, A + SizeDelta)
- evaluated start = P + L×anchorMin + A×pivot + anchoredPosition − evaluatedSize×pivot

Equal anchors make a fixed-size widget; separate anchors stretch the widget. With a center pivot, stretch anchors [0,0]→[1,1] and SizeDelta [-20,-20] produce 10-pixel margins. A fixed [0.5,1] anchor, [0.5,1] pivot and negative Y position produce a bottom-centered prompt. Nested widgets use the parent's evaluated logical rectangle. Every frame recomputes from authored values, avoiding resize accumulation. Extremely small stretched dimensions clamp to zero.

A Canvas entity's Sort Order Offset adds to the document Sort Order. Canvases sort by total order, stable SceneIdentity and entity index. Within a Panel, children sort by local Z then stable widget ID, with the parent background drawn first and each subtree kept together. `clipChildren` intersects descendant scissor rectangles with the Panel rectangle; text is also clipped to its own rectangle. Clipping uses existing DX12 scissors and does not need stencil/depth or a UI render target.

## Font and text

`ui::RasterizeFont` uses Windows GDI's grayscale TrueType glyph outlines at 32-pixel character height (Segoe UI). It builds a private 1024×512 RGBA atlas once and uploads it through the existing texture mechanism. Latin ASCII, digits, basic punctuation and Cyrillic U+0400–U+045F (including Ё/ё) are included. Glyph advances, bearings, ascent and line height are retained; spaces have advances without bitmap geometry. The decoder validates UTF-8; unsupported/invalid characters use `?`. Explicit newlines and uniform text scaling are supported.

There is one regular system font, with no shaping, kerning engine, font fallback chain, rich text, automatic wrapping or font selection. Large magnification of the bitmap atlas can soften edges. The font is supplied by Windows at runtime; no font file is copied from ImGui or redistributed. Other platforms would need a rasterization implementation at this small atlas boundary. The current runtime renderer is DX12.

## Rendering and lifecycle

RenderSystem finishes the existing scene/fog/environment output, tone mapping/FXAA, built-ins and ordered custom HLSL PostFX. After optional scene collider diagnostics it builds the UI list and calls `DrawRuntimeUI`, before Editor ImGui and Present. The UI therefore remains outside PostFX. It blends directly into the existing R8G8B8A8_UNORM Viewport/backbuffer RTV, without a new UI target or sampling that destination.

CPU layout emits colored/textured triangles plus texture handles and scissor commands. The DX12 backend owns a small fixed shader/root signature with source-alpha blending and no depth/stencil. Its vertices use a per-in-flight-frame upload buffer, growing geometrically only when necessary; old buffers retire through existing fence queues. Font/image SRVs use the existing persistent descriptor infrastructure and pixel-SRV texture states. The final Viewport/Present barriers remain unchanged. Resize/F11 change layout coordinates and scissors; the UI renderer has no resolution-sized textures to accumulate.

`UICanvasComponent` only stores enabled, document key and sort-order offset. Scene/Prefab serialization, component Undo/Redo and Copy/Paste use the existing registry path. RuntimeUI keeps independent document-state copies per full entity/world identity and resource revision, with no Editor World pointers. Entity deletion/removal and world switches remove stale instances. Source documents and runtime copies remain separate; runtime rendering never writes JSON. Texture/font GPU resources follow existing RenderSystem/device shutdown, while resource caches may retain reusable CPU/GPU assets normally.

## Editing, saving and validation

The Inspector publishes authored drafts to the cached UI resource for preview and has a separate, bounded document history (128 snapshots), grouped over continuous edits. No World clone is made. **Save UI** validates and uses the existing atomic temporary-file/replacement writer. It checks whether disk content changed outside the document session and refuses to silently overwrite it. **Reload UI from disk** is an explicit discard action with confirmation. New document creation is a separate asset operation; undoing Canvas creation does not delete its UI file.

Unsaved drafts and their histories belong to the current editor session; Save UI is required before closing/reopening the application. Ordinary scene saving does not silently save documents. While dirty, a UI resource does not auto-reload over an authored draft. Image errors appear in the Inspector; invalid/missing documents are diagnosed in Inspector/Console. Scene Validation loads/parses documents read-only and checks widget structure, IDs, dimensions, finite values and Image references without uploading textures to the GPU.

## UI.2 / UI.3 extension points

Visual authoring is now implemented by [Widget Designer](WidgetDesigner.md), reusing these data, layout and document transactions. The paragraph below describes the UI.1 boundary.

`ui::Document`, stable Widget IDs and `LayoutRect`/`LayoutDocument` are editor-independent data/commands shared with Widget Designer UI.2. `EditUIDocument` is the current authoring transaction entry point and is reused by the visual handles/anchor presets. RuntimeUI's per-entity `Instance::state` is the later temporary UI-state boundary; a future Lua API should change that instance rather than shared documents or files. No Lua UI API, widget input events, Button, animations or world-space UI is implemented by UI.1.


## Verification results

WhispEngine and the affected regression targets built successfully. The final RuntimeUIRegression passed 800 checks on native DX12. It covers an empty Canvas, alpha compositing, actual UTF-8 Cyrillic/newline glyph pixels, standalone rendering without EditorLayer/ImGui initialization, ResourceManager Image loading, ProgressBar fill, nested clipping, anchors/reference scaling, Canvas overlap ordering, separate instances and deletion, explicit document Undo/Redo and atomic save/load, external-file conflict protection, scene persistence, Copy/Paste and Prefab Apply/Revert. It checks opaque HUD pixels over working grayscale and VHS, preview switching, Play/Pause/Stop, portrait/landscape resize, F11 and zero validation errors. Extreme signed sort orders are rejected without integer overflow.

The Interactive Room HUD sample additionally exercises real FPS capture/camera ownership, movement, cursor release, Clean Play F11 and unchanged Scene Picking. An unoccluded native viewport capture was visually inspected at `out/build/shadows-dx12/diagnostics/runtime-ui/room-scene.png`, including both bars and Russian text.

The final combined six-test run passed in 82.76 seconds: Runtime UI (then 798 checks), Built-in Screen Effects (148), Custom PostFX (140), Fog CPU (42), Fog DX12 (204) and Interactive Room (198). A following UI-only run passed 800 checks after adding integer-boundary validation. The first font test found the GDI space-glyph case (zero bitmap bytes with a nonzero reported black box); this was corrected with explicit empty-glyph and bitmap-size handling, then covered by the passing native text tests. `git diff --check` passed. No benchmark or large scene was introduced; temporary assets and developer preferences are restored by the tests.

## Changed files

Added:
- `engine/ui/UIDocument.h/.cpp`: document/schema, graph validation, loader and atomic saving.
- `engine/ui/RuntimeUI.h/.cpp`: independent instances, layout, UTF-8-ready text geometry, clipping and draw lists.
- `engine/ui/UIFont.cpp`: independent Windows glyph rasterization/atlas and UTF-8 decoding.
- `engine/ecs/components/UICanvasComponent.h`: entity/document ownership link.
- `engine/render/backends/dx12/Dx12RuntimeUI.cpp`: alpha-blended UI shader, vertex buffers and scissors.
- `engine/editor/EditorRuntimeUI.cpp`: creation, document history, explicit saves and widget Inspector.
- `engine/assets/ui/PlayerHUD.ui.json`, `engine/assets/scenes/interactive_room_ui.json`: demo UI and separate demo scene.
- `tests/RuntimeUIRegression.cpp`, this document.

Extended:
- `engine/resources/ResourceManager.h/.cpp`: cached UI resource type and dirty-document reload protection.
- `engine/ecs/ComponentRegistry.h/.cpp`: Canvas component metadata/serialization and UI document asset kind.
- `engine/ecs/systems/RenderSystem.h/.cpp`: ready draw-list evaluation, shared image/finalization path and UI resource lifetime.
- `engine/render/IRenderAdapter.h`, `engine/render/backends/dx12/Dx12RenderAdapter.h/.cpp`: render-area dimensions, UI draw hook and device cleanup.
- `engine/editor/EditorLayer.h/.cpp`, `EditorInspector.cpp`, `EditorViewportCreate.cpp`, `EditorUi.cpp`, `AssetCatalog.cpp`, `EditorAssets.cpp`, `SceneValidation.cpp`: preview toggle, creation/selection and read-only validation.
- One small render-frame connection in `engine/core/Application.cpp` to keep UI enabled in Play and honor Edit preview visibility.
- Root/engine `CMakeLists.txt`, `tests/ApplicationTestAccess.h`: sources, system GDI link, demo copying and targeted checks.
