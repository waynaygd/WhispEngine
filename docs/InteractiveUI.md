# Interactive Runtime UI (UI.3)

Runtime Canvas now supports Lua overrides and interactive Buttons. It reuses UI.1 layout, clipping, font atlas, texture cache and DX12 submission, and UI.2 document transactions. ImGui remains the editor interface; it does not draw game widgets.

## Author a menu

Create a Canvas, open Widget Designer, and use **Add → Button**. A Button includes a child Text label. Its properties expose Enabled, Normal/Hover/Pressed/Disabled colors (with color swatches), and the existing anchors, pivot, size, local Z, visibility, opacity and clipping controls. Button can parent children like Panel. Undo/Redo and Save UI use the existing document history. Ordinary Edit clicks select widgets without sending gameplay events.

Attach a LuaScript to the **same Entity as the Canvas**. Enable its Input permission if it reads named input actions. Canvas updates/events do not grant camera or physics permissions.

```lua
return {
    fields = {},
    on_start = function(self)
        self.hud = ui.get(self.entity)
        self.hud:set_text("InteractPrompt", "Нажмите E")
        self.hud:set_progress("SanityBar", 0.65)
        self.hud:set_visible("Menu", true)
        ui.set_input_mode("UIOnly")
    end,
    on_ui_event = function(self, event)
        if event.widget == "Resume" and event.type == "click" then
            self.hud = ui.get(self.entity)
            self.hud:set_visible("Menu", false)
            ui.set_input_mode("GameOnly")
        end
    end
}
```

`ui.get("PlayerHUD")` also accepts a unique document name or resource key (`ui/PlayerHUD.ui.json`). Multiple matching Canvas owners raise an error; use the Entity overload for independent instances.

## API and runtime state

| Setter | Widget | Value | Getter |
| --- | --- | --- | --- |
| `set_text(id, value)` | Text | valid UTF-8, at most 16384 bytes | `get_text(id)` |
| `set_progress(id, value)` | ProgressBar | finite 0…1 | `get_progress(id)` |
| `set_image(id, key)` | Image | safe Texture Resource Key | `get_image(id)` |
| `set_visible(id, value)` | any | boolean | `get_visible(id)` |
| `set_opacity(id, value)` | any | finite 0…1 | `get_opacity(id)` |
| `set_color(id, {r,g,b,a})` | any | four finite 0…1 channels | `get_color(id)` returns four numbers |
| `set_enabled(id, value)` | Button | boolean | `get_enabled(id)` |

Color changes the normal color/tint; Button state colors remain authored properties. Missing IDs, incompatible types, invalid ranges/UTF-8/keys and stale handles follow the existing Lua error policy. Use `handle:valid()` before reusing a handle across document reloads. A safe key can still reference an absent texture: normal asynchronous loading/diagnostics and the magenta fallback apply.

Each Canvas owns `RuntimeUI::Instance::state` and a field override mask. Setters update one widget, without copying the document, saving a file, or modifying the Editor World, Scene or Prefab. UI reload merges only explicitly overridden fields with the same Widget ID **and type**; other fields come from the new document. Missing/incompatible widgets lose their overrides. Stop clears all overrides; the next Play starts from authored data.

Canvas userdata is a value token: World-qualified Entity/generation, runtime session, instance serial, resource key and revision. It contains no widget, ImGui or DX12 pointer. Stop, owner deletion, index reuse, document replacement/reload and scene/project lifecycle invalidate old tokens. Reacquire after reload:

```lua
if not self.hud:valid() then self.hud = ui.get(self.entity) end
```

## Input and events

`ui.set_input_mode(mode)` requires Play. `ui.get_input_mode()` returns:

| Mode | Behavior |
| --- | --- |
| `GameOnly` | Existing gameplay routing; UI does not hit-test. Switching back requests capture through the existing FPS capture service, subject to focus/Pause/ImGui guards. |
| `UIOnly` | Releases the actual game capture, blocks gameplay input, accepts UI clicks. |
| `GameAndUI` | Releases capture. Interactive UI owns a left-button gesture; other input follows existing gameplay routing. An FPS controller can capture an unconsumed click outside UI. |

