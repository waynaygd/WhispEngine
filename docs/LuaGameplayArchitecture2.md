# Lua Gameplay Architecture 2.0

Implementation follows [the accepted audit](LuaGameplayArchitecture2Audit.md).
One LuaScriptComponent still owns one Behaviour per Entity; scenes and prefabs
need no migration. No DOOM67 files, character physics, crouching or concrete
weapon/health systems are added.

## Behaviour and module

A Behaviour returns the existing fields/callback table and is attached to an
Entity. A module returns a table of functions/data, has no required callbacks or
fields, and does not need to be attached to an Entity. Both are ordinary `.lua`
files in the active project's `scripts/` directory.

```lua
-- Assets/scripts/common/sequence.lua
local M = {}
function M.advance(state)
    state.value = (state.value or 0) + 1
    return state.value
end
return M
```

```lua
-- A Behaviour using the module
local Sequence = Script.require("common.sequence")
return {
    fields = {},
    on_start = function(self) self.sequence = {} end,
    on_update = function(self, dt)
        self.last_value = Sequence.advance(self.sequence)
    end
}
```

`common.sequence` resolves only to `scripts/common/sequence.lua` within the
active asset root. Names consist of dot-separated ASCII identifiers, maximum
128 bytes; each segment starts with a letter or underscore, then letters,
digits or underscores. File paths, empty segments, slashes, drive letters and
traversal are rejected. SafeAsset/project canonical-path checks also apply.
Legacy workspace scripts use their existing active asset root.

Declare imports at the top level of Behaviours/modules. A callback may request
an already declared module; discovering a new dependency only inside a callback
is rejected with `Import must be declared at script/module top level`. This
explicit graph rule permits validation of the complete dependency graph before
live replacement. Computed names are permitted at top level when they resolve
within the same bounded rules, but literal names are easier to audit and rename.

## State, cache and sandbox

- Each Behaviour Instance has its own module tables, closures and environment.
  A repeated import in that instance returns the same table. Two Entities using
  the same Behaviour do not share mutable module state.
- Modules within one Behaviour share its sandbox environment and owner-bound
  APIs. Prefer local variables and explicitly passed self/state, not globals.
  State stored on self remains visible in the existing Lua Inspector; private
  closure upvalues are not added to Inspector inspection.
- Immutable source/bytecode snapshots are cached separately. Unchanged modules
  reuse compiled bytecode across instances and reload candidates; the transient
  compilation cache is bounded to 128 entries. Loaded Program snapshots retain
  their required bytecode independently of cache eviction. No user-supplied
  binary chunk is accepted: only engine-compiled text becomes cached bytecode.
- Maximum source size remains 256 KiB per file. A graph is limited to 64 module
  files, 16 active import frames (including the root) and 2 MiB of source total.
  Cycles/deep graphs report the dependency chain, rather than recursing forever.
- No package/io/os/debug/loadfile/dofile/load/pcall is enabled. Existing allowlist,
  permissions and callback signatures remain in force.
- Nested evaluation shares the outer 200,000-instruction execution hook: an inner
  scope neither resets the counter nor clears the outer hook. The owning scope
  clears its hook when execution ends, including errors.

Module initializers should be pure and deterministic. Gameplay APIs are bound
after top-level evaluation, as for existing Behaviours: perform gameplay actions
in callbacks. Modules do not acquire extra Camera/Input/Character permissions.

## Reload and lifetime

Runtime checks known source timestamps/sizes at most once per 0.5 seconds of
Update time, on the existing owner thread. There is no second ResourceManager
or file watcher thread. ResourceManager has no existing Lua resource loader;
Lua-specific graph metadata lives alongside existing Program/Instance state.

A changed root or dependency selects its transitive dependent Behaviours. All
affected candidate graphs are evaluated and schema-checked in disposable VMs
before any Program revision is published. Existing live actors' serialized
overrides must also remain valid against the candidate fields; otherwise reload
is rejected before retirement. Adjust the overrides and explicitly reload, or
save the corrected source again. Failure preserves live instances,
their self/upvalues, timers and accepted source snapshots. Old dependency edges
and newly failed imports remain watched for recovery. Creating a previously
missing file can recover a rejected Behaviour automatically.

Failed transaction cohorts are retained: fixing one rejected root retries the
entire affected group, so a shared module cannot leave half the group updated.

The transaction checks that its source metadata did not change while preparing
candidates and retries a raced source snapshot on the next poll. Unrelated watched
files do not block it. Explicit
`LuaRuntime::ReloadScript(resourceKey)` uses the same path for either a Behaviour
or a dependency. Successful publication lets existing Sync retire old instances
through on_stop/on_destroy and run replacement on_start; transient state resets
under the existing reload lifecycle. A fresh module graph is instantiated for
each replacement. Changes made while Play is paused are picked up when Update
resumes, or by an explicit reload. Stop/new Start clears sources, module caches,
watches and dependency edges, including across project switches.

Load-time validation is not a rollback mechanism for errors in subsequent
gameplay callbacks. Those retain the existing error/failed-instance semantics.
As before, nondeterministic top-level code should be avoided: validation and
instance execution take place in different VMs/environments.

## Events and composition

```lua
-- Request from one Behaviour
World.send_event(target, "request", 25)

-- Receiver's on_event
function(self, event)
    if event.type == "request" and event.source then
        World.send_event(event.source, "accepted", event.amount)
    end
end
```

