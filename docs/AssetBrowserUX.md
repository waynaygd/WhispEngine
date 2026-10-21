# Asset Browser UX (Editor.2A)

The existing Asset Browser now provides Grid, List and Compact views, a collapsible folder tree with a splitter, scoped search, multiple asset selection and keyboard navigation. Document activation continues through EditorDocuments and the existing Scene/Prefab/Material/UI/Lua lifecycle.

## Audit and scope

`EditorAssets.cpp` already owned the catalog presentation, import/create menus, dependency-checked file operations and `WHISP_ASSET` payload. `AssetNavigation` already cached folder children/counts and navigation history by catalog revision. `EditorAssetPreview.cpp` already provided real mesh/texture thumbnails, resource-version invalidation, a 96-entry GPU cache, at most two asynchronous load requests per frame/two pending loads and one thumbnail upload/render per frame. Those budgets and the resource loading algorithms are retained.

The former presentation had a fixed tree width, two view modes, one selected key and details below all list rows. During native layout validation, an initial three-row prototype left only four content pixels in the default 153 px bottom dock; the final short-dock layout uses two rows. Reading browser load status through `ResourceManager::Get` could validate project paths repeatedly. A 1220-texture fixture also exposed repeated ancestor checks during catalog Refresh: a native frame stalled for approximately 58 seconds.

The changes stay in browser presentation, browser preferences and small supporting APIs. `PeekIndexed` only reads a resource cache by an already indexed key; it cannot resolve, load or modify a file. Catalog Refresh caches entry safety checks for its duration, checks scan roots, rejects symlinks and Windows reparse entries, and never follows linked directories. Import/rename/delete path validation and dependency tracking are unchanged. There are no ECS, ResourceManager loading algorithm, GLB decoding or renderer pipeline changes. The only DX12 adapter change adds an optional proportional font to the existing ImGui atlas.

## Layout and presentation

- **Grid:** Small (120), Medium (180), Large (260 logical px), calculated column count, bounded card width, actual cached preview or monochrome vector icon, up to two name lines, type/status, hover and selection. In a short dock the preview sits beside the name instead of consuming the entire panel height.
- **List:** icon, available name width, type/status where space permits, cached size in wider panes. Narrow panes prioritize the name; the tooltip retains all metadata.
- **Compact:** icon and name only. It requests no thumbnails.
- **Details:** the Options menu opens the existing preview and metadata tools in an optional small `Asset Details` window. Double-clicking a texture/model also opens it. This avoids placing previews after thousands of clipped rows. Closing it leaves asset navigation intact.
- Short panels combine Header and Navigation into one row. In narrow short panels navigation and thumbnail size move into Options; controls remain within the dock. The tree automatically hides below 430 logical px without changing its saved visibility/width.
- Folder tree expansion, current-folder highlight, internal scrolling and an adjustable 90–480 logical px splitter are retained separately from content scrolling. Navigating reveals/expands the current path.
- Type symbols are drawn with ImDrawList, without an icon font or per-card textures. Scene, Prefab, Mesh, Material, Texture, Lua, UI, UI Style, PostFX and Audio have distinct symbols. Unsupported files receive an Unknown entry and informational opening; generated `.wmesh`/`.wtex` cache files stay hidden.
- Browser text uses Windows Segoe UI when available, loaded once into the existing font atlas with Cyrillic ranges. Other editor panels retain their font. Resource keys in tooltips use the existing monospace face. The existing font remains the fallback.

## Navigation, search and selection

Back/Forward, Up and Project root use the existing history. Breadcrumb segments navigate to their exact folder; overflow becomes an ancestor menu. Narrow short panels expose the same commands in Options.

Search matches display filenames through their full resource key, case-insensitively, using cached catalog entries. Scope is explicitly Current Folder or All Assets, including when the search is empty. Filters are All, Scene, Prefab, Model, Material, Texture, Script, UI and Other. Changing view mode preserves folder, search/filter and selection. Clear Search clears the query without changing the folder/filter.

