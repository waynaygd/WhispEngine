# Custom HLSL PostFX (PPV.3)

Custom effects are project assets: a versioned `.postfx.json` plus a relative `.hlsl`. Add the manifest to a Post Process Volume using **Inspector → Custom PostFX → Add Effect**, drag a Custom PostFX asset onto that combo, or use **Asset Browser → Assign to Active Entity** with a Volume selected. **Refresh Effects** discovers newly created manifests. No C++ changes are needed to add an effect.

## Minimal example

Create `Assets/postfx/example/Example.postfx.json` under the active Project Asset Root:

```json
{
  "version": 1,
  "id": "mygame.example",
  "name": "Example tint",
  "description": "A display-space RGB multiplier",
  "shader": "Example.hlsl",
  "stage": "AfterTonemap",
  "order": 100,
  "parameters": [
    {"name":"tintColor", "type":"float3", "default":[1,0.7,0.5], "min":0, "max":1},
    {"name":"enabledTint", "type":"bool", "default":true}
  ]
}
```

Create `Example.hlsl` alongside it:

```hlsl
#include "WhispPostFX.hlsli"
float4 PostProcess(float2 uv)
{
    float4 color = SampleSceneColor(uv);
    if (enabledTint) color.rgb *= tintColor;
    return color;
}
```

The engine supplies the include virtually and generates typed variables from the manifest. Do not copy it into the project. The engine wrapper applies `lerp(original, result, EffectWeight)` **once**; return the full-strength effect from `PostProcess`. A zero effect weight skips the fullscreen draw. Effects which cannot be meaningfully blended by their final color are outside this contract.

The included real example is `engine/assets/postfx/vhs/VHS.postfx.json` + `VHS.hlsl`: scanline strength, tracking and frequency. It is copied into the existing standalone asset root by the normal build. For a separate project copy those two files into that project's Asset Root.

## Manifest contract

Version 1 supports `float`, `float2`, `float3`, `float4`, `int`, `bool`. Numeric vector ranges apply independently to every component. Numeric defaults must match the declared shape and finite range; integer overrides must be JSON integers. Bool values must be booleans. There are at most 16 parameters; each receives one float4 slot. Names are unique HLSL identifiers beginning with a lowercase letter, at most 64 characters; use descriptive names rather than HLSL keywords. Unsupported identifiers also produce normal compile diagnostics.

Required fields: version, id, name, shader, stage, parameters (possibly an empty array). Description is optional, order defaults to 0. IDs are stable and unique within the active asset root. Duplicate IDs are diagnosed and excluded rather than selected by filesystem traversal order. Only `AfterTonemap` is supported; order then stable ID determine execution order. Numeric ranges and order are bounded. No texture or sampler parameters are supported.

All resource and shader paths must remain under the active Project Asset Root, using forward slashes. The shader path is relative to its manifest's directory. Absolute paths, drive names, backslashes, `..` components and canonical paths escaping the root through links are rejected. An external engine-fallback path cannot be used to bypass the project root. Custom includes other than `WhispPostFX.hlsli` are rejected.

## Shader interface and restrictions

Available values/functions:

- `float4 SampleSceneColor(float2 uv)` reads the preceding pass with clamped linear sampling inside texel centers.
- `float2 ScreenSize`, `float Time`, `float DeltaTime`, `float EffectWeight`.
- Declared parameters, with generated float/vector/int/bool variables.
- Entry point `float4 PostProcess(float2 uv)`.

The engine owns VS/PS entry points, root signature, source texture, sampler and constants. The PS bytecode is checked for bindings: only the engine's t0 SceneColor, s0 SceneSampler and b0 PostFxConstants in space 0 are permitted. There is no reflection-driven Inspector framework; the small binding check rejects arbitrary textures, UAVs and extra constant buffers. These are fullscreen pixel effects only, with no compute dispatch, custom depth/stencil writes or arbitrary root signatures. Nonfinite returned colors fall back to the input pixel. Output RGB is clamped, alpha preserves the input.

This is a graphics resource contract, not a security sandbox for hostile HLSL. Developers must avoid unbounded or excessively expensive shader logic.

## Color and GPU execution

