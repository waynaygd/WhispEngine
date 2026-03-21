# WhispEngine

The project currently opens a single window with the selected render backend (`DX12` or `Vulkan`) and includes a basic ECS integration bound to `GameplayState`.

## Renderer

The active renderer is selected in `engine/config/app.json` with the `activeRenderer` field.

## ECS

The ECS layer lives in `engine/ecs` and currently contains:

- `World` with entity lifecycle based on `index + generation`
- component storage with `Add/Get/Has/Remove`
- update systems through `SystemPipeline`
- rendering of ECS objects through a bridge into the existing renderer

### Components

- `TransformComponent`
- `VelocityComponent`
- `TriangleRenderComponent`
- `BoundsBounceComponent`

### Systems

- `MotionSystem`
- `BoundsBounceSystem`

## Gameplay controls

- `ENTER` in `Menu` switches to `Gameplay`
- `ESC` in `Gameplay` switches back to `Menu`
- `SPACE` spawns a new ECS entity
- `BACKSPACE` destroys the last ECS entity

## ECS scene config

`engine/config/app.json` contains an `ecsDemo` section:

- `logSnapshots` enables or disables periodic ECS snapshot logs
- `initialEntities` defines the initial ECS objects for `Gameplay`

Each `initialEntities` item supports:

- `x`, `y`
- `scale`
- `angle`
- `vx`, `vy`
- `angularVelocity`

This allows changing the starting ECS scene without editing code.
