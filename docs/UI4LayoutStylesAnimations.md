# UI.4 — Layout Containers, Shared Styles and UI Animations

UI.4 extends the existing UI Document, Widget Designer, per-Canvas RuntimeUI state and DX12 UI pass. It adds no renderer, render targets, third-party libraries, event bus or threaded animation system. Existing version-1 UI documents retain manual layout and instantaneous Button states by default. Scene/project/prefab formats and the original Room/UI.3 demo are unchanged.

## Layout

A Panel's `layout` is `0` Manual, `1` Vertical, `2` Horizontal or `3` Stack. Example:

```json
{"id":"Menu","parent":"Root","type":"Panel",
 "position":[40,40],"size":[520,300],"layout":1,
 "padding":[60,40,60,40],"spacing":20,"alignment":[0.5,0],
 "stretchChildren":false,"autoSize":[false,false],"reverse":false}
```

Padding order is **left, top, right, bottom**. Alignment is an X/Y fraction from 0 (start) to 1 (end); 0.5 centers. Children flow in **document vector order**, independently of local Z. Z still controls drawing/picking priority. `reverse` reverses flow, retaining Z. Hidden children reserve no space. `ignoreLayout:true` excludes a child from both flow and content measurement and uses its normal anchors/pivot/position.

For participating children, the container controls the resulting position/rectangle. Anchors and position remain authored and are restored when the parent returns to Manual or the child enables Ignore Parent Layout. Their nonnegative `size` supplies fixed content dimensions; the Inspector exposes Content Fixed Size separately from managed transform controls. Negative legacy Size Delta remains legal in manual stretch layouts and contributes zero preferred size in automatic flow. `stretch:[true,false]` requests child width stretch; the second component requests height stretch. Vertical/Horizontal divide remaining main-axis space equally among children stretching on that axis. `stretchChildren` stretches the cross axis; Stack stretches both. Fixed children may overflow when space is insufficient; flexible sizes clamp to zero, start alignment wins over negative alignment offsets, and existing Clip Children controls whether overflow is drawn/hit.

`autoSize:[true,true]` measures a container from participating children plus spacing/padding. Text/Image/Button use fixed preferred size; there is no rich text/content measurement. Auto Size requires fixed anchors on that axis. Auto-size plus a dependent child stretch is rejected rather than iterating constraints. Manual mode ignores stored automatic-layout flags without erasing them.

`ComputeLayout` measures bottom-up once, arranges parent before children once, and builds deterministic rectangles. It shares these rectangles with runtime rendering, hit testing and Designer selection/handles. Animation transforms apply after flow, so they do not feed back into content measurement. Nesting retains the existing 64-level validation limit. Aggregate animation scale is capped at 10000 to avoid overflow through deeply nested animated parents. Negative/NaN padding, invalid spacing/alignment, invalid sizes and cycles are diagnosed by document validation. Empty containers are supported.

Designer exposes layout mode, spacing, padding, alignment, reverse, Auto Size, Stretch Children, per-child stretch and Ignore Parent Layout. Managed children cannot be dragged/resized or silently changed with anchor/pivot presets. Ignore Parent Layout restores those controls. Container drag/resize and reparent recalculate layout. Existing document transactions handle Undo/Redo, drag cancellation with Escape, dirty drafts and Save UI. Canvas edits stop animation preview before modifying authored transforms.

## Shared styles

Styles use `.uistyle.json` files in the active Project Asset Root:

```json
{
 "version":1,
 "styles":{
  "PrimaryButton":{
   "type":"Button",
   "properties":{
    "color":[0.12,0.24,0.38,1],
    "hoverColor":[0.3,0.55,0.82,1],
    "pressedColor":[0.07,0.14,0.25,1],
    "disabledColor":[0.18,0.18,0.18,0.7],
    "transitionDuration":0.12
   }
  }
 }
}
```

