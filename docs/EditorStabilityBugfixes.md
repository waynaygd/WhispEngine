# Editor.2C.1 — Stability and workflow bugfixes

Work uses the existing uncommitted Editor.2C implementation on `engine_workflow`. No commit, branch switch, lab №3 change, ECS rewrite, resource system rewrite or rendering architecture change is part of this work.

## Findings and status

| Problem | Cause | Result | Verification |
| --- | --- | --- | --- |
| PlayerStart Eye | Overlay reused the FPS Euler pitch clamp. An equivalent decomposed Yaw 180° rotation can have X/Z = 180°; clamping X made the arrow point almost vertically. | Fixed in the editor overlay using full quaternion orientation for forward and arrow wings. | Native PlayerStart rotation, Eye pixels, equivalent Euler representation, Local/World quarter turns, parent scale, Undo/Redo. |
| Asset Import folders | The Import modal always combined Target Folder + source stem into a new package, including standalone images. | Texture imports publish one file directly; models/HDR retain package ownership. Multi-select and newline batch input support standalone images. | Real Import modal with PNG/JPEG, direct file keys, no stem folders, repeat collision preserving bytes; existing model-package tests. |
| Project Open dirty | SetupEcsRuntimeDemo calls ClearPhysicsStressScene, which intentionally marks a user cleanup dirty. Bootstrap inherited that flag despite creating an untouched/empty World. | Startup marks the constructed baseline saved; the Legacy fallback does likewise. User cleanup and ordinary dirty checks are retained. | Native before-fix failure, fresh browser/Legacy open without prompt, real edited project Save/Discard/Cancel and failure guards. |
| Asset Rename | EditablePath omitted UI Document/Style and FileSuffix treated their compound suffix as plain `.json`. Open document keys and sessions were not remapped. | Authoring UI Document/Style can rename when unreferenced and clean. Full suffix, tab identity, session and history survive. Referenced resources remain blocked with specific reasons. | Native F2 Rename/Enter, active UI tab, subsequent Save/Undo/Redo, dirty refusal, UICanvas reference refusal, Style rename; file-operation suite. |
| Widget Lock | Designer picked the last rendered hit and had no editor exclusion metadata. | Hierarchy lock glyph and Lock/Unlock context action; Canvas picking skips locked widgets and suppresses their manipulation handles. | Real hierarchy activation and Canvas mouse events, nested child selection, no document/history changes, tabs/reload and Play guard. |
| Volumetric Fog | In the inspected Project A, saved Extinction is 1.09 per world unit. Switching that component from Distance to Volumetric activates very strong Beer–Lambert absorption. | No renderer regression established in controlled checks. Shader/medium math retained; Inspector now explains transmittance for the current value. | Real DX12 Project A copy, controlled CPU/DX12 fog checks, lighting/output/Play/F11 comparisons, timing CSV and pixels. This is a diagnosed configuration effect, not a claimed shader bugfix. |

## PlayerStart overlay

Reproduce in a fresh scene: create PlayerStart, apply two Y quarter turns through the existing transform action, inspect the decomposed Transform. `(X=pi,Y=0,Z=pi)` represents the same physical orientation as Yaw=pi. `PlayerLookDirection` interprets X as a gameplay camera pitch and clamps it to approximately 89°. That is unsuitable for an editor TRS overlay and produces an almost vertical vector for a horizontal 180° turn.

`PlayerStartGizmoDirection` in `LightGizmoMath.h` derives local +Z from the complete normalized rotation quaternion. `DrawPlayerStartGizmos` uses that direction and the rotated local X for the arrowhead. Eye attachment remains feet plus world-sized Eye Height, with no rotated world position or accumulated offset. Scale does not change the body/arrow dimensions. Parent transforms still use WorldTransform and the existing hierarchy restrictions on shear/singular scale.

Runtime PlayerSpawn, FPS pitch limits, controller scripts and the real gameplay camera are unchanged. The native regression also evaluates the former helper on the problematic Euler representation, establishing the old vertical direction while checking the new horizontal result. It checks the yellow Eye pixels after rotations and Undo/Redo and captures `entity-icon-captures/player-yaw180.ppm`.

## Project startup and dirty guards

