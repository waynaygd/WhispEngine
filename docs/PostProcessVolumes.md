# Post Process Volume Core (PPV.1)

Create a volume through **Viewport -> Create -> Environment -> Global Post Process Volume / Local Post Process Volume**, or add **Post Process Volume** to an entity in Inspector. Global and local use the same component. Creation starts with an enabled Distance Fog override, intensity 0.65, start 2 and end 30; Add Component starts with inherited parameters. Local volume extents are half sizes (full size = twice the extents, scaled by Transform). Select the local entity in Hierarchy and use the normal Move/Rotate/Scale Gizmo. Its box outline is editor-only, hidden in Game View and all Play states including Clean Play.

Inspector sections: Volume, Fog, Height Fog, Volumetric Fog, Environment and Atmosphere. Each parameter has an independent Override checkbox. Unchecked means inherit from the scene/earlier volumes; it is serialized as null, as with existing optional material overrides. Active values are saved. Enabling an override starts from the existing effect's defaults. Existing Inspector transactions, Mixed Values, Undo/Redo, Copy/Paste and prefab serialization are reused.

## Existing effects

All existing FogSettings fields are supported: Enabled, Distance/Height/Volumetric mode, intensity, color, start/end, world-space base height, height density/falloff, scattering color, extinction, anisotropy, maximum distance, quality and scene lighting. Fog profile height is in world coordinates and does not shift automatically with the volume box.

Existing environment parameters: IBL intensity, exposure, rotation and tone mapping. Asset paths and environment enable/diffuse/specular switches remain scene settings. Existing sky/DayNight parameters: sky intensity, horizon glow, sunset warmth, night intensity, sun disc intensity and sun glow intensity. Parameters have an effect in the sky mode that already consumes them; this does not introduce a separate physical atmosphere effect. PPV never creates lights, changes light components or drives sun synchronization.

Fog still uses the existing DX12 Fog pass and its Distance/Height/Volumetric algorithms. Sky still uses the existing gradient/cubemap/DayNight renderer. No shader algorithms, render passes or GPU ownership paths were added. A backend that did not support Fog previously gains no new Fog implementation from PPV.

## Blending

For a local box with world center c, orthonormal world axes u[j], and world half extents h[j], define outside distance:

`d = sqrt(sum(max(abs(dot(camera-c, u[j])) - h[j], 0)^2))`.

Inside/on the box, spatial weight is 1. Outside, if radius R > 0, use `t = clamp(d/R, 0, 1)` and `spatial = 1 - t*t*(3-2*t)`. Beyond R it is zero. Radius is measured in world units and is not scaled with the entity. R = 0 explicitly gives a hard boundary. The box uses the engine's composed World Transform, including rotation, parent scale, negative scale and pivot geometry; it is not an AABB. Unsupported shear/singular transforms follow existing hierarchy restrictions and are ignored safely.

Final alpha for each valid enabled volume is `spatial * Blend Weight`; global spatial weight is always 1. Zero weight and disabled volumes contribute nothing. Start from base settings every evaluation. Apply volumes in ascending `(Priority, persistent scene ID, Entity index, generation)` order. A later volume blends only its checked fields: `result = lerp(result, override, alpha)`. Thus higher priority is applied later; it does not erase unchecked fields or completely suppress lower priority at fractional alpha. Same-priority results are stable across save/load and ECS iteration order when scene identities are valid. Entities without identities use the deterministic handle fallback.

Float and color channels blend linearly. Fog Mode, Quality, Scene Lighting and Tone Mapping switch when alpha >= 0.5; their transitions are intentionally discrete. Fog Enabled is special: a continuous activation scalar starts at base enabled ? 1 : 0, is interpolated toward each checked enabled value, and multiplies final fog intensity. The pass is active only for positive activation. This avoids a half-weight fog pop when fading from a scene without Fog. Independently overridden start/end distances are kept renderer-valid by clamping evaluated start below 10000 and evaluated end to at least start + 0.001; authored values are never changed.