Single click selects one asset. Ctrl-click toggles an asset; Shift-click selects the visible result-order range from the anchor. Empty content space clears asset selection. These operations never change scene entity or widget selection. Multiple selection is for selection and copying keys; bulk rename/delete/move are disabled with a reason.

When the browser owns keyboard focus and no text edit, popup, active item or drag owns input:

- Arrow keys move asset selection (Grid uses column count), scrolling the selected row into view.
- Enter opens the selected asset; Backspace navigates to its parent folder.
- Ctrl+C copies selected resource keys, separated by newlines.
- F2/Delete retain existing single-file dialogs and dependency checks. There are no new bulk file operations.

Right-click provides Open, Show in Explorer, Copy Resource Key, Copy Path, existing asset-specific actions, and the existing guarded Rename/Delete/Move. Unsupported, protected, Play/Prefab-context and multi-selection operations are disabled with explanations. Reference audits occur only when explicitly opening/confirming a file operation, never for each visible asset per frame.

Double-click opening waits for mouse release and cancels on drag threshold, including a fast second drag. Scene/Prefab/Material/Lua use `OpenDocument`, UI uses the existing `OpenUIDesigner` document entry point. Dirty guards, Save/Discard/Cancel and Prefab return-to-Scene are unchanged. Folders navigate directly. Models/textures reuse existing preview/import tools. `WHISP_ASSET` payload format and placement/assignment consumers are unchanged.

## Preferences and project lifecycle

The existing `editor-preferences.json` has an optional `assetBrowser` object: view, tree visibility/width, search scope, Details visibility and per-project sessions (folder/expanded paths). Existing `assetCardSize`, `assetGrid` and `projectAssets` fields/APIs remain compatible; preferences without the new object derive Grid/List from the legacy boolean. Width and view values are clamped. Restored folder paths must belong to the current catalog.

Project Switch stores the outgoing folder/expansion session, clears asset selection, navigation history, pending double-click and resource preview handles, then restores only the incoming project's folder/expansion. Selection and handles are not serialized. Restart restores settings and a valid last folder. Search text/type filters reset on Project Switch to avoid carrying hidden results into another project.

## Thumbnail cache and performance

Only clipped Grid rows and an explicitly opened selected preview call `RequestAssetThumbnail`. List/Compact do not request previews by themselves. The clipper uses the actual Grid/List row heights plus table cell padding; its guard row can include nearby items. A resident thumbnail handle is reused across steady frames. Resource version changes retire/rebuild the existing thumbnail; catalog revision/cache epoch changes use existing invalidation. Folder tree, hover metadata and keystrokes do not scan files. File sizes are cached during explicit catalog Refresh and are optional for scene entries supplied by SceneCatalog.

`BrowserStatistics` reports matched/clipped items, thumbnail calls/resident hits, load requests, uploads, catalog revision and per-thread project-path filesystem probes during browser rendering. The probe counter counts `symlink_status`/Windows attribute checks in `ProjectContext::CheckNoLinks`; it does not claim to count every OS operation in the entire editor. Together with an unchanged catalog revision and inspection of browser metadata paths, it verifies that steady presentation does not revalidate each asset on disk. Explicit Refresh/import/file operations and first asynchronous loads are outside that steady-state claim.

Native measurements use isolated 0/20/200/1000-file folders, 35 warm-up frames and 24 sampled frames per mode at 1280×720 with DX12 validation enabled. CPU values measure Browser UI submission, not GPU frame time. Grid uses Small thumbnails in the default short bottom dock; clipped counts include the guard row. This is a short regression series, not a general benchmark.

| Assets | Grid UI, ms | List UI, ms | Compact UI, ms | Clipped items G/L/C | Steady path probes |
|---:|---:|---:|---:|---|---:|
| 0 | 0.197 | 0.169 | 0.191 | 0 / 0 / 0 | 0 |
| 20 | 0.181 | 0.189 | 0.168 | 8 / 3 / 3 | 0 |
| 200 | 0.204 | 0.231 | 0.183 | 8 / 3 / 3 | 0 |
| 1000 | 0.263 | 0.181 | 0.224 | 8 / 3 / 3 | 0 |

