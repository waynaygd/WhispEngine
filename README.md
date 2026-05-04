# WhispEngine

WhispEngine now runs in a single window and uses the render backend selected in config: `DX12` or `Vulkan`.

## Current state

- one active renderer selected through `engine/config/app.json`
- ECS-based scene and object rendering
- scene entities are described in JSON scene assets and config
- resource-driven DX12 mesh rendering with loaded mesh, material, texture, and shader assets
- JSON scene loading plus a runtime JSON scene snapshot
- async mesh/texture/material preloading with placeholder resources while background loads complete
- shader/resource hot-reload polling, scene/app-config hot reload, and binary `*.wmesh` / `*.wtex` caches
- Vulkan still keeps the primitive fallback path

## Renderer

The active renderer is selected in `engine/config/app.json` with:

```json
"activeRenderer": "dx12"
```

Change it to `"vulkan"` to switch backend. The config is read on launch, so you do not need to rebuild just to change the renderer.

## ECS

The ECS layer is located in `engine/ecs`.

### Core

- `Entity`
- `Component`
- `World`
- `SystemPipeline`

### Main components

- `TransformComponent`
- `TagComponent`
- `MeshRendererComponent`
- `MaterialComponent`
- `VelocityComponent`
- `BoundsBounceComponent`

### Main systems

- `MotionSystem`
- `BoundsBounceSystem`
- `RenderSystem`

## Scene config

The ECS demo scene is usually loaded from `ecsDemo.sceneFile` in `engine/config/app.json`.
The default scene is `engine/assets/scenes/pz3_demo_scene.json`.

Each entity can define:

- `tag`
- `meshPath`
- `texturePath`
- `shaderPath`
- `materialPath`
- `color`
- `visible`
- `bounce`
- `position`
- `rotation`
- `scale`
- `linearVelocity`
- `angularVelocity`

`color` is treated as material tint for the entity. The engine writes a runtime
snapshot to `assets/scenes/pz3_runtime_snapshot.json` in the build output.

## PZ3 resource pipeline

WhispEngine now has a small centralized resource path for Practical Work #3:

- `ResourceManager` lives in `engine/resources`, is owned by `Application`, and provides typed `Load<T>()`, `LoadAsync<T>()`, `Get<T>()`, `Reload<T>()`, and fallback caching for `MeshResource`, `TextureResource`, `ShaderResource`, and `MaterialResource`.
- `MeshLoader` uses Assimp to load CPU mesh data with positions, normals, UVs, indices, and submesh metadata.
- `MeshLoader` also writes/reads a small Whisp binary CPU mesh cache (`*.wmesh`) next to the runtime asset after the first import.
- `TextureLoader` uses `stb_image` for common images and a lightweight DDS DXT5 path for the demo texture, normalizing uploads to RGBA8 `TextureData`, and writes/reads binary texture caches (`*.wtex`).
- `ShaderLoader` loads shader source files; DX12 currently creates the textured shader pipeline from HLSL source.
- `MaterialLoader` reads JSON material files with `shaderPath`, `texturePath`, and `baseColor`.
- `IRenderAdapter` exposes opaque render handles for meshes, textures, and shaders. DX12 implements upload, bind, draw, and destroy for the resource-driven path.
- `MeshRendererComponent` owns geometry assignment, while `MaterialComponent` owns the material resource reference plus optional per-entity tint and overrides. `RenderSystem` receives `ResourceManager` by explicit injection, uploads GPU resources once, reuses the handles, and swaps placeholder GPU data to final assets after async completion.
- `ResourceManager::WatchForHotReload()` / `PollHotReload()` reload changed resources, and `Application` hot-reloads JSON scene/config files. `RenderSystem` tracks resource versions and recreates GPU handles when reloaded CPU resources change.

Resource-driven rendering is implemented for DX12 only. Vulkan still keeps the primitive ECS path.

Example resource-driven entity config:

```json
{
  "tag": "AfricanHead_Left",
  "meshPath": "models/african_head.obj",
  "materialPath": "materials/african_head.material.json",
  "color": [1.0, 1.0, 1.0, 1.0],
  "position": [-0.6, -0.1, 0.0],
  "rotation": [0.0, 2.72, 0.0],
  "scale": [0.32, 0.32, 0.32],
  "bounce": false
}
```

## Controls

- `ENTER` in `Menu` switches to `Gameplay`
- `ESC` in `Gameplay` switches back to `Menu`
- `SPACE` spawns a new ECS entity
- `BACKSPACE` destroys the last ECS entity

## Notes

- ECS objects are created during application initialization.
- Scene assets can be edited during runtime: JSON scene changes rebuild the ECS demo scene, and shader/resource edits are hot-reloaded without restarting the application.
- Rendering goes through `RenderSystem` and the existing `RenderAdapter`.
- DX12 is the full PZ3 resource-driven path; Vulkan remains the primitive fallback backend.