The baseline is established immediately after initial SetupEcsRuntimeDemo in Application::Initialize, before loading a configured scene. OpenScene continues to establish its own loaded baseline. When Legacy Workspace builds the fallback demo rather than loading a saved scene, that construction is also marked saved. ClearPhysicsStressScene itself remains dirty when invoked as a later editing operation.

This does not infer cleanliness from an empty World, a filename or entity count. New/Untitled scenes with actual editor transactions remain dirty; Scene, Material, UI and Project Settings protection and project staging remain intact. The native ProjectSystem check failed before the fix with `fresh Project Browser has no authored changes`. After the fix it drives RequestProjectOpen from both fresh startup and Legacy without an unsaved dialog. Existing edited-project modal tests exercise Cancel, Discard, Save from Pause and save failures. No empty Untitled file is generated by a clean project transition.

## Widget Lock

`UIDocumentEdit::lockedWidgets` stores widget IDs as editor session metadata. The lock glyph is a small vector padlock, with state color and a tooltip explaining Canvas selection/move/resize and session ownership. The tree context menu offers Lock/Unlock. Every widget type uses the same control. Hierarchy selection remains available for finding/unlocking a widget.

Only `editor::widgets::Pick` receives the exclusion set; runtime hit testing and draw data are unchanged. It continues searching behind a locked hit. A locked parent does not recursively lock its children. A selected locked widget produces no Canvas resize handles and cannot begin a Canvas drag. Other unlocked widgets retain normal move/resize transactions.

Locks survive document/workspace switches and explicit reload for IDs that still exist. They clear on project/session reset and are not serialized to `.ui.json` or preferences across process restart. Lock changes do not dirty the document or consume authored Undo/Redo; geometry Undo/Redo remains unchanged. No Lock Children behavior was added. IDs are the session identity, so deleted/reused/renamed IDs do not have a separate persistent lock identity.

## Import destination and package ownership

The Import modal uses standalone publication for PNG/JPEG/DDS. Example: source `brekotkin.png` and Target Folder `textures` produce `textures/brekotkin.png`. Package name is hidden for these files. The standard file chooser supports multiple selections; multiline source input accepts one path per line. Batch entries are scanned before publication, with duplicate destination names and existing destination collisions rejected before the first publish. Images keep their source filenames and resource keys.

`ImportPlan::standaloneImage` distinguishes explicit standalone file publication from existing package publication. The decoder validates an owned staging copy. A successful standalone publication renames that one file to the destination and removes its empty staging folder. No source files or pre-existing user directories are deleted. A failed publication cleans only staging/parents owned by that attempt. Batch files are individually atomic; if a later file changes or fails decoder validation during publication, earlier successfully imported files remain and the error is shown. The batch is not advertised as one transaction.

OBJ+MTL, GLB/GLTF dependencies, generated mesh manifests, embedded texture handling and model caches keep the previous package path. The existing Scan API defaults to package mode for existing callers that explicitly request packages; ordinary image import in the Editor modal chooses standalone mode. HDR retains package ownership and coupled metadata restrictions. A batch of model packages is intentionally rejected with an explanation; each model can carry its own dependency set.

## Safe rename boundaries

Canvas-entity/GUID names are ordinary authored UI documents: EditorLayer's New UI Document/CreateUICanvas writes them through UILoader::Save. The name is not a generated-resource ownership marker. The supported authoring scope is `ui/` in the active writable asset root, with `.ui.json` or `.uistyle.json`. Model-package/generated paths, shaders, protected built-in resources, source fallbacks, read-only/system/reparse files and unsafe names retain their guards.

FileSuffix preserves `.ui.json` and `.uistyle.json` during rename. `RenameAssetDocument` updates the canonical path/key of the existing document without changing its ID; UI map nodes retain draft/history/view/lock state at the new resource key, and Material session nodes retain history. Script previews invalidate their cached source. Invalidation unloads old caches, stops watching the old asset path and registers the new watched resource where applicable. The ResourceManager addition only removes matching watch entries under the existing lock; it does not rewrite loading/versioning.

The operation still refuses disk references, current Scene/Settings references and Scene Undo/Redo references. It now also considers UI drafts/history, dirty UI/Style sessions and material session/history texture references. Dirty documents must be explicitly saved/discarded first. An open Prefab Document must close before file operations, because suspended Prefab session migration is unsupported. Open asset Documents must close before Delete. Destination collision and dependency checks precede filesystem mutation; blocked/failed checks leave paths, references and drafts intact.

