# Lua Gameplay Architecture 2.0 — preliminary audit

Status: audit/design only. The APIs below are proposals, not implemented APIs.
Existing user changes to LuaRuntime.cpp and PlayerComponent.h are preserved.

This is the historical pre-implementation audit. The implemented contract and
actual verification results are now in [LuaGameplayArchitecture2.md](LuaGameplayArchitecture2.md).

## Existing integration points

- LuaRuntime::Impl::Environment creates an explicit allowlist, with separate
  math/string/table/utf8 tables. package, io, os, debug, loadfile/dofile and pcall
  are not exposed. Evaluate accepts text chunks and requires a returned table.
- Load reads at most 256 KiB through SafeAsset and validates the definition/schema
  in a disposable VM. Successful load increments Program.revision. Sync retires
  old instances and starts replacements at a safe owner-thread boundary.
- ExecutionBudget installs a 200,000-instruction hook. Its current destructor
  clears that hook unconditionally: nested module evaluation must not reset or
  disable the outer execution budget. This must be corrected before imports.
- Each Instance owns an environment, definition and self table. ScriptComponent
  stores one scriptPath plus serialized field overrides and permissions. Multiple
  behaviours per entity would require ECS/schema changes and are unnecessary.
- World.send_event already captures its bound owner as Event.source. History
  already records source, target, other, numeric payload and delivery/error status.
  Update constructs on_event data with type/amount/other but omits source. UI events
  already have a source field. Timers construct their own event tables.
- Update snapshots the queue before delivering callbacks. Replies therefore run
  on a later update; the queue is bounded to 1024 entries. Preserve both rules.
- ReloadScript currently reloads one behaviour explicitly. ResourceManager's file
  watch loop does not currently provide a Lua module dependency graph. Automatic
  dependency reload is new work, not something that already works implicitly.
- AssetCatalog already recognizes .lua under scripts; Document Tabs opens .lua,
  and ScriptEditor resolves SafeAsset paths and opens Visual Studio for editing.
  The built-in source view is read-only. A second Lua editor is unnecessary.
- GameplayEventsRegression, LuaScriptingRegression, LuaGameplayApiRegression,
  LuaGameplayControllerRegression, LuaInspectionRegression and UI regressions
  provide existing infrastructure to extend.

## Minimal module design

Proposed import: `Script.require("gameplay.health")`, mapped strictly to
`scripts/gameplay/health.lua` inside the active asset root, using SafeAsset and
the project resolver. Validate nonempty identifier segments; reject slashes,
backslashes, empty/dot segments, NUL, absolute paths and root escapes including
symlinks. Apply the same 256 KiB limit to every source and bounded graph depth,
node count and total bytes. Do not expose the native package loader.

Separate immutable compilation/source caching from mutable module instances.
Cache compiled chunks by canonical key and content revision, but evaluate a fresh
module table per behaviour instance. Within that instance, repeated imports
return the same cached table. Nested imports use the same instance graph. Never
share tables/closures containing mutable game state across entities. Modules can
operate on self passed explicitly, preserving current Inspector visibility.

Use an active import stack for cycle detection and diagnostics containing the
full root -> module -> dependency chain. Preserve a single execution budget
across nested imports. Also bound graph expansion in C++, since recursion into
native import callbacks is not adequately bounded by Lua instruction count alone.

Program candidates must contain an immutable dependency snapshot. Validate the
entire candidate graph in a disposable VM before publishing it. A changed module
invalidates transitive dependent behaviours. Stage all affected candidates before
committing revisions; on any error retain old sources, module tables, revisions,
timers and running instances. Never retire live instances before validation.
Use existing safe lifecycle boundaries for successful replacement. Track missing
dependencies too, so creation/fix of a missing module can recover without restart.
Project switches must clear canonical-key caches, watches and dependency edges.

## Event contract

Proposed ordinary event.source is a live sender Entity at delivery, or nil when
absent/destroyed. Do not substitute event.other: physics participants retain their
existing meaning. History retains the enqueue-time identity snapshot even when
the live source dies. Replies use the existing asynchronous World.send_event;
no RPC, promises or new correlation fields are needed for this stage.

Test source identity, replies delayed to the next update, amount preservation,
legacy handlers, sender destruction/slot reuse, bounded self-reply chains, timer
and physics sources, handler failures and history status updates.

## Composition and crouching

One player behaviour may compose input/movement, a weapon module and health
module through explicit self state. Independent entities can use queued events.
Modules do not acquire extra Camera/Input/Character permissions; preserve the
owner-bound API and current permission checks.

PlayerSpawn.h already derives body half-height and eye height from PlayerStart.
The collider is an upright box, with world-sized halfExtents; PlayerComponent
only carries eyeHeight. ApplyCharacterMovement/GroundCharacter operate on the
existing Rigidbody and contacts. A camera offset alone is not physical crouching.
Defer crouching to a separate stage: explicit standing/crouched dimensions,
feet-preserving body-center adjustment, obstruction overlap test before standing,
physics broadphase/contact refresh, camera offset reconciliation and lifecycle
reset/serialization rules. Do not introduce an eye-height-only approximation.

## Required implementation gates

1. Module graph/cache/budget component and isolated import/security tests.
2. Transactional integration into Load, instance evaluation and dependency reload.
3. on_event source plus queue/history compatibility tests.
4. Existing editor .lua workflow and dependency diagnostics; avoid behaviour-schema
   inspection as a requirement for opening a plain module.
5. Release build and focused runtime/controller/input/physics/UI/Inspector tests.
6. Document actual APIs and results only after those checks pass.

No Architecture 2.0 runtime implementation or new test run has been completed by
this audit. DOOM67, gameplay assets, physics and existing API signatures are untouched.