The order is existing scene lighting/environment/fog → existing exposure/tonemap/sRGB encoding and FXAA → PPV.2 built-ins → ordered custom passes → existing ImGui editor/game overlays → Present.

Custom shaders receive **display-referred sRGB-encoded RGB values**, stored in `R8G8B8A8_UNORM`; their texture view performs no automatic sRGB decode. They do not receive linear HDR. They must not repeat tone mapping/exposure unless the effect deliberately changes display colors.

When ready effects are active, the existing built-in output draw writes a separate display target instead of the final viewport/backbuffer. The first custom pass reads that target. Intermediate passes alternate two per-frame display targets; the last writes the existing final RTV. With one effect only one intermediate is needed. RTV/SRV descriptors are owned by those targets and copied into the existing per-frame shader-visible descriptor table. Every intermediate write transitions SRV → RTV, then RTV → pixel SRV before the next read. No pass reads its destination. Existing viewport/Present barriers remain unchanged.

Targets are allocated only on first use or changed resolution, with old textures and descriptor heaps retired through the existing frame-fence queues. The count is bounded by two display targets per in-flight frame. A per-frame upload buffer holds 32 aligned 512-byte constant blocks and is reused after the frame fence. Old PSOs are similarly retired after in-flight use. Compilation workers capture immutable CPU definitions and never access the renderer/device; their completion creates a replacement PSO on the render thread. Device shutdown waits existing GPU work and drops programs/targets/buffers. Failed or compiling effects do not block other ready effects.

With no requested effects, the PPV.2 output path is unchanged. Disabled or zero-weight effects submit no custom draws. During first compilation an unavailable effect is skipped; its shader appears after successful compilation. Custom effects are limited to 32 active IDs per camera, sorted deterministically.

## Volume blending and persistence

The existing Volume traversal, OBB influence, priority and stable-ID layering are reused. Custom layers are collected during the same loop; the resolver does not traverse World or recalculate spatial weights.

Each entry saves resource key, stable effect ID, enabled, weight and an object of parameter overrides. Presence of a parameter in that object is its Override flag. Defaults come from the manifest, not from zero. Starting from effect weight zero, each Volume blends weight toward its enabled entry weight (or zero if disabled) using the Volume influence. Float/vector overrides blend with that same influence; int/bool select at influence ≥ 0.5. Unchecked fields preserve previously evaluated values. Different effect IDs never share values. The final weight controls the wrapper's color blend independently of parameter defaults.

Scene serialization remains the existing component JSON format with an additional optional `customEffects` array. Missing resources, removed parameters and incompatible saved values are preserved in authored data. Inspector diagnoses mismatches and offers an explicit Use default/reset action; rendering ignores incompatible overrides. Copy/Paste, scene Undo/Redo and Prefab Apply/Revert use ordinary component snapshots. Evaluation/rendering never write back to components. Scene Validation parses files and checks paths, identity, ranges and overrides without compiling HLSL or changing authored data.

## Lua Runtime control

```lua
-- Inside a gameplay script, during Play:
postfx.set("mygame.example", "tintColor", {1.0, 0.3, 0.2})
postfx.set("mygame.example", "enabledTint", true)
postfx.set_weight("mygame.example", 0.8)
postfx.clear("mygame.example")
```

Calls return true on success, false plus Console diagnostics for unknown IDs/parameters and invalid values. Use numbers for floats/ints, booleans for bools and sequential tables for vectors. Runtime overrides apply after Volume evaluation to the active camera. Setting a parameter alone preserves the evaluated weight; `set_weight` can also activate a registered effect without an authored Volume. `clear` removes that ID's temporary values and restores the Volume result.

Overrides are JSON values owned by the existing LuaRuntime instance, never Lua references or component writes. Start/Stop clear the map; runtime instances and editor/prefab serialization remain separate. Changed manifests revalidate temporary values each frame, ignoring values which no longer match the schema. Pause retains overrides; Stop discards them. No tween/Timeline API is added.

## Hot Reload and diagnostics