All sampled series issued **0 new loads and 0 thumbnail uploads** after warm-up. Nonempty Grid series had **100% resident thumbnail hits** (8 clipped/nearby calls per frame); List/Compact made **0 thumbnail calls**. The latest explicit Refresh of 1223 entries is approximately 0.2 seconds. CPU variation in these short series includes validation and normal editor workload.

Raw results: `out/build/x64-release/asset-browser-ux-regression/performance.csv`. The native log also reports an explicit catalog Refresh duration. The initial repeated-path observation is recorded in `out/editor2a-audit-baseline.txt`; `out/editor2a-ux-test.log` contains the latest run.

## Regression and visual checks

`AssetBrowserUXRegression` runs the real Application/ImGui/DX12 editor with test-owned projects and native synthetic input. It checks view modes and Combo controls, size changes, tree visibility/splitter, history/breadcrumbs, name/key search, grouped filters/scope, text-input key ownership, single/Ctrl/Shift selection and clipboard routing, arrows, tooltips, double-click document types, texture preview, drag payload, missing previews, resource version reload, clipped steady-state metrics, short dock/narrow layouts, Project Switch and saved folder expansion. `AssetBrowserUXRestartRegression` starts a new process and verifies preferences/session restoration without stale selection.

Clipboard callbacks are isolated to test memory, so the regression does not replace the user's clipboard. Only owned fixture files are created/edited; source Scene and Prefab bytes are compared around navigation. Native GPU captures are saved beside the isolated executable (`grid-docked`, `list-docked`, `compact-docked`, `list-narrow`, `compact-narrow`, `grid-documents`). ImGui logs/captures are generated artifacts under `out/`, not project assets.

Editor.2A baseline outcomes across the targeted runs: **23 passed; 2 legacy Editor2 failures remain**. The final UI run passed 5/5 (AssetBrowserUX, restart, existing AssetBrowser, EditorWorkflow DX12, EditorUsability DX12). AssetBrowserUX passed **425 checks**, restart **9 checks**, with **zero DX12 validation errors**. AssetFileOperations DX12 passed its complete dialogs/dependency checks separately. EditorQualityOfLife passed after explicitly arranging a normal Level viewport.

The two unmodified optional legacy tests, `Editor2Regression` and `Editor2Dx12Regression`, fail before reaching Browser code: they require exactly 17 built-in ECS components although the unchanged current registry registers 20. Neither the registry nor these two tests was edited. This is recorded as an outstanding legacy-test mismatch, not a passing result.

Test fixture updates: existing Browser/file-operation tests now select controls by ImGui IDs instead of old menu-row/header coordinates, and wait for Stop's workspace transition before refocusing. Pixel/picking tests explicitly arrange Level layout rather than inheriting a saved 28 px viewport. The workflow test exits its fixed diagnostic layout before opening a Material Document; the usability test settles New Scene before setting the picking camera. These updates retain pixel, selection, file-safety and GPU assertions. No rendering or ECS production behavior was changed to satisfy them.

Logs: `out/editor2a-related-tests.log` (initial expanded run and failures), `out/editor2a-browser-retest.log` (complete file operation pass), `out/editor2a-layout-retest.log` (layout/pixel diagnosis), `out/editor2a-final-ui-tests.log` (final 5/5).

Related EditorDocument/PrefabDocument regressions retain dirty guards, save failure and return-to-scene coverage. Existing AssetBrowserRegression covers real PNG/JPEG/DDS/OBJ/glTF previews, stable GPU handles, resource invalidation and drag/drop placement. AssetFileOperations, EditorUsability and EditorWorkflow cover safe file operations, navigation and import; ProjectSystem/WidgetDesigner check project/UI integration. Resource tests validate the supporting cache lookup change.

## Limits