**No general dependency migration was introduced.** Referenced UI documents/styles, scripts, materials and textures remain blocked rather than partially rewriting Scene/Prefab/UI/Lua files. This means an unreferenced clean Canvas can rename, while a Canvas currently assigned to UICanvas must first have its references managed explicitly. Scene file rename remains unsupported; unreferenced Prefabs retain the existing guarded operations when no Prefab tab is open. Dynamic/computed Lua paths remain outside static proof; existing prefab dynamic-risk confirmation and hard dependency blockers are preserved. The UI names the dependency/session sources instead of incorrectly classifying all UI assets as protected/generated.

## Fog diagnosis and visual evidence

No trustworthy older visual reference was supplied. The comparison therefore uses a fixed camera in an owned copy of Project A and the physical expectations already tested by FogRegression: zero extinction preserves source color, homogeneous transmission follows `exp(-sigma * distance)`, height attenuation follows its analytic integral, directional/local lights contribute scattering, and shadow maps reduce scattering.

The original Project A scene stores mode Distance and Extinction approximately 1.09. Extinction is inactive in Distance mode. In Volumetric mode at/below Base Height, transmission is about 33.6% over one unit and **0.00185% over ten units** at Density=1. Strong loss of scene detail is expected. With the existing Height Falloff=0.25, absorption weakens above Base Height, as the Inspector explanation states. A control value of 0.04 was changed only in the diagnostic copy/in-memory World, never in source materials or the user's scene.

The captured authored-extinction image becomes a dense scattering veil; this is not uniformly darker HDR output. The initial fixed-camera mean linear HDR was 0.11543 with Fog Off, 0.214879 with authored Volumetric settings and 0.135691 with Extinction=0.04. Mean brightness is diagnostic only; the images and transmission tests distinguish detail extinction from insufficient illumination. No Fog shader, far plane, shadow pass, PostFX, material evaluation or P3.1 performance path was replaced.

`FogRegression --project <owned descriptor>` requires a `stability-fixture` marker beside the descriptor, since loaders may create caches. This local diagnostic was run against `out/editor-stability/ProjectA`, copied from Project A. It captures Off/Distance/Height/Volumetric, control extinction, Lit/Unlit, no Directional, no Spot, output controls, Edit/Play/Pause and F11. For the Play comparison only the copy's spawn/camera setup is fixed and its controller stopped, so different spawn points do not masquerade as a Fog difference. The diagnostic also checks that visiting Material Preview and returning does not change the Fog scene's HDR pixels.

Evidence is under `out/build/x64-release/fog-captures/`: `projectA-*.ppm`/PNG and `projectA-comparison.csv` (mean HDR, CPU/GPU pass times and actual target size). Controlled Fog tests additionally capture shadow/local-light/quality comparisons and timings. Project A's original Main.json SHA-256 remained `E262E977EC74934CE93B7662BF644388F51579E58227DBBBB308C95F0FFE6E6F`.

Final Project A diagnostic means: 12 warm-up + 8 measured frames per scenario on the local RTX 4060 Laptop GPU. CPU measures Fog submission; GPU measures the Fog pass with the existing delayed timestamp readback. These are short local measurements, not a portable budget.

| Scenario | Resolution | Mean linear HDR | CPU Fog ms | GPU Fog ms |
| --- | --- | ---: | ---: | ---: |
| Off | 400x300 | 0.115430 | no pass | no pass |
| Distance | 400x300 | 0.124678 | 0.0339 | 0.0093 |
| Height | 400x300 | 0.151461 | 0.0329 | 0.0092 |
| Volumetric, authored Extinction | 400x300 | 0.214879 | 0.0464 | 0.0984 |
| Volumetric, control 0.04 | 400x300 | 0.135691 | 0.0402 | 0.0988 |
| Unlit | 400x300 | 0.437924 | 0.0650 | 0.0308 |
| No Directional | 400x300 | 0.135335 | 0.0401 | 0.0293 |
| No Spot | 400x300 | 0.135691 | 0.0679 | 0.0968 |
| Output controls disabled | 400x300 | 0.135691 | 0.0471 | 0.1235 |
| Play, fixed camera | 400x300 | 0.135691 | 0.0448 | 0.1249 |
| Pause, fixed camera | 400x300 | 0.135691 | 0.0607 | 0.1271 |
| Edit return | 400x300 | 0.135691 | 0.0462 | 0.1286 |
| F11 | 1280x720 | 0.133237 | 0.0409 | 0.6648 |

