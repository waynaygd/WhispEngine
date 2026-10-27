# WhispEngine narrative prototype

## Engine audit

The existing `ProjectManager::Prepare/Create`, staged `Application::OpenProject`,
project-relative asset roots and v4 scene serialization support a separate project.
The new project does not require changing the engine's legacy workspace or FPS
player templates. Future ProjectManager-created projects also receive Assets/data.

ECS Camera already supports orthographic projection, hierarchy, selection and a
shared camera state across DX12/Vulkan. Logical camera pose differs deliberately
from interpolated rendered pose. The extension derives rays from that same basis.
Input previously exposed actions and relative mouse motion; the new host capability
provides normalized viewport coordinates and aspect, respecting gameplay ownership
and Runtime UI pointer consumption. No OS screen coordinates are passed to Lua.

`Physics.raycast`, World Transform setters and owner-only Character movement already
exist. Navigation builds a bounded flat A* grid from the existing collider convention
(world-aligned, already-scaled extents). It excludes triggers, dynamic bodies and
the ground by foot height. It keeps radius and half-cell clearance around obstacles.

Lua Behaviours, Script.require, typed Entity fields and the existing Lua Script
Editor remain the script workflow. Dialogue is a project module, with graph content
in DataAssets. No second Lua VM or dialogue-specific engine component is introduced.

ResourceManager already supplies caches, immutable resource snapshots, replacement
versions, last-good reload handling and filesystem watches. DataResource joins
those mechanisms, including async loading, ClearAll and memory statistics.
There is one common resource payload for DataAsset and DataTable; `kind` selects
their validated authoring form. There is no UE-style reflection system.

AssetCatalog registers both suffixes in the data folder. Asset Browser creates
resources; selecting them opens typed controls in Inspector. Object/array controls
support dialogue graphs, and DataTable uses an editable table with add/delete.
Schema JSON can be edited there; Apply preserves compatible values and adds defaults.
Save validates the complete document and uses SceneSerializer's atomic replacement.
Unsaved data participates in the existing project Save/Discard/Cancel workflow.
Data Inspector currently has one draft; save/discard it before editing another asset.

Existing v2 Prefab serialization and v3 parentPrefab inheritance are used unchanged.
The example includes `witness.prefab.json` and an inherited radio variant. The
authored scene keeps its own three NPC instances, so editing these demonstration
prefabs does not silently rewrite the scene.

Widget Designer documents, UICanvas, RuntimeUI handles, instance overrides and
`on_ui_event` implement all game UI. The player owns the Canvas directly, allowing
its own Behaviour to receive UI events and drive its own Character component.
Buttons use the existing click events, not a custom renderer or polling GUI.

Play clones the authored World; Stop restores it and retires Lua/UI handles.
The prototype uses one level. Existing deferred `World.load_scene` travel is not
needed and is unchanged. Game progress is session state and resets on new Play.

## Authoring format

DataAsset (`data/*.data.json`):

```json
{
  "version": 1,
  "kind": "DataAsset",
  "type": "NPCDefinition",
  "schema": {
    "type": "Object",
    "fields": {
      "name": { "type": "String" },
      "active": { "type": "Boolean" },
      "position": { "type": "Vec3" },
      "role": { "type": "Enum", "values": ["witness", "detective"] }
    }
  },
  "value": {
    "name": "Марта", "active": true,
    "position": [0, 0, 0], "role": "witness"
  }
}
```

Supported schema types are String, Number (optional min/max), Boolean, Vec3,
Color (four channels in [0,1]), Enum, Resource, Object and Array. Resource values
are project asset keys; nonempty references must resolve to an existing regular
file, with optional suffix constraint. `optional: true` permits an empty reference.
Object has `fields`; Array has `items`. A schema may supply a `default` value.
All object fields are required; unknown fields are rejected.

DataTable (`data/*.table.json`) uses `kind: "DataTable"`, the same Object schema,
and `rows: [...]` instead of value. Its schema must include `id: {"type":"String"}`.
Every row has that schema and a unique nonempty ID. Examples: Skills, Items,
SkillChecks. NPCDefinition, DialogueGraph, QuestDefinition and CharacterStats are
DataAssets. Schemas are inline and explicit; shared schema registries/migrations
are outside this implementation.

Limits: 4 MiB input, 24 schema/value nesting levels, 64 fields per Object, 2,048
rows/array elements and 65,536 UTF-8 bytes per String. Empty tables still validate
their schema. Diagnostics identify invalid fields, types, IDs and references.

CSV import/export uses a sibling `<key>.csv` file selected by the existing asset
key, strict columns and escaped quoted multiline UTF-8 cells. Non-string values
use JSON text in cells, preserving Number/Boolean/vector types. Import creates a
validated draft; Save commits it. This is a data interchange format, not a formula
evaluation system; no expressions are executed. CSV uses a 4 MiB bound.

## Lua API