- Multi-selection does not introduce bulk filesystem operations or dependency rewriting.
- Audio, UI Style and Unknown entries provide identification/information; no audio player or second UI-style lifecycle is introduced.
- Very short docks show a small number of List/Compact rows; they preserve content space and internal scrolling. Long paths use breadcrumbs overflow/tooltip instead of widening the panel.
- Catalog Refresh remains an explicit synchronous scan. Repeated safety validation was removed; it is not a new background catalog framework. Measured steady-state rendering remains bounded by clipping and catalog filtering.

## Files changed for Editor.2A

`CMakeLists.txt`; `engine/editor/EditorAssets.cpp`, `EditorAssetPreview.cpp`, `AssetCatalog.{h,cpp}`, `EditorLayer.h`, `EditorPreferences.{h,cpp}`, `EditorUi.cpp`, `EditorProject.cpp`; `engine/resources/ResourceManager.h`; `engine/core/ProjectContext.{h,cpp}`; `engine/render/backends/dx12/Dx12RenderAdapter.cpp`; `tests/AssetBrowserUXRegression.cpp`, `AssetBrowserRegression.cpp`, `AssetFileOperationsRegression.cpp`, `EditorQualityOfLifeRegression.cpp`, `EditorUsabilityRegression.cpp`, `EditorWorkflowRegression.cpp`; this document. Existing uncommitted Editor.1/Editor.1.1 changes are retained. No branch switch or commit is performed.


## Editor.2A.1 — unified folders and assets

The separate folder Selectable loop before the asset table has been removed. `AssetNavigation` now caches `AssetContentItem` records alongside its existing folder tree/counts when the catalog revision changes. Each record contains a stable identity, display name, folder/asset kind, path/key and asset index. Asset identities remain resource keys; folder identities use `folder:` plus the folder path. Selection state stays in the existing selection set, and icons/previews use the existing rendering path. This extends the existing presentation cache; it does not introduce another catalog.

One sorted sequence, one ImGui table, one clipper and common selection handlers serve Grid, List and Compact. Folders precede assets, with name/key ordering within each group. Sorting runs only when rebuilding the revision cache. Grid uses the same card width, height, spacing, wrapping and Small/Medium/Large sizing for both kinds. Folders use the existing restrained ImDrawList folder glyph and a Folder caption, without GPU textures. List uses the same row/metadata layout (Directory status for folders); Compact uses the same minimal icon/name row.

Single/Ctrl/Shift selection and arrow navigation cross the folder/asset boundary. Enter opens the selected item; double-click waits for mouse release and cancels on a drag. Opening a folder reuses existing navigation history, breadcrumbs and tree synchronization. Keyboard ownership leaves the tree's native navigation intact. Asset opening still routes through the existing document/preview workflow. No Document Lifecycle, ResourceManager, renderer or ECS behavior was changed for Editor.2A.1.

Search semantics are preserved: child folders appear only in Current Folder with an empty query. Type filters apply to assets and retain those child folders. A nonempty query hides folders. All Assets searches assets only, including with an empty query. Folder context menus retain existing create actions and add Open; folder rename/delete/move and folder drag payloads are not implemented. F2/Delete cannot invoke file operations for a folder or mixed selection. Ctrl+C copies paths/keys without the internal folder identity prefix. Resource drag payloads and dependency guards remain unchanged.

### Matched before/after CPU measurements

Both runs used the native DX12 editor, the same 1223-entry owned catalog, 1280×720, the default short bottom dock, Small cards, 35 warm-up frames and 48 measured frames per case/mode. The mixed directory contains 1000 assets and 64 immediate child folders. Values are mean Browser UI submission CPU time; they are short local measurements, not a frame-rate guarantee.

| Assets | Folders | View | Before, ms | After, ms |
|---:|---:|---|---:|---:|
| 20 | 0 | Grid | 0.10635 | 0.04072 |
| 20 | 0 | List | 0.08744 | 0.03362 |
| 20 | 0 | Compact | 0.08616 | 0.05196 |
| 1000 | 64 | Grid | 0.11715 | 0.08430 |
| 1000 | 64 | List | 0.11779 | 0.07629 |
| 1000 | 64 | Compact | 0.10693 | 0.06611 |