Directional/Spot ablations preserve the existing scene's light settings otherwise. The Spot does not materially affect the whole-image mean at this camera; controlled FogDx12Regression separately asserts a Spot contribution at a chosen ray. The no-post case disables AA and environment tone mapping, rather than inventing effects absent from the source scene. F11 changes aspect ratio/size and is checked for valid output/resource resize, not identical pixels to a 4:3 target.

## Regression record

All interaction tests use the actual native Application, Dear ImGui and DX12. No stub renderer replaces the preview, editor or fog path. Generated captures, fixture projects and logs stay under ignored `out/`; native test scopes restore the caller's preferences. Layout-sensitive interaction fixtures explicitly start with default preferences and restore the original files on exit.

The selected related set contains 27 test targets: ProjectSystem CPU/DX12; EditorDocument/restart; PrefabDocument/restart; AssetBrowserUX/restart; AssetFileOperations CPU/DX12; WidgetDesigner; UI4; EditorEntityIcons CPU/DX12; Fog CPU/DX12 and FogPostHotfix CPU/DX12; LightSystem CPU/DX12; MaterialSystem CPU/DX12; InteractiveRoom; EditorWorkflow CPU/DX12; MaterialEditorUX/restart.

The first broad run passed 23/27. Four old UI fixtures required updates: default layout isolation for fixed mouse coordinates, the added UI menu entry when locating Environment/Light, and Material Preview as the foreground workspace panel instead of the former long-form Material Editor. The targeted rerun passed FogPostHotfixDx12, UI4 and EditorWorkflowDx12. InteractiveRoom then hit an intermittent movement assertion; no physics changes were made, position/yaw detail was added to its failure message, and two standalone runs passed (12.14 and 10.22 seconds). Thus all 27 selected targets have passing final results across the broad run and targeted reruns; this is not a claim of a single clean 27/27 run. The intermittent room assertion remains a test reliability limitation.

Final results and timings are recorded in `out/editor-stability-before.log`, `out/editor-stability-regressions.log`, `out/editor-stability-related-rerun.log`, `out/editor-stability-room.log`, `out/editor-stability-room-confirm.log`, `out/editor-stability-interaction.log`, `out/editor-stability-project-fog.log` and `out/editor-stability-all-build.log`. The Project A diagnostic passed 146 assertions, including exact HDR equality across Material Preview navigation, and identical fixed-camera Edit/Play/Pause means. Tests with native DX12 assert zero validation errors. WhispEngine and the selected regression targets build successfully. Tracked engine assets remain unchanged.

## Changed functions/files for this stage

- `Application::Initialize`, `Application::OpenLegacyWorkspace`: bootstrap clean baseline only.
- `LightGizmoMath.h`, `DrawPlayerStartGizmos` in `LightGizmos.cpp`: quaternion-derived overlay directions.
- `UIDocumentEdit`, `WidgetCommand`, `DrawUIDesigner`, `widgets::Pick`, `ReloadUIDocument`: editor-only lock state and Canvas exclusion.
- `AssetImport.h/.cpp`, `DrawImportDialogs`: standalone image publication, multi-file planning, preserved model packages.
- `AssetCatalog::FileSuffix/EditablePath`, `AssetFileBlockers`, `InvalidateAssetFileUi`, `RenameAssetDocument`, `DeleteAssetFile`: compound UI suffixes, session remapping and explicit safety boundaries.
- `ResourceManager::StopWatchingAsset`: remove obsolete hot-reload watches after a completed file operation.
- `DrawInspector` Fog section: current-extinction transmittance explanation.
- Targeted tests: ProjectSystemRegression, EditorEntityIconsRegression, WidgetDesignerRegression, AssetFileOperationsRegression, FogRegression. Related fixture updates: FogPostHotfixRegression, UI4Regression, InteractiveRoomRegression, EditorWorkflowRegression.
- This document. Existing Editor.2C changes remain in place without being reverted or committed.