`event.source` is the live sending Entity at ordinary event delivery, or **nil**
if absent/destroyed. Handle identities include World and generation; a reused
slot is not substituted. `event.other` retains its existing physics participant
meaning and representation. `event.amount` and `event.type` are unchanged.
Timers identify their owner as source; physics events have no invented initiator.
The existing UI-event source contract remains unchanged.

Delivery remains asynchronous. Update takes a queue snapshot; replies and
self-sent events from a handler wait until a later Update. The 1024-entry queue
limit and callback instruction budget remain in force. A chain may continue
over frames, but cannot recursively execute synchronously. Send to a destroyed
target is safely ignored/skipped. No RPC/correlation protocol is added.

Gameplay Event History already records sender/receiver snapshots, numeric
payload, delivery status and handler errors; the new Lua field uses that same
stored source. History can retain a destroyed sender's snapshot while the live
Lua event source is nil.

A player Behaviour can compose movement, weapon and health modules through
explicit self sub-tables; autonomous entities can communicate through events.
Ownership of camera/input/movement stays with the existing Behaviour. Multiple
Behaviour components on one Entity are outside this stage.

## Editor and diagnostics

Use the existing Asset Browser `.lua` entry and Script Document tabs to open a
module without attaching it to an Entity. The built-in view is read-only; use
Open in Visual Studio to edit/save and Reload file to refresh the preview.
During Play, saving a known module triggers Runtime dependency reload.

Import errors include the root/dependency chain, appear in existing root load
errors/Lua Inspector, and are also available from Error(moduleResourceKey) via
dependent roots. No second editor or schema requirement for plain modules is
introduced.

The existing static dependency audit now translates literal Script.require calls
to resource keys for safe Rename/Delete blockers. Dynamic calls generate risk
diagnostics. It does not execute source or rewrite imports. Aliases/reflection
cannot generally be proven statically; prefer direct literal imports.

## Implementation files

- `engine/scripting/LuaRuntime.cpp`: nested budget, lifecycle integration,
  instance module context, source delivery, dependency diagnostics.
- `engine/scripting/LuaModules.inl`: private graph/cache/validation/reload code
  included in existing Impl, without introducing another Lua VM lifecycle.
- `engine/scripting/LuaModulePath.h`: shared import-name validation/mapping.
- `engine/editor/LuaAssetDependencies.h`, `AssetCatalog.cpp`: literal/dynamic
  import dependency audit.
- `engine/editor/EditorDocuments.cpp`: existing Lua-source workflow hint.
- `tests/LuaModulesRegression.inl`, `LuaScriptingRegression.cpp`: isolated owned
  fixtures integrated into the existing regression target.
- `tests/AssetFileOperationsRegression.cpp`: real dependency audit, Rename/Delete
  protection for modules without an Entity attachment and dynamic-import risk.

## Validation

Release build of WhispEngine and the related regression targets succeeded.
After the final source-read/override safeguards, the focused run passed **5/5**:
LuaScriptingRegression (**247 checks**), GameplayEventsRegression (36),
LuaGameplayApiRegression (100), LuaInspectionRegression (59) and
AssetFileOperationsRegression. Log: `out/lua2-last-core-tests.log`.

The final native DX12 rerun also passed **3/3** after those safeguards:
LuaPhysicsEventsDx12Regression, GameplayEventsDx12Regression and
LuaGameplayApiDx12Regression (42.75 seconds). Each asserts zero DX12 validation
errors. Log: `out/lua2-final-dx12-tests.log`.

Module cases cover repeated/nested imports, table identity, per-entity closure
state, cycle/depth/path/size rejection, sandbox, module exceptions, preservation
of the outer instruction hook, explicit/automatic transitive reload, whole-group
rollback/recovery, missing-file creation and preservation of live instances when
a changed schema rejects serialized overrides. Event cases cover replies and
amount, dead/reused source handles, asynchronous self-reply chains and the
existing 1024-event cap. Fixtures use owned scratch directories, not game projects.

The broader related run passed **17/18**, 124.24 seconds, using a temporary clean
layout with exact preference-byte restoration. Passed: Physics, CharacterMovement,
LuaPhysicsEvents CPU/DX12, UI4, InteractiveUI, RuntimeUI, EditorDocument,
LuaInspection, GameplayEvents CPU/DX12, LuaGameplayApi CPU/DX12,
LuaGameplayController CPU, CharacterMovement DX12, LuaScripting and
AssetFileOperations. Log: `out/lua2-final-tests.log`.

**Unresolved native camera test:** LuaGameplayControllerDx12Regression fails at
`Edit RMB captures free-fly`; a clean-layout repeat also reached and failed at
`Play without player retains RMB free-fly`. For comparison, a temporary executable
was built using the exact saved LuaRuntime.cpp from before Architecture 2.0,
with the remaining engine/test unchanged. That baseline also failed at
`Edit RMB captures free-fly` (`out/lua2-baseline-test.log`). This comparison did
not reset or replace working-tree source files. The camera/input assertion was
not weakened and the unrelated editor input path was not changed. A fully green
18/18 native run is not claimed.

RuntimeUI's initial pixel Undo failure disappeared with a clean layout. The
original preference files were restored in finally blocks; no user's layout
reset is part of the implementation. `git diff --check` passed and tracked
engine assets remain unchanged. Initial CMake/icon/PlayerComponent changes were
present before this task and were not reverted or included in the Lua design.

## Remaining limitations / next stages

No crouching, physical controller changes, multi-Behaviour ECS migration,
universal RPC, or concrete game modules are implemented. These exclusions are
intentional. Timestamp/size-preserving external file edits are not detected by
the metadata poll; use explicit ReloadScript in that case. No automatic import
rewriter or new writable built-in Lua editor is included.
