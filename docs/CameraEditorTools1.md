# Camera Gizmo & Preview 1.0

## Audit and architecture

CameraComponent already provides validated perspective/orthographic settings;
CameraState/ResolveCameraPose provide the shared world optical basis, including
parent rotations and roll without scale distortion. Light/Fog/PlayerStart gizmos
already project procedural lines/icons into an ImGui overlay with viewport picking.
Camera entities previously had only the ordinary fallback entity icon.

The editor reuses that overlay and selection pipeline. No model asset, MeshRenderer,
temporary Entity, controller, runtime camera selection or physics object is added.
The existing Camera component Inspector, Scene/Prefab documents and preferences
remain the integration points.

## Gizmo and frustum

**Scene View > Display > Camera Gizmos** toggles the procedural wire camera
body, lens, up marker and forward axis. The camera Inspector exposes the same
toggle, **Frustum**, and **Live Preview**. All three are editor preferences, default
on for older preference files; no scene/project settings or layout panels are added.

The body follows the shared optical basis. Its display size is adjusted for editor
distance/FOV/viewport height and bounded for readability. Selected cameras are
yellow; disabled cameras remain visualizable. Small screen-space hit regions around
camera origins select the actual Camera Entity through ordinary viewport selection,
including Ctrl/Shift handling. Gizmos are editor overlays, with x-ray visibility;
they are not depth-tested scene meshes. Transform gizmo ownership takes priority.

The selected frustum uses CameraState projection coefficients. Perspective plane
half-extents are z/projection[0] and z/projection[5]; orthographic extents are
1/projection[0] and 1/projection[5]. `orthographicSize` remains the full vertical
span. Both near/far rectangles and their four connecting edges use world optical
right/up/forward, including parenting and roll. Parent scale affects position but
does not stretch the basis. Existing representable-TRS/shear restrictions apply.

When the selected camera has a visible preview, the frustum uses its exact integer
target aspect; otherwise it uses Scene View aspect. Only the **displayed** far plane
is capped to min(farClip, max(nearClip, 200)). The Inspector explains this cap.
CameraComponent, clip settings and render projection remain unchanged. Extreme valid
FOV/clip values are clipped through the existing robust editor line projection.

Gizmos/frustum are suppressed in Play and Game View. Turning Camera Gizmos off
also suppresses its specialized icon; it does not fall back to a duplicate icon.

## Live preview and renderer scheduling

Select one Camera Entity through Hierarchy or its gizmo. In Inspector, enable
**Live Preview**. The inline 16:9 image uses the available Inspector width (64–1024
pixels), with bounded dimensions and the resulting integer width/height aspect.
It requires an enabled valid Camera with Transform, and a visible preview rectangle
in a Scene/Prefab document. Disabled cameras show an explanation and release the
preview target. Hidden/collapsed/scrolled-out Inspector preview does not render.

DX12 provides one auxiliary target on the primary editor window using the existing viewport target creation,
depth buffer, RTV/DSV/SRV, resource barriers, HDR/postfx and RenderSystem path.
It is separate from both Scene View and Material Preview targets. The shared
RenderSystem receives a temporary resolved CameraState, and its main camera state
is restored afterward. Application's Editor Camera and gameplay selection state
are never overwritten. No Camera.set_active/acquire, Lua or Play is required.

The renderer has per-frame HDR/postfx/lighting scratch resources which are not a
general independent multi-view render graph. To avoid recording two scene passes
that can alias those resources, preview and Scene View are **time multiplexed**:
one preview scene pass every fourth eligible frame; other frames render Scene View.
On the preview frame, Scene View displays its last completed image. Simulation,
input, editor UI and overlays still update each frame. Preview rendering invokes
only RenderSystem, not another simulation/frame-system pass. When preview is hidden,
all frames return to ordinary Scene View rendering and the auxiliary target is freed.

This is the minimal safe single-renderer implementation, not simultaneous multi-view
rendering. At 60 editor frames/second preview is approximately 15 updates/second;
Scene View receives approximately 45 renders/second. No extra full scene pass is
performed per frame. Resizing shared scratch targets can still have a cost.

Preview renders scene geometry, material, lighting/fog/postfx through the same
pipeline, with camera-specific projection and sky basis. Lighting follows the
existing Scene View Lit/Unlit display mode. Grid, collider debug,
placement previews, isolation masking and camera overlays do not enter its image.
Existing authoring-mode Camera Attachments semantics remain: no Play-only visual
attachment override is activated by preview. Runtime UI keeps the existing editor
UI Preview setting; preview does not start gameplay.

## DX12 resources and lifecycle

BeginCameraPreview swaps the auxiliary target into the existing viewport slot,
begins the ordinary target pass and transitions SRV -> render target. End performs
the existing transition back to SRV and restores the Scene View target. ImGui uses
the auxiliary SRV only after recording that pass. Resize reuses the existing fenced
viewport allocation path. Release during recording retires the SRV and color/depth/RTV/DSV resources
through the current frame fence, including ImGui references already recorded that
frame. Outside recording it drains prior GPU work before immediate release. There
is one current auxiliary target; retired allocations remain only until their frame
fences complete, using the existing bounded per-frame retirement queues.

