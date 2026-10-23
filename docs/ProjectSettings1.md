# Project Settings 1.0

## Audit and architecture

ProjectContext already owns a typed ProjectSettings value (name, startupScene,
gpuInstancing) and the original JSON descriptor. ProjectManager::Prepare validates
format version, types and contained paths. Application publishes immutable project
contexts only after successful saves/transitions. WithSettings preserves unknown
JSON fields, detects external descriptor edits, and writes through the existing
exclusive temporary-file, flush and atomic replacement transaction. No second
configuration file or settings registry was introduced.

Settings live in the project's .whispproject manifest. Missing rendering settings
retain the existing default (GPU instancing enabled). Unsupported format versions
are rejected. Editor layout, tools and document sessions remain in Preferences;
scene lighting, fog and antialiasing remain scene data; backend and fixed physics
clock remain startup/engine configuration. Opening the settings panel changes none
of those values.

The previous Project Settings panel already provided draft editing and the global
project transition guard. This stage registers it as DocumentType::ProjectSettings
(appended after existing document enum values), connects dirty/Save/close decisions,
and restores its presence and active selection through existing document sessions.
The published ProjectContext is authoritative; one existing draft is retained
until explicit Save or Discard. Draft values are never stored in Preferences.

## Interface

Open File > Project Settings. Reopening reuses the same project-scoped document.
The existing dockable ImGui panel can be moved and docked independently of Scene
Hierarchy/Inspector. General now includes a case-insensitive name search, adaptive
category width, wrapped paths and read-only descriptor/version information.

Project / General supports:

- Project Name: display name, editable; no folder/file rename.
- Startup Scene: existing project-open scene contract, editable with existing
  scene picker. This is not a newly introduced Editor/Game Startup Map contract.
- Project File, Format Version, Project Root, Assets Root and Scenes Root: read-only.

The pre-existing Rendering category retains functional GPU Instancing. Existing
Physics and Startup / Runtime informational categories are explicitly read-only.
No fictional Input, Packaging, GameMode or Maps & Modes controls were introduced.
Opening without a project shows instructions to open a .whispproject.

## Save and lifecycle

Changing the draft marks the document unsaved. Toolbar Save, Ctrl+S and Save & Apply
use the existing manifest save path. Startup Scene takes effect on the next project
open; GPU Instancing uses the existing apply contract. Editing/saving during Play
is disabled. Revert restores the published values.

Closing either the document tab or panel with dirty settings uses the existing
Document transition Save/Discard/Cancel dialog. Save failure leaves the draft and
error visible; Discard reloads the published settings; Cancel retains the document.
Switching between documents retains the draft. Project switch/close/exit use the
existing global unsaved-project guard and save-all-before-transition operation.
Project transitions reset draft/context/error/search state; session restoration
stores only document presence/selection and layout, never settings values.

Missing/read-only manifest, invalid names or scenes, unsafe paths, incompatible
versions, malformed JSON and external descriptor changes use existing diagnostics.
A failed replacement cannot publish an updated ProjectContext. Unknown JSON fields
remain in the descriptor on save.

## Future extension and limits

Future categories can extend ProjectSettings, Prepare/WithSettings and the existing
category content without another persistence path. GameMode, Default Player Prefab,
separate game/editor startup maps, physics configuration, Input Mapping and Packaging
are not implemented. PlayerStart's empty-Prefab legacy semantics are unchanged.
Settings Undo/Redo is not added; use Revert or close Discard. Search currently covers
General. The existing project transition save operation is retained; no new global
Save All menu is invented.

## Files changed in this stage

- engine/editor/EditorDocuments.h/.cpp: document type, dirty/save/close/session integration.
- engine/editor/EditorProjectSettings.cpp: registration, close guard, search/read-only data.
- engine/editor/EditorLayer.h: General search text.
- engine/editor/EditorProject.cpp: reset search on project transition.
- engine/editor/EditorUi.cpp: allow opening the empty-state panel.
- engine/core/ProjectContext.cpp: reject read-only/unavailable manifest before replacement.
- tests/ProjectSystemRegression.cpp: typed persistence and actual document workflow regression.
- docs/ProjectSettings1.md: this document.

Previous Player Workflow/Camera Attachments implementation was preserved.
No source assets or user game projects are edited by these tests.

## Validation

Verified on Windows, 2026-10-23:

- Release WhispEngine and related targets built successfully. Logs:
  out/settings1-build.log, out/settings1-project-build.log,
  out/settings1-workspace-build.log.
- Initial related run: 31/33 passed, 185.58 seconds (out/settings1-tests.log).
  The two ProjectSystem tests failed at the new read-only-manifest assertion.
  The test now removes all write permission bits to establish Windows read-only
  correctly; the manifest writer also explicitly rejects FILE_ATTRIBUTE_READONLY
  before attempting replacement.
- Rebuilt ProjectSystem CPU/DX12 run: 2/2 passed, 10.69 seconds
  (out/settings1-project-tests.log). New coverage includes typed reopen, unknown
  JSON preservation, invalid values, read-only failure/file preservation, singleton
  document registration, dirty close/Cancel, active Save, Discard, invalid Save
  retaining its draft, and the actual dockable ImGui window. Existing project
  lifecycle/portability and zero DX12 ERROR/CORRUPTION assertions remain unchanged.
- The other 31 related tests passed: document/Prefab and restart, SceneValidation,
  Player Workflow CPU/DX12, Camera/Attachments CPU/DX12 and Vulkan smoke, physics,
  movement, Lua/modules, Runtime UI, interpolation/instancing, lights, Inspector
  and Material Editor. Their assertions were not weakened.
- Workspace/layout native regressions: 4/4 passed, 48.77 seconds
  (out/settings1-workspace-tests.log), including separate-process restart, corruption
  recovery and legacy layout migration, with existing DX12 validation assertions.
- The known periodic RMB test passed during this run; no fix for that input issue
  is claimed.

The initial 33-test run is not described as all-green; the subsequent targeted
run verifies the corrected ProjectSystem path. Tests create only owned fixtures.
The runner restores and checks original editor preference/layout bytes. No Debug
build result is claimed. git diff --check passes; source assets are unchanged.
