# Built-in Screen Effects (PPV.2)

Use the existing Post Process Volume Inspector. Each parameter has its own nullable Override checkbox. Unchecked values inherit the base or lower-priority volumes. The four new sections use the same edit transactions, serialization, prefab handling and Scene Validation as Fog. Newly enabled overrides start from the defaults below. Tint is a display-referred RGB multiplier; its color picker performs no linear/sRGB conversion and exposes no alpha control.

| Section / parameter | Default | Range |
| --- | --- | --- |
| Color Grading / Saturation | 1 | 0–2 |
| Contrast | 1 | 0–2 |
| Brightness | 0 | −1–1 |
| Color Tint | white | RGB 0–1 |
| Vignette / Intensity | 0 | 0–1 |
| Smoothness | 0.5 | 0.01–1 |
| Roundness | 1 | 0–1 |
| Film Grain / Intensity | 0 | 0–1 |
| Scale | 1 pixel | 0.5–4 pixels |
| Chromatic Aberration / Intensity | 0 | 0–1 |
| Radial Falloff | 2 | 0.1–8 |

Contrast pivots around display gray 0.5; Brightness adds display values and is not another Exposure. Exposure remains the PPV.1 Environment value applied once by the existing output transform. Saturation uses Rec.709 luminance weights. Vignette blends rectangular and aspect-correct circular distance with a smooth edge and black corners. Grain is fine monochrome hash noise with bounded ±0.03 display amplitude at full intensity and smooth interpolation between seeds every four rendered frames; seeds repeat after 1024 frames. Chromatic offsets are radial (up to 1.5% of the screen at full intensity) and clamp inside texel centers.

## Actual DX12 integration

The existing `BeginHdrScene` / `EndHdrScene` path in `Dx12Environment.cpp` already resolves a per-frame R32G32B32A32_FLOAT scene target into an R8G8B8A8_UNORM viewport or swapchain target. Scene/fog/environment operate on linear radiance in that HDR path. `DisplayColor` applies the existing exposure, optional Reinhard tone map and sRGB encoding; FXAA interpolates those display-referred colors. The built-in stage now follows that transform, before Editor ImGui composition. No lighting or fog shader algorithm changes.

`ResolveDisplay(uv)` retains the previous tone mapping / FXAA implementation. `BuiltInPostFX(uv,size)` first resolves displaced R/G/B samples when aberration is enabled, then applies grading, vignette and grain. It never applies exposure to an already transformed sample. Output alpha stays 1 as before. The neutral branch calls the old resolve directly. With no active screen effects the old eligibility condition remains unchanged, so a scene that previously required no HDR intermediate still requires none. Active effects enable the existing HDR path even with Environment, Fog and AA disabled.

There is still **one output fullscreen draw**, with no new texture, descriptor heap, upload buffer or per-frame allocation for screen effects. Constants use 20 root DWORDs. The HDR source transitions from SRV to RTV at scene begin, then RTV to pixel SRV at scene end. Output writes a separate viewport/backbuffer already in RTV state. The viewport later transitions RTV to SRV for ImGui; the backbuffer follows the existing Present barriers. No pass reads and writes the same resource. Existing per-frame targets, retirement fences and descriptor tables handle resize and F11; replacing a size retires resources through the existing queue rather than retaining a target for each historical resolution.

The editor's ImGui controls and its current ImGui gameplay overlays are submitted afterward and remain unaffected. There is no independent game HUD render pass to relocate. Any future game HUD that should remain clean belongs after `EndMainPass` / `EndHdrScene`, with Editor UI composition.

## Settings and validation

`PostProcessVolumeComponent` → the existing single volume collection, box weight and deterministic priority blend in `EvaluatePostProcess` → `EvaluatedPostProcess.screenEffects` → `RenderSystem::SetScreenEffects` → DX12 sanitized value copy → root constants → HLSL.

`WHISP_PP_FLOATS` includes all new scalar overrides, so metadata, default initialization, blending and numeric validation share the same supported field list. Tint follows the existing optional color path. No new scene/project format version is needed; missing fields remain unchecked, leaving old scenes neutral. Scene Validation diagnoses finite/range violations including tint. As in PPV.1 an invalid volume is ignored during evaluation. `SanitizeScreenEffects` independently clamps finite bounds and replaces NaN/Infinity at the render API boundary, without modifying authored components or saved data.

## PPV.3 insertion point and current limits

The concrete built-in shader entry is `BuiltInPostFX`; the CPU stage is `EndHdrScene`, called by `EndMainPass` after Fog. For subsequent ordered effects, redirect this output RTV to the first display-referred intermediate, then append fullscreen draws in the requested order, alternating distinct SRV/RTV resources and transitioning each between draws; the last draw targets the existing viewport/backbuffer. Keep those draws before UI submission. They consume evaluated settings rather than traversing volumes or repeating tone mapping. PPV.2 needs no such extra targets because all four effects fit in the existing output draw.

No custom effect registry, manifest loader, reflection, Lua API, hot reload, LUT, new dependency or speculative interface is introduced. Screen effects are implemented for DX12; other adapters retain their existing rendering. Grain advances on rendered frames, including Pause, and its temporal cadence is therefore frame-rate dependent.

## Targeted verification

`ScreenEffectsRegression` renders Interactive Room on native DX12 and reads actual viewport/frame pixels. It checks neutral identity, grayscale, neutral explicit overrides, vignette center/edges, grain changes, aberration, contrast/brightness/tint, HDR exposure and both FXAA modes, one output draw, unchanged menu pixels, independent local/global blending, validation/sanitizing, Undo/Redo, Copy/Paste, Prefab Apply/Revert, Save/Open, runtime camera, Pause/Stop, repeated resize with bounded target bytes, F11/workspaces and DX12 debug errors. Temporary scene/prefab files and developer preferences are restored. Captures live in `out/build/shadows-dx12/diagnostics/screen-effects/`.

Existing `PostProcessVolumeRegression`, `FogRegression`, `FogDx12Regression` and `InteractiveRoomRegression` cover volume transforms/atmosphere, all fog modes and gameplay interactions, Clean Play capture, Tab, F11 and movement. No benchmark or large synthetic scene is used.

Final build: WhispEngine and all four regression executables succeeded. The combined five-test run passed in 71.39 seconds: Screen Effects 148 checks, PPV.1 102, Fog CPU 42, Fog DX12 204, Interactive Room 198. Neutral/HDR/FXAA image comparisons were exact; DX12 validation reported zero errors. Native scene captures were visually inspected (`comparison.png`). `git diff --check` passed.

## Changed files

Added: `engine/render/ScreenEffects.h`, `tests/ScreenEffectsRegression.cpp`, `docs/ScreenEffects.md`.

Extended: `engine/render/PostProcess.h`, `engine/ecs/components/PostProcessVolumeComponent.h`, `engine/ecs/PostProcessEvaluation.cpp`, `engine/ecs/ComponentRegistry.cpp`, `engine/ecs/systems/RenderSystem.cpp`, `engine/editor/EditorInspector.cpp`, `engine/render/IRenderAdapter.h`, `engine/render/backends/dx12/Dx12RenderAdapter.h`, `engine/render/backends/dx12/Dx12RenderAdapter.cpp`, `engine/render/backends/dx12/Dx12Environment.cpp`, root `CMakeLists.txt`.