Loaded effects register their manifest and HLSL with the existing ResourceManager file polling. HLSL and valid manifest changes replace only that immutable CPU definition and schedule one background compile for the changed effect. No shader is compiled every frame. If another edit arrives during compilation the newest definition is compiled next. New files are discovered by Refresh Effects. **Reload Effect** explicitly retries either file, and retries either file immediately. Manifest changes refresh their shader dependency after the watch-map traversal; removed files are reported once and are retried when they reappear.

A parse or compile failure retains the last working definition/program. For a compile failure after a schema edit, rendering uses the old PSO together with its old parameter layout/defaults. Inspector shows resource/GPU diagnostics and whether the last working shader is retained; Console records loader/compiler failures. Repairing the files and reloading recovers without restarting. An initial invalid effect is skipped. A schema's new invalid runtime values cannot become stale Lua references.

Limits: DX12 only; no custom depth/texture inputs, user samplers, compute or Render Graph; no arbitrary includes. Timing is render-frame based and can advance while gameplay is paused. A shader compile can still be expensive on its worker, and PSO creation remains a small render-thread operation.


## Validation results

WhispEngine and all affected regression executables built successfully. Final CustomPostFxRegression: 140 checks on native DX12, including the six GPU parameter types, real VHS row alternation, separate weight/parameter blending, ordered passes, zero-weight identity, actual Lua script calls and GPU changes, automatic file polling, manual reload, compile-error fallback, unsupported bindings, duplicate IDs, read-only validation, Undo/Redo, Copy/Paste, Prefab Apply/Revert, scene Save/Open, runtime session isolation, Pause/Stop, resize, F11, unchanged ImGui pixels and zero DX12 validation errors.

The final combined CustomPostFxRegression + InteractiveRoomRegression run passed in 47.32 seconds (140 + 198 checks). Earlier targeted runs passed ScreenEffectsRegression (148), PostProcessVolumeRegression (102), Fog CPU (42), Fog DX12 (204), LuaGameplayApi CPU (100) and DX12 (168). The first broader run hit the previously observed Interactive Room capture failure at check 49; its isolated rerun and final combined rerun passed without changing input/controller code. The expanded parameter test caught a macro/swizzle naming collision, which was fixed with per-invocation typed variables and confirmed by real vector/int/bool GPU values. The VHS sampling frequency was also corrected and is checked against actual pixel-center rows.

The final native editor capture was visually inspected; numeric image checks cover the complete viewport even when unrelated diagnostic windows overlap it. Temporary assets are owned by the tests and removed; the existing preference guard restores developer settings. No benchmark or stress scene was run. `git diff --check` passed.

## Changed files

Added:
- `engine/resources/PostFxResource.h/.cpp` — manifest/data contract, strict paths, type checking and shared resolution.
- `engine/render/backends/dx12/Dx12PostFx.cpp` — compilation, binding checks, PSO lifecycle, ping-pong passes and constants.
- `engine/assets/postfx/vhs/VHS.postfx.json`, `VHS.hlsl` — actual project effect example.
- `tests/CustomPostFxRegression.cpp`, this document.

Extended:
- `engine/resources/ResourceManager.h/.cpp` — native resource type, discovery, diagnostics and manifest/HLSL watches.
- `engine/render/PostProcess.h`, `engine/ecs/components/PostProcessVolumeComponent.h`, `engine/ecs/PostProcessEvaluation.cpp` — authored entries and layers collected by existing Volume evaluation.
- `engine/ecs/ComponentRegistry.h/.cpp` — asset kind and component snapshot serialization.
- `engine/ecs/systems/RenderSystem.h/.cpp`, `engine/render/IRenderAdapter.h` — evaluated GPU frame and runtime overrides.
- `engine/render/backends/dx12/Dx12RenderAdapter.h/.cpp`, `Dx12Environment.cpp` — final-output routing and lifecycle.
- `engine/editor/AssetCatalog.cpp`, `EditorAssets.cpp`, `EditorInspector.cpp`, `EditorLayer.h`, `EditorPostProcess.cpp`, `SceneValidation.cpp` — resource use, manifest controls and read-only diagnostics.
- `engine/scripting/GameplayServices.h`, `LuaRuntime.h/.cpp`, two small connections in `engine/core/Application.cpp` — session-owned Lua API and host resource access.
- Root and engine `CMakeLists.txt` — sources, demo assets and targeted regression target.