Invalid weight/radius/extents, nonfinite or out-of-range values, invalid enums, and explicitly conflicting start/end are reported by the existing Scene Validation and ignored at the render boundary. Local invalid/missing world transforms are also diagnosed. With no volumes, base rendering is unchanged. Invalid/nonfinite camera positions safely return base settings.

## Architecture and lifecycle

- Authored: `PostProcessVolumeComponent` and existing Fog/SceneSettings, serialized through ComponentRegistry in the unchanged scene/prefab format.
- Evaluated: `PostProcessSettings` / `EvaluatedPostProcess`, computed by `ecs::EvaluatePostProcess(cameraPosition, world, baseSettings)` without mutating World or base settings and without accumulation between frames.
- Renderer: RenderSystem gathers the legacy base Fog and scene Environment/Atmosphere, evaluates with its actual render camera, then sends copied Fog/Environment values to existing consumers. EditorVisualFrame evaluates the sky copy with the same actual viewport camera, retaining the existing explicit editor-background debug override behavior.

Application wires the existing scene background into RenderSystem via one additional settings pointer; it is never a retained World/component pointer. Edit, Runtime and Prefab Editor use the current World; gameplay uses the active camera already chosen by Application. Pause does not force simulation for evaluation. Stop discards Runtime clones as before. No Lua PostFX API is introduced.

## Verification

The targeted PostProcessVolumeRegression uses small owned scenes/prefabs and Interactive Room. It covers no-volume identity, globals, oriented locals, radius continuity, parent scale/rotation/pivot, priority/tie ordering, disabled/zero/unchecked fields, numeric validation, non-accumulation, real DX12 pixel differences, existing Height/Volumetric consumers, editor/game cameras, Undo/Redo, Copy/Paste, Prefab Editor, Save/Open, sky intensity and DX12 validation. Captures are in `out/build/shadows-dx12/diagnostics/post-process-volume/`.

Final results: WhispEngine and all affected test targets built successfully. PostProcessVolumeRegression passed 102 checks, including actual box vertices in Edit, their absence in Play with Game View disabled, and repeat Play. Existing Fog CPU passed 42 checks, Fog DX12 passed 204, and Interactive Room passed 198. The final combined four-test run passed; the strengthened PPV-only follow-up also passed. An earlier combined run had one FPS cursor-capture failure in Interactive Room; both its isolated retry and the final combined run passed, with no gameplay/input code changes. Native captures were visually checked.

Existing Fog CPU/DX12 and InteractiveRoom regressions are also run. Real developer preferences are restored by the existing test guard and temporary scene/prefab files are removed. No large scene, stress test or benchmark is introduced.

## PPV.2 / PPV.3 extension point

Extend the explicit `PostProcessSettings` and authored override fields, then the field blending policy in `EvaluatePostProcess`; `WHISP_PP_FLOATS` shares the supported scalar list across validation, Inspector metadata and blending. Reuse the same volume collection, OBB weights and deterministic layering for built-in or custom HLSL effect parameters. Future render consumers should consume evaluated values, not query/modify authored volumes or reproduce volume selection in a shader. Custom shader bindings/passes and temporary Lua override data belong to later consumers/inputs; no shader plugin system or new Lua control system is implemented in PPV.1.

## Files

Added: `engine/render/PostProcess.h`, `engine/ecs/components/PostProcessVolumeComponent.h`, `engine/ecs/PostProcessEvaluation.h/.cpp`, `engine/editor/EditorPostProcess.cpp`, `tests/PostProcessVolumeRegression.cpp`, this document.

Extended: `engine/ecs/ComponentRegistry.cpp`, `engine/ecs/systems/RenderSystem.h/.cpp`, one settings connection in `engine/core/Application.cpp`; editor `EditorInspector.cpp`, `EditorLayer.h/.cpp`, `EditorViewportCreate.cpp`, `EditorVisualSettings.cpp`, `EditorUi.cpp`, `EditorQualityOfLife.cpp`, `SceneValidation.cpp`; `tests/ApplicationTestAccess.h`; root and engine CMakeLists.