There is one cursor capture mechanism. Gesture ownership is only mouse-action consumption, not cursor capture. A press started on UI remains consumed until release even if the widget is hidden/deleted/disabled. Moving outside cancels Click, while Pointer Up still reaches a surviving pressed widget. Hiding, disabling, clipping away or replacing a pressed widget cancels activation. Noninteractive Panel/Text/Image pass through to Buttons. Drawing and hit-testing share hierarchy traversal, visibility, inherited opacity, layout/clip rectangles, local Z and Canvas sort order. Input maps window pointer events into actual Viewport render-target pixels, including resize and F11. Editor widgets/popups retain input protection. Play content clicks do not drag the editor window.

Events are `pointer_enter`, `pointer_leave`, `pointer_down`, `pointer_up`, `click`. They go to the Canvas owner's `on_ui_event(self,event)` through the existing bounded Lua event queue, on the main thread before normal Update. UI dispatch also runs while Editor Pause stops simulation; ordinary gameplay events remain queued. A missing/disabled receiver is safely skipped. Rendering callbacks never execute Lua.

Event fields: `type`, `widget`, `canvas` (safe handle), `canvas_key`, `source`, `owner`, `session` (Gameplay Event session). Gameplay Event Inspector shows the existing source/receiver/status fields plus Canvas/Widget. Its bounded history follows existing capture/clear/Stop policies; executable pending events are cleared on Stop.

Tab releases capture, Escape stops Play, F11 only changes Viewport layout. Clean Play hides editor controls while the actual mouse capture is active and retains game HUD. Pause releases capture; UI remains usable. Script failure/removal/hot reload releases input mode ownership safely. Stop resets mode and capture through existing lifecycle services.

## Demo

Open `scenes/interactive_room_interactive_ui.json` from the built-in assets. It is a separate copy of Interactive Room with `ui/InteractiveRoomUI.ui.json` and `scripts/interactive_room_ui.lua`. Play, click to control the FPS player, then press **M**. The menu releases the cursor; **Resume** and **Close Menu** call Lua, hide the menu and restore FPS capture. SanityBar animates and InteractPrompt changes text/visibility. This is a UI demonstration, not a new health system or full gameplay pause. Existing Room assets/scripts/prefabs remain unchanged. For a project, copy the sample's dependencies into its Asset Root.

## Validation and cost

UI document validation (also used by Scene Validation) accepts Button, checks its colors and interactive values, and checks optional `requiredWidgets` IDs. Canvas references and Image keys use existing validation. `requiredWidgets` is optional in version 1; Designer exposes a Required Widget ID checkbox and updates it on rename. Static validation never executes Lua.

Documents retain ResourceManager handles and versions. Image resolution retains the P3.1 texture cache (including pending/failed resource owners); steady draws do not revalidate filesystem paths or recreate textures. Setters add no GPU synchronization. Existing CPU layout/geometry generation is retained rather than introducing a new static geometry cache in this feature.

Implementation files:

- `engine/ui/UIDocument.{h,cpp}`, `RuntimeUI.{h,cpp}` — Button schema, shared traversal, independent overrides, handles, pointer gestures.
- `engine/scripting/LuaRuntime.{h,cpp}`, `GameplayServices.h`, `GameplayEventHistory.h` — bindings, input mode ownership, existing event dispatch/history.
- `engine/core/Application.{h,cpp}`, `engine/platform/GlfwWindow.{h,cpp}`, `InputManager.h` — native pointer coordinates, action consumption and lifecycle routing.
- `engine/ecs/systems/RenderSystem.{h,cpp}` — runtime access and retained pending Image resource owners.
- `engine/editor/EditorLayer.cpp`, `EditorRuntimeUI.cpp`, `EditorWidgetDesigner.cpp`, `WidgetDesignerTools.cpp`, `EditorGameplayEvents.cpp` — authoring, Play pointer ownership, event details.
- `engine/assets/ui/InteractiveRoomUI.ui.json`, `engine/assets/scripts/interactive_room_ui.lua`, `engine/assets/scenes/interactive_room_interactive_ui.json` — separate demo.
- `tests/InteractiveUIRegression.cpp`, `tests/RuntimeUIRegression.cpp`, `CMakeLists.txt` — native integration, supported-type baseline update, assets/test target.
- `docs/InteractiveUI.md`, `docs/RuntimeUI.md` — usage and updated links.