A widget references `styleAsset` and `styleId`. `styleOverrides` is a list of explicitly local property names, for example `["color"]`. Canonical normal color/tint is `color`, matching existing widgets. Supported properties: color, opacity, size; Panel padding/spacing; Text fontSize/textAlignment; Button hoverColor/pressedColor/disabledColor/transitionDuration. Types must match exactly. Colors are finite RGBA in 0…1. No CSS, inheritance graph, custom font system or rounded/nine-slice rendering is introduced.

Priority is **type defaults/authored fallback → shared style → explicit widget overrides → Lua runtime overrides → temporary animation layer**. Assigning a style preserves authored fallback values. Inspector edits to styled properties mark explicit overrides; Reset to Style removes the marker. Clearing/missing/invalid/incompatible styles restores authored fallback, retaining runtime overrides. Source Style Assets are immutable to runtime code.

Style sheets share ResourceManager's existing UI resource family (`UIResource::styles`, separate cache keys from `UIResource::document`) and hot reload/dirty protection. Each Canvas/preview retains resource handles, cache epoch and revisions. It resolves mappings/values on document/style revision changes, not every draw. Removing a style causes fallback; a missing file created later is discovered by the existing resource watch. Scene Validation checks missing assets/IDs, type compatibility and values without running Lua. Runtime draw diagnostics include missing styles.

In Widget Inspector:

1. Select a Button and choose **New Style Asset** to create and assign a safe `ui/Style-….uistyle.json`.
2. Copy its Style Asset/Style ID into another Button and choose **Apply Style**.
3. Expand **Edit Shared Style**, change hoverColor and observe all assigned buttons in Designer.
4. Use **Save Style** independently of **Save UI**. Both have Undo/Redo; a drag commits one transaction. Style IDs can be cloned with Create Style ID from Current.

Controls above the style section show authored fallback values; the canvas shows resolved shared values. Explicit overrides are listed with Reset to Style buttons. Dirty Style drafts defer resource hot reload. Save compares the current disk content with the saved baseline and refuses to overwrite external changes; Reload Style / Discard Draft explicitly accepts the external version. New Style Asset writes its initial file; undoing its assignment does not delete that file. UI and Style histories are independent. Project Save includes dirty UI/Style drafts; Discard clears them on transition. Stop Play before saving these authoring drafts.

## Animation clips and Lua

Clips live in the UI document's optional `animations` array:

```json
{"id":"MenuFadeIn","widget":"Menu","property":"opacity",
 "from":0,"to":1,"duration":0.25,"delay":0,"speed":1,
 "easing":"easeOut","loop":false,"pingPong":false}
```

Properties: opacity, progress, offsetX, offsetY, scaleX, scaleY and color. Position offsets use logical reference-resolution units; scale is applied about the widget pivot and inherited by descendants. Color uses four-channel from/to arrays. Opacity/progress/colors stay in 0…1, scale in 0…100, offsets in ±100000. Duration is 0.0001…3600 seconds, delay 0…3600, effective speed 0.001…100. Easing: linear, easeIn (quadratic), easeOut (quadratic), easeInOut (smoothstep). Ping-pong requires Loop. A document supports at most 512 clips.

```lua
local menu = ui.get(self.entity)
menu:set_visible("Menu", true)
ui.set_input_mode("UIOnly")
menu:play_animation("MenuFadeIn")
menu:play_animation("MenuSlide", {restart=true, speed=1.0})
if menu:is_animation_playing("MenuFadeIn") then
    -- Playback is local to this Canvas.
end
menu:stop_animation("MenuSlide")
```

One track owns each Widget ID/property pair. Starting a different clip replaces the previous owner for that pair; different properties run together. Restart uses the declared from value. `restart=false` keeps an already-running identical clip (including its speed); otherwise it starts normally. Delay holds from. Non-looping completion holds the final visual value until Stop/replacement, and emits exactly one `animation_complete`. Stop removes that property's animation layer and restores its base result. Getters from UI.3 return the base runtime value; animation is a separate visual layer. Offsets/scales never accumulate into authored transforms.