All cases recorded **0 steady browser path probes**. Previously the 64 folder rows were submitted outside the asset clipper and excluded from the visible-item statistic. The unified statistic includes folders: the short dock submits 8 Grid items or 3 List/Compact items. In the mixed fixture these top items are folders and request zero thumbnails; scrolling to asset rows verifies that existing cached thumbnails remain available. There is no new per-frame filesystem validation, folder texture creation, thumbnail pipeline or CPU/GPU synchronization.

Raw results are `out/editor2a1-before.csv` and `out/editor2a1-after.csv`; logs are `out/editor2a1-baseline.log` and `out/editor2a1-after.log`. Run the existing executable with `--unified-benchmark` to collect the short comparison series. The before series used the saved Editor.2A implementation before these presentation changes.

### Native regression and executable inspection

The existing `AssetBrowserUXRegression` was extended, without a new test framework. It now passes **485 checks**, plus **9 restart checks**. Added checks use real ImGui table geometry/input for equal card widths, shared wrapping, folders-first ordering, all sizes/views, mixed single/Ctrl/Shift selection, arrows/Enter, double-click folders, history/Up, breadcrumbs/tree synchronization, preserved search/filter scope, safe folder context/keyboard/drag behavior, clipped 1000-assets/64-folders rendering and a genuinely narrow docked browser. Existing asset document activation, drag/drop, thumbnail invalidation and project/restart checks remain.

The final targeted CTest run passed **7/7**: AssetBrowserUX and restart, AssetBrowserRegression, EditorDocument and restart, PrefabDocument and restart. Native DX12 validation reported **zero errors**. Related document tests retain dirty/save failure/return-to-scene coverage. Log: `out/editor2a1-final-tests.log`; detailed native output: `out/build/x64-release/Testing/Temporary/LastTest.log`.

The existing placement regression initially restored the user's saved Scene session before creating its floor, introducing unrelated entities into the test raycast. It now starts with isolated default preferences and asserts its world contains only its owned floor. Its scoped guard restores original preferences/layout bytes after the application shuts down. Pixel, placement, round-trip and GPU assertions remain intact; production placement logic and gaming assets were not changed.

The actual freshly built `WhispEngine.exe` was also launched twice against the isolated test project, opening Project Root and `scripts/unified` in Grid. Both processes exited successfully. Captured frames were visually inspected: Root folders and the Unknown asset share one grid; the scripts view places two folders followed by seven Lua assets in equally sized cards. These were executable startup/capture runs; interactive actions were exercised by the native regression.

Captured executable frames:

- [Project Root](../out/build/x64-release/asset-browser-ux-regression/manual-root.png)
- [Scripts with folders and Lua assets](../out/build/x64-release/asset-browser-ux-regression/manual-scripts.png)
- [Native narrow dock regression](../out/build/x64-release/asset-browser-ux-regression/unified-narrow-docked.png)

Launch logs: `out/editor2a1-manual-root.log`, `out/editor2a1-manual-scripts.log`. Captures, logs, benchmark snapshots and test fixtures are generated under ignored `out/`; they are not gaming resources.

### Files and limits for Editor.2A.1

`engine/editor/AssetNavigation.h`, `engine/editor/EditorLayer.h`, `engine/editor/EditorAssets.cpp`, `tests/AssetBrowserUXRegression.cpp`, `tests/AssetBrowserRegression.cpp`, and this document. Earlier Editor.1/1.1/2A changes remain present. Work stays on `engine_workflow`, without a commit or branch switch.

Folder rename/delete/move, folder payloads, recursive folder search and bulk file operations remain outside this change. Large directories still build a filtered pointer list per frame, while sorting is revision-cached and rendering is clipped. These measurements cover the stated local browser scenarios; the complete engine suite was not rerun for Editor.2A.1. The historical optional Editor.2A legacy component-count mismatch above was not modified by this task.
