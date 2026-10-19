# Lua Runtime Inspector

Open **View -> Lua Runtime Inspector** during Play and select an entity in the Hierarchy, Viewport or Gameplay Events. The existing Editor/Runtime selection mapping is used. The current engine supports one LuaScript component per entity; entities without a live behaviour show an explicit status instead of authored Script Fields.

The panel reads the actual running instance's `self` table and, separately, custom data in that instance's behaviour definition. Definitions/environments are created separately per instance in this runtime: two instances of the same prefab do not share inspected state. Engine `self.entity`, field declarations and known lifecycle callbacks are omitted. Absent declared fields display actual raw nil values, never schema defaults or serialized overrides.

Supported copied values: boolean, number, escaped string, nil, bound Entity identity and bounded nested tables. Other userdata, functions, threads and unsupported keys display their type. Locals, closure upvalues and environment globals are unavailable. Inspection uses raw table operations, never `__index`, `__pairs`, `__tostring`, callbacks, a new Lua VM or re-evaluation of source.

Limits: 256 copied value nodes per snapshot, 64 scanned entries per table, three nested table levels, 512 bytes per string and 128 bytes per displayed string key. Cycles and truncated snapshots are labelled. Child tables that are depth-limited remain opaque. Available entries are sorted by displayed name; a truncated table is a bounded subset, not an exhaustive listing.

**Refresh** reads once. **Auto Refresh** reads at most once per 250 ms while the panel is open. Changing selection or instance refreshes once even in manual mode. Otherwise manual mode only reads cheap C++ lifecycle/status metadata, not Lua tables. The UI displays snapshot age; values can be frozen while status/error metadata remains live. Pausing never invokes a behaviour to update its fields.

Snapshots contain only copied C++ values, no Lua references or World pointers. Full Entity handles, session, instance serial and program revision guard replacement. Stop/scene changes clear the panel snapshot. Successful reload invalidates old fields until the existing runtime synchronizes the replacement; failed reload shows the existing source-level diagnostic alongside the still-running old instance. This does not change the existing reload/reset policy.

Callback faults and rejected instance creation retain diagnostics for their exact entity. A healthy peer is never assigned an unrelated path-global callback error. Source load/reload diagnostics are labelled as shared-source diagnostics. Existing error logging and Gameplay Event history remain in use.

**Show Events for Entity** opens Gameplay Events with a filter matching the selected entity as source or target. **Inspect Script** in event details selects the still-live runtime entity and opens this panel. Neither action invents event relationships or starts event capture automatically; bounded history can legitimately have no remaining matching records.

Verification: `LuaInspectionRegression` covers actual values, primitive/opaque types, entity bindings, nil, bounded/cyclic tables, owner-thread guards, repeated reads without callbacks, per-instance faults, reload, deletion and sessions. `InteractiveRoomRegression` covers E-driven door values, independent instances, manual/automatic refresh, native panel controls/search, event navigation, pause, reload, Stop, repeated Play and the real InteractiveRoom project. GameplayEvents CPU/DX12 and SceneValidation regressions cover the adjacent tools. Temporary fixtures and editor preferences are preserved/cleaned by the tests.

Files added for P2.4:
- `engine/scripting/LuaInspection.h`
- `engine/editor/EditorLuaRuntimeInspector.cpp`
- `tests/LuaInspectionRegression.cpp`
- `docs/LuaRuntimeInspector.md`

Files extended for P2.4:
- `engine/scripting/LuaRuntime.h`, `engine/scripting/LuaRuntime.cpp`
- `engine/scripting/GameplayEventHistory.h`
- `engine/editor/EditorLayer.h`, `engine/editor/EditorLayer.cpp`
- `engine/editor/EditorUi.cpp`, `engine/editor/EditorTransactions.cpp`, `engine/editor/EditorGameplayEvents.cpp`
- `tests/InteractiveRoomRegression.cpp`
- root `CMakeLists.txt` and `engine/CMakeLists.txt`

Final targeted run: all five tests passed (Lua Inspection 59 checks, Interactive Room 192, Scene Validation 49, Gameplay Events CPU 36 and DX12 40). WhispEngine and all affected regression targets built successfully. The native inspector screenshot was visually checked. Existing Scene Validation work remains in the working tree; no commit or branch switch was performed.