```lua
on_ui_event = function(self, event)
    if event.type == "animation_complete"
       and event.animation == "MenuFadeOut" then
        self.hud:set_visible("Menu", false)
        ui.set_input_mode("GameOnly")
    end
end
```

Completion uses the existing bounded UI/Lua queue and Canvas-owner routing from UI.3. Gameplay Event Inspector payload includes Animation ID. UI delta advances once per Application frame before hit testing/dispatch, including Editor Pause, with the existing 0.1-second delta clamp. It does not advance simulation or introduce game pause. Rendering and hit testing see the same animated rectangles/opacity/clip. Stop/session change/entity deletion clears playback and events; document reload resets animation tracks/layers and invalidates handles, while compatible Lua overrides retain UI.3 semantics. Reacquire handles after reload.

Buttons use the same UI clock/visual layer for finite linear color transitions. `transitionDuration` applies to normal/hover/pressed/disabled transitions; zero retains instantaneous UI.3 behavior. Interrupted transitions start from the current interpolated color. A color clip temporarily takes priority over state transitions until stopped. State animation does not change pointer ownership or Click logic.

Widget Inspector provides New Animation, stable ID rename, target, property, from/to, timing, easing, loop/ping-pong and Play/Stop Preview. Preview uses the same player/layout/render output in Edit and discards completion events without calling Lua. Clip/parameter edits use the existing UI document Undo/Redo. Widget rename updates clip targets; deleting a subtree deletes its clips and required-ID references. Invalid IDs, duplicates, missing targets and incompatible properties produce validation errors.

## Demo

Open `scenes/interactive_room_ui4.json`. Its separate document, style sheet and Lua behavior are:

- `ui/InteractiveRoomUI4.ui.json`
- `ui/RoomUI4.uistyle.json`
- `scripts/interactive_room_ui4.lua`

The HUD uses Horizontal Layout, SanityBar updated from Lua and an appearing InteractPrompt. M opens a Vertical Layout menu with shared-style Resume/Close Menu Buttons. Fade In and slide run together; hover color transitions are visible. Click starts Fade Out; completion hides the menu and restores the existing FPS capture service. Repeat M to reopen. The script handles stale handles after UI reload. This is an interface demonstration, not a new health/inventory/pause system. Original Interactive Room and UI.3 files are unchanged. Copy sample dependencies into a project's Asset Root when using a standalone project.

## Verification and performance

UI4Regression exercises CPU layout/validation and a native GLFW/Win32/DX12 editor session, including actual pixel output and completion callbacks. Existing SyntheticEditorInput only isolates ImGui from the unrelated physical desktop cursor; game pointer/key messages still go through GLFW. Performance samples use the same Release/DX12 build, debug layer, 1280×720 render target, UIOnly with cursor released and room/camera setup, 8 warm frames and 32 samples per state. UI.3 versus UI.4 are different UI documents in the current implementation, not a controlled pre-change engine comparison. The active phase slows MenuFadeIn to keep it playing for the whole sample. GPU UI times come from existing DX12 timestamps, not CPU command-recording duration. No compilation/readback runs inside sampled frames.

Existing CPU Build/Layout includes the renderer's Sync/layout/build, but excludes pre-Lua Sync/hit testing and the UI animation clock. Layout remains O(n log n) plus bounded-depth validation; static style resolution is revision driven. Geometry still uses the existing per-frame UI builder; no independent static-geometry cache is introduced. Runtime/preview documents copy at creation/source change, not each setter/animation frame. No per-widget filesystem path checks, texture recreation, render target creation or fence waits are added by the style/animation paths. Cold document/style loading uses the existing synchronous UI resource loader.

Final validation:

- WhispEngine built successfully in `out/build/shadows-dx12` and normal `out/build/x64-release` (DX12/Vulkan) Release configurations.
- **UI4Regression: 194 checks passed**, actual DX12 green/blue styled Button pixels, finite hover interpolation/endpoints, native pointer hits on animated rectangles, Pause completion dispatch, native Room M/Resume/Close Menu/FPS movement/Tab workflow, style loading/deletion/late creation/hot reload, override priority, independent Canvas players, stale handles, reload/reset, layout/clip validation, Designer Undo/Redo/Save, external Style conflict protection, and zero DX12 validation errors.
- **InteractiveUIRegression: 175 checks passed** in the final combined UI.4/UI.3 run. The initial aggregate run transiently failed the existing native-hover pixel check; an isolated repeat and the final combined run passed without changing that test or input routing.
- RuntimeUIRegression, WidgetDesignerRegression, SceneValidationRegression, ProjectSystemRegression, ProjectSystemDx12Regression, GameplayEventsRegression, GameplayEventsDx12Regression and LuaGameplayApiRegression passed. These cover existing UI.1/UI.2 authoring/pixel output, project teardown/drafts and legacy Lua/event behavior.
- `git diff --check` passed. No commit or branch switch was made; LR №3 files were not changed.

Measured results from the final UI4Regression run:

| State | CPU Build/Layout median ms | CPU p95 ms | GPU UI median ms |
| --- | ---: | ---: | ---: |
| UI.3 demo | 0.0988 | 0.1143 | 0.001024 |
| UI.4, menu closed | 0.0800 | 0.1084 | 0.001024 |
| UI.4, menu open | 0.0861 | 0.2061 | 0.004096 |
| UI.4, animation active | 0.0942 | 0.1694 | 0.005120 |

All four rows recorded the same camera `(-1.85, 1.65, -7.3)`, yaw/pitch `0/0`, input mode UIOnly and capture=false. GPU values are timestamp measurements with microsecond-scale quantization; this short sample does not establish engine-wide improvements or regressions. Original UI.3 timings at a different resolution are not used as a pre-change comparison.

Evidence: `out/build/shadows-dx12/ui4-final-ui.log`, `ui4-related.log`, `ui4-ui3-repeat.log`, `ui4-regression.log`; `out/build/shadows-dx12/diagnostics/ui4/cost.json` and `menu-open.ppm`. The separate report log retains the initial hover failure for transparency. Source tests/UI.3 assets were not altered to make repeats pass.


## Implementation files

- `engine/ui/UIDocument.{h,cpp}`: compatible schema, clip/style parsing and validation.
- `engine/ui/UILayout.cpp`: shared measure/arrange and animated rectangles.
- `engine/ui/UIEffects.cpp`, `RuntimeUI.{h,cpp}`: style cache, per-instance tracks, Button transitions and preview.
- `engine/resources/ResourceManager.{h,cpp}`: UI-style byte accounting and missing/deleted UI watch handling.
- `engine/ecs/systems/RenderSystem.cpp`: resolved Designer preview through the existing renderer.
- `engine/core/Application.{h,cpp}`, `engine/scripting/LuaRuntime.cpp`: UI clock, bindings and completion queue.
- `engine/editor/EditorUI4.cpp`, `EditorRuntimeUI.cpp`, `EditorWidgetDesigner.cpp`, `WidgetDesignerTools.cpp`, `SceneValidation.cpp`, `EditorLayer.h`, `EditorProject.cpp`: authoring, managed transforms, references, transactions and project draft protection.
- `CMakeLists.txt`, `engine/CMakeLists.txt`, `tests/UI4Regression.cpp`: build/assets and target regression.
- The four separate demo assets listed above and this report.

## Limits

There is no Flexbox/Grid, text auto-measurement, min/max constraint solver, theme inheritance, corner radius, timeline editor, World Space UI or navigation extension. Manual and automatic sizing rules are explicit rather than iteratively solving ambiguous dependencies. Button transitions use one duration for all state changes. Completed clips hold endpoints; replacement starts from declared from, with no optional current-value blending mode. Hot document reload resets playback. Physical high-DPI hardware interaction is not claimed; coordinate mapping, reference scaling and shared animated hit/render bounds are exercised in tests. The previously documented native desktop cursor-warp limitation is not addressed by changing editor free-fly input in this feature.
