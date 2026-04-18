# WhispEngine

WhispEngine now runs in a single window and uses the render backend selected in config: `DX12` or `Vulkan`.

## Current state

- one active renderer selected through `engine/config/app.json`
- ECS-based scene and object rendering
- support for `line`, `triangle`, `quad`, `cube`
- object color comes from `MeshRendererComponent`
- scene entities are described in config

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
- `VelocityComponent`
- `BoundsBounceComponent`

### Main systems

- `MotionSystem`
- `BoundsBounceSystem`
- `RenderSystem`

## Scene config

The ECS demo scene is described in `engine/config/app.json` inside `ecsDemo.initialEntities`.

Each entity can define:

- `tag`
- `primitive`
- `color`
- `visible`
- `bounce`
- `material`
- `texture`
- `position`
- `rotation`
- `scale`
- `linearVelocity`
- `angularVelocity`

Supported primitive values:

- `line`
- `triangle`
- `quad`
- `cube`

Note: the current `cube` is rendered as a wireframe cube.

## Controls

- `ENTER` in `Menu` switches to `Gameplay`
- `ESC` in `Gameplay` switches back to `Menu`
- `SPACE` spawns a new ECS entity
- `BACKSPACE` destroys the last ECS entity

## Notes

- ECS objects are created during application initialization.
- Rendering goes through `RenderSystem` and the existing `RenderAdapter`.
- DX12 and Vulkan both support ECS primitive rendering in the current version.