The request stores only Entity values and dimensions, and is rebuilt each UI frame.
Changing selection/World drops the previous image before showing a different camera.
Deletion, disable, deselection, hidden preview, Play and project changes release the
target. Scene/Prefab changes use the existing World/generation identity checks.
Project reset clears requests and source identity; device shutdown releases the
target. Editor restart persists the three display preferences, never Entity handles
or GPU texture IDs. Material Preview keeps its independent existing target/lifecycle.

## Validation and limits

Verified on Windows x64, Release, 2026-10-24:

- MSVC Release `WhispEngine`, the new CameraEditorToolsRegression and related
  targets built successfully. Final production build: `out/camera-editor1-complete-build.log`;
  final stronger Player Prefab regression build: `out/camera-editor1-prefab-test-build.log`.
- Final related CTest run: **37/37 passed**, 165.73 seconds
  (`out/camera-editor1-tests-tests.log`). Includes Camera System/API, Attachments,
  Player Workflow, ProjectSystem/Maps & Modes, Level Travel, Lua runtime/modules,
  Physics/CharacterMovement, Scene/Prefab documents and restart, Inspector,
  Material Editor/Preview and restart, Runtime UI, interpolation and lighting.
- Final focused run: **2/2 passed** (`out/camera-editor1-target-tests.log`),
  **3374 CPU checks** and **127 native DX12 checks**. The focused run strengthens
  the already-passing related run with a child camera inside an ordinary Player
  Prefab, selected in the real Prefab document.
- CPU checks cover projection-derived corner NDC/depth, perspective/orthographic
  span, aspect/FOV/clip, arbitrary parent rotations/roll, signed uniform scale,
  disabled/deleted cameras, extreme valid settings and preference compatibility.
- DX12 uses actual ImGui Inspector/Scene View, target readback and pixel changes
  for movement, rotation, FOV, clipping, orthographic size and top-down view.
  It checks that moving the preview camera leaves Scene View pixels identical,
  leaves authored serialization and editor pose unchanged, and never changes
  gameplay selection. Additional checks cover picking, parent/Prefab preview,
  resize, deletion/disable, hide/reopen cycles, Play/Stop, project changes and restart.
- A late-close regression releases the target from ImGui EndFramePre, after its
  image command has recorded the SRV. It verifies safe current-frame retirement,
  recovery/reallocation and **zero DX12 validation errors**, with
  `WHISP_DX12_DEBUG=1`. Device restart retains the zero-error assertion.
- The first targeted run exposed two test assumptions: old preferences still
  require the existing version field, and restart may legitimately restore a
  Camera Prefab document and create a fresh preview. Corrected tests verify valid
  version-1 compatibility, empty resources on a new device, and live new-World
  handles for restored documents (`out/camera-editor1-target-tests-initial.log`).
  No production assertion or sandbox rule was weakened.
- Initial build integration fixed a missing explicit Engine source-list entry
  and a Windows `near` macro collision in a test variable. Successful final build
  results above supersede those attempts.
- Original preferences/layout bytes were restored and verified by the runners.
  `git diff --check` passed. Game projects/resources were unchanged, with no branch
  switch, commit or repository cleanup. The periodic RMB free-fly test passed;
  no unrelated fix for it is claimed.

Debug was not built. The related Vulkan coverage is the existing Camera System
smoke test, not a live Camera Preview Vulkan regression.

Changed files:

- `engine/editor/CameraEditorMath.h`: shared-state frustum corners and display cap.
- `engine/editor/EditorCameraTools.cpp`: wire overlay, picking and inline preview.
- `engine/editor/EditorLayer.h` / `.cpp`: per-frame request, settings and overlay integration.
- `engine/editor/EditorInspector.cpp`, `EditorEntityIcons.cpp`: preview and specialized icon ownership.
- `engine/editor/EditorPreferences.h` / `.cpp`, `EditorUi.cpp`: persisted display flags.
- `engine/editor/EditorProject.cpp`: project reset cleanup.
- `engine/core/Application.h` / `.cpp`: single-pass scheduling and temporary CameraState.
- `engine/render/IRenderAdapter.h`: optional auxiliary target capability.
- `engine/render/backends/dx12/Dx12RenderAdapter.h` / `.cpp`: target, barriers, retirement and readback.
- `CMakeLists.txt`, `engine/CMakeLists.txt`: source and regression integration.
- `tests/CameraEditorToolsRegression.cpp`: CPU and native workflows.
- `docs/CameraEditorTools1.md`: architecture, UX, limits and verification.

Live preview is implemented on DX12. Other adapters retain gizmo/frustum support
and show the backend limitation instead of a fake preview image. No Vulkan preview
or full multi-viewport feature parity is claimed. Future work could provide isolated
per-view render scratch resources and budgeted simultaneous rendering; animation,
controllers, rigs, blending, shake and cinematic tools are outside this stage.