```lua
local npc = Data.load('data/keeper_npc.data.json')
local skill = Data.find('data/skills.table.json', 'logic')
local allSkills = Data.rows('data/skills.table.json')
```

`load` requires DataAsset; `rows`/`find` require DataTable. Missing row returns nil;
invalid files/kinds produce ordinary Lua diagnostics. Tables are independent
value copies: changing them cannot mutate ResourceManager's definitions or disk.
Vec3 becomes the existing native Vec3; Color becomes a four-element Lua table;
Array becomes a one-based table. Resource references remain strings. Explicitly
load the referred DataAsset when needed. Store quest flags, dialogue state and
current stats separately on the Behaviour. Reload updates ResourceManager's
snapshot; existing Lua copies deliberately remain stable until loaded again.

C++ uses `Load<DataResource>`, `Get<DataResource>`, `Reload<DataResource>` and
`WatchForHotReload<DataResource>` on ResourceManager. The immutable JSON document
is `handle->GetData().document`. DataLoader provides ValidateSchema, Validate,
Save, ImportCSV and ExportCSV. Invalid reload retains the last good snapshot and
records the failure through the existing resource state/diagnostics.

```lua
local x,y = Input.mouse_position() -- normalized; unavailable is -1,-1
local ray = Camera.pointer_ray(self.camera) -- nil if unavailable/UI consumes pointer
local explicitRay = Camera.screen_ray(self.camera, .5, .5, 16/9)
local hit = ray and Physics.raycast(ray.origin, ray.direction, 150, self.entity)
Camera.set_orthographic_size(self.camera, 22)
local path = Navigation.find_path(
    Vec3.new(0,0,-5), Vec3.new(4,0,2),
    Vec3.new(-8.5,0,-7.5), Vec3.new(8.5,0,7.5),
    .35, .31, self.entity)
```

Camera operations require the existing Behaviour camera permission. Pointer input
also requires its existing input permission. Explicit screen rays work without
mouse capture. x/y are in [0,1], top-left origin; aspect is width/height.
Orthographic size means full vertical world span. Existing FPS actions, capture,
camera leases and controllers are unchanged.

Navigation returns a one-based array of native Vec3 waypoints, or an empty table
for blocked/unreachable/out-of-bounds destinations. Invalid configuration reports
an error. Cell size: .1–10; actor radius: 0–5; maximum: 16,384 cells. The returned
waypoints retain start.y (pass foot height, not body center). Character movement
still belongs to the actor's own script, through Character.move_world/stop.

## Dialogue/state

Each graph has a stable start ID and array of uniquely identified nodes. Nodes
provide speaker, portrait resource, text and a dynamic list of choices. Choices provide
next ID, required/excluded flags, a flag to set, quest update and optional skill
check ID. SkillChecks rows select the skill, target difficulty and evidence bonus.
The runtime rolls 2d6, selects success/failure node IDs and displays the arithmetic.
Tests use isolated authoring copies to exercise both outcomes deterministically.
Closing a dialogue stops movement and keeps session flags; reopening reconstructs
the graph and evaluates choices from those flags. Cyrillic is UTF-8 throughout.
The journal shows current quest state. The dialogue column retains full conversation entries and player replies in a scrollable history.

## Limits

This is a small primitive-based vertical slice. Navigation is flat and static:
no slopes, stairs, multiple floors, moving-obstacle avoidance or navmesh baking.
There is no savegame, voice acting, combat, character animation or portrait animation.
Game progress resets on Stop. Camera rotation is fixed; zoom uses Z/X or the mouse wheel over the world. Response count is bounded by the existing 2,048-widget document budget, with two ordinary widgets per response. Engine UI
supports its existing resolution scaling; there is no new world-space UI layer.

The regression executable runs CPU gameplay, DX12 application integration and a
Vulkan rendering smoke test. Native actions are deterministic injected inputs;
they exercise the real scheduler/physics/UI event pipeline without depending on
foreground OS cursor focus. Vulkan smoke does not prove every dialogue branch on
that renderer; the renderer-independent gameplay suite covers those branches.

## Gameplay & UI Overhaul 1.0

`ui.measure_text(text, font_size, width)` returns wrapped UTF-8 text and its measured height using the runtime font. `Camera.world_to_screen(camera, world_position)` supports world markers. Runtime canvases support replacing dynamic children and setting scroll, position and opacity. A scrollable Panel must also clip its children; `hitTest` makes only its own rectangle block pointer input. Wheel events over a scrollable panel scroll it; wheel events over the world reach gameplay. Dynamic entries use the existing widgets, events and renderer.

The transcript keeps up to 400 full entries per Play session and uses the existing 2,048-widget budget. Choices are data-driven, with two widgets per choice, and share that budget. The camera follow, composition offset, bounds and zoom range are configurable in the player Behaviour. Movement uses the existing character controller and static navigation grid, with configurable acceleration, braking and turn response.