Short cost sample from the final InteractiveUIRegression run: Release/DX12 with debug layer, the separate Interactive Room demo, fixed 800×600 render target, 32 steady frames per state, no parallel compilation during sampling. Existing `runtimeUILayoutMs` / `runtimeUISubmitMs` counters:

| State | Build/layout median CPU ms | p95 CPU ms | Submit median CPU ms |
| --- | ---: | ---: | ---: |
| Animated HUD, menu hidden | 0.0616 | 0.2033 | 0.2237 |
| Menu visible | 0.0706 | 0.0881 | 0.2905 |

These are CPU measurements, not GPU times. Layout includes the renderer's Sync/Build, but excludes Application's pre-Lua Sync/hit-test/event dispatch. Readbacks are outside the sampled frames. A comparable pre-UI.3 build of this same scene/configuration was not measured; the previous approximate 1 ms figure is not a controlled before/after comparison. Output: `out/build/shadows-dx12/diagnostics/interactive-ui/cost.json`. No scalability benchmark was run.

Verification:

- WhispEngine Release built in `out/build/shadows-dx12` and the normal DX12/Vulkan `out/build/x64-release` configuration.
- InteractiveUIRegression: **175 checks**, native GLFW/Win32 pointer and key messages plus actual DX12 normal/hover/pressed/disabled/Image pixels, zero DX12 validation errors. It covers per-instance overrides, stale/reused handles, document and Lua reload, clipping and sibling/Canvas order, whole left-button consumption, disabled/hidden widgets, Pause dispatch, Designer Undo/Redo/Save, F11, demo M/Resume/Close Menu, real FPS movement, Tab/recapture/Escape, repeated Play and scene switch.
- RuntimeUIRegression, WidgetDesignerRegression, SceneValidationRegression, ProjectSystemRegression and ProjectSystemDx12Regression passed. The project regression exercises project/session teardown.
- GameplayEventsRegression, LuaGameplayApiRegression and LuaGameplayControllerRegression CPU cases passed. GameplayEventsDx12Regression and LuaGameplayApiDx12Regression also passed.
- InteractiveRoomRegression initially failed its existing capture assertion during the aggregate run. Two separate runs of the same engine passed **198 checks** each, including the original walkthrough. No engine workaround was added for that transient capture failure; the test depends on native window focus. Its existing project-open portion uses the owned build-directory `InteractiveRoom.whispproject` / copied `Scenes/InteractiveRoom.json` fixture. The source descriptor has a missing startup path and was not changed.

- LuaGameplayControllerDx12Regression stopped at its existing **Edit RMB free-fly** assertion. A diagnostic rerun confirmed that this desktop rejects its cursor warp: requested `(440,386.5)`, actual GLFW position `(128,158)`, outside Viewport bounds `(72,203)…(808,570)`, with focus=true, hover=true and editor blocking=false. The free-fly production path was not changed. This test is **not passed** in this environment; the new UI regression's actual native FPS/menu/Tab/F11/Escape path and the original Room walkthrough passed. Diagnostic-only test edits were removed.

Logs: `out/build/shadows-dx12/ui3-ctest.log`, `ui3-native-ctest.log`, `ui3-room-repeat.log`, `ui3-controller.log`. Screenshots and cost sample: `diagnostics/interactive-ui/` in that build directory.

Limits: DX12 runtime UI as in UI.1; no gamepad/keyboard navigation, World Space UI, automatic script receiver search or second event bus. Input is sampled per frame using the existing system; a complete down/up between two samples has the same limitation as existing gameplay actions. Native test messages drive GLFW; deterministic ImGui positions isolate tests from the unrelated physical desktop cursor. This is not a physical high-DPI hardware test.
