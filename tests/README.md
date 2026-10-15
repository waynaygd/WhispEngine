# Physics regression scenarios

Run from an x64 Visual Studio developer shell:

```powershell
cmake --preset x64-release -DWHISP_BUILD_PHYSICS_TESTS=ON
cmake --build out/build/x64-release
ctest --test-dir out/build/x64-release --output-on-failure -V
```

`PhysicsRegression` runs the real ECS and PhysicsSystem without a renderer or
physics middleware. Assertions remain enabled in Release. The optional target
is disabled by default and does not change the application entry point.

Scenarios:

- Drop a cube, verify resting height and sleep, then check 20 seconds without drift.
- Let a tilted cube topple onto a face.
- Simulate the application's 0.18-unit, four-layer pyramid for 20 seconds at
  30, 60 and 144 FPS. Repeat with sleeping and both damping multipliers disabled.
- Fire a mass-2 cube at 14 units/second into each pyramid; verify rotation,
  bounded energy and subsequent settling of bodies remaining on the ground.
- Remove a sleeping cube's support, initialize an airborne sleeper, apply torque
  to a sleeping cube, and check that very slow unsupported fall never sleeps.
- Sphere-ground rest, sphere rolling down a rotated ramp, and sphere-sphere impact.
- Crossed box faces with no contained vertices, large dynamic boxes and a large
  static sphere (broadphase coverage).
- A 50-unit/second cube against a thin wall.
- World-space angular velocity with an initially rotated body.
- Repeated impacts of rotating cubes with both damping multipliers disabled;
  check that total kinetic energy does not exceed its initial value.

The solver uses 12 configurable velocity iterations and four position iterations.
Position correction changes poses, not velocities. Restitution targets are frozen
before warm starting; normal and two-dimensional friction impulses accumulate.
The warm-start cache matches local anchors one-to-one, checks entity generations
and geometry, scales impulses with timestep, and expires absent contacts.

Sleeping requires a quiet connected dynamic island, stable contact separation and
an upward support path to an immovable body for 0.75 seconds. Support impulses do
not reset the sleep timer. An awake colliding member wakes its whole island.

Limitations: adaptive substeps are bounded to 256 per update and are not swept CCD;
arbitrarily fast/thin configurations can still tunnel. These are numerical tests,
not a visual editor playthrough. Moving static geometry remains outside the
kinematic-body model. Collider offsets retain the existing world-space convention.

## Visual physics stress test

Open the `Physics Stress Test` panel. Choose 100, 250, 500 (default), or 1000 cubes
and press Create. The tracked demo is replaced; the stress floor and cubes share
one mesh and shader/default texture. Play starts/resumes physics, Stop pauses,
Restart recreates the initial layout, and Clear removes the stress entities.
Load Scene restores the normal demo. Serial/Parallel can be switched between
updates; small ranges retain the existing serial fallback. Close/reopen no workers
per object: the same enkiTS scheduler services all ranges and resource loads.

Reproducible headless checks (fixed 1/60 dt, real visual-scene ECS components):

```powershell
./out/build/x64-release/PhysicsRegression.exe --stress
./out/build/x64-release/PhysicsRegression.exe --stress-long
./out/build/x64-release/PhysicsRegression.exe --diagnose-runaway
```

`--stress` compares both modes for 600 frames at each size. `--stress-long` compares
500 cubes for 7200 frames (120 seconds of simulation per mode), printing timing
windows every 10 simulated seconds. Both verify identical poses/velocities,
finite positions, floor containment, pause/resume and cleanup. Default regression
also verifies repeated creation/restart, stale handles after Clear and discarded
job handles. ResourceRegression checks async cache reuse, finalization, missing
resource fallback and shutdown while decode is pending.

Run the actual renderer from its build directory (DXIL lookup requires this):

```powershell
Set-Location out/build/x64-release
./WhispEngine.exe --stress 500 --frames 300
./WhispEngine.exe --stress 500 --serial --frames 300
```

The frame limit cleanly exits; omit it for interactive testing. Visual runs use a bounded 60 Hz physics clock; compare deterministic headless
measurements for equal simulation histories. Editor/world changes and JobSystem APIs are owner-thread
operations. Workers read snapshots or write distinct body/pair slots; each stage
waits before dependent stages or editor mutations.

With ENABLE_TRACY=ON the ordinary Engine target links TracyClient and receives
TRACY_ENABLE. Connect Tracy 0.14.1 while Play is running (TRACY_ON_DEMAND).
Zones include PhysicsSystem, PhysicsStep, PhysicsIntegrate/Serial/Parallel/Job,
PhysicsBroadphaseSerial (both passes), PhysicsNarrowphase/Serial/Parallel/Job,
PhysicsSolverSerial, PhysicsPoseIntegrate/Serial/Parallel/Job and
PhysicsPositionProjectionSerial. Worker threads are named enkiTS worker N.
RenderSystem/RenderGather/RenderPrepare/RenderSubmit, DebugColliders, EditorLayer
and ResourceLoadJob/ResourceDecode separate non-physics work.

Stage times are accumulated wall times across all substeps; worker scheduling and
waits are included in parallel stages. Bodies/sleeping are sampled at update start;
pairs/manifolds/contact points are peak counts across substeps. Projection time
excludes its broadphase, which contributes to Broadphase. Remaining total time
includes gathering, island/support/sleep work, cache maintenance and callbacks.

## Final stability diagnostics

From the build directory, append a CSV filename to `--benchmark`, `--stress`, or
`--stress-long` in PhysicsRegression to retain individual measured samples.
WhispEngine accepts `--diagnostics FILE.csv`. Rows include mode, phase, frame wall
time, physics wall time, live bodies, sleeping bodies, substeps, peak pairs/contact
counts, stage times, and physics Dispatch/Wait wall times. Dispatch/Wait overlap
the corresponding stage and must not be added to its time again. Samples are
buffered and saved after Run; a crash will leave the log but may leave no CSV.

`--stability-scenario FILE.csv` uses the same Application operations as ImGui:
Create 500/Play, Stop, resume, Serial, Parallel, Restart, Stop, Clear, recreate,
and a GLFW close request at frame 744. It checks entity counts after creation and
cleanup. It is an opt-in diagnostic mode, with the original physics settings.

```powershell
./tests/RunPhysicsStability.ps1
./tests/CapturePhysicsTracy.ps1
./tests/CapturePhysicsTracy.ps1 -SettledRun
```

The capture script needs the official Tracy **0.14.1** Windows CLI, by default
`out/diagnostics/tracy-tools/tracy-capture.exe`; override `-CaptureExecutable` if
necessary. `--wait-tracy` waits up to 30 seconds for a connection before Run and
returns an error if no profiler connects. Captures stay local under
`out/diagnostics`; open `.tracy` in the matching Tracy profiler. Export CPU zones:

```powershell
./out/diagnostics/tracy-tools/tracy-csvexport.exe out/diagnostics/engine-lifecycle.tracy
./out/diagnostics/tracy-tools/tracy-csvexport.exe -u out/diagnostics/engine-lifecycle.tracy
```

Additional zones `JobDispatch`, `JobWait`, `DX12BeginFrame`, `DX12Present`,
`DX12FrameFenceWait`, and `DX12WaitForGpu` distinguish CPU scheduling from
renderer/fence waits. These are CPU wall times, not GPU timestamp measurements.
The scripts overwrite their own diagnostic artifacts; close any manually running
engine/profiler first and avoid other CPU-heavy work during comparisons.

## Fixed physics timestep and Broadphase diagnostics

Physics is scheduled once per application frame, outside renderer/window loops.
It receives 1/60 s per tick, up to four ticks per frame. Excess whole ticks are
dropped and reported; the fractional remainder is retained. Internal adaptive
substeps remain unchanged. Play/Stop, scene creation/cleanup/restart and update
mode changes reset the clock epoch; the first subsequent frame delta is ignored.
Camera, editor and rendering remain frame updates. Render interpolation is
described below.

Physics Stress Test displays ticks/frame, last tick and summed physics/frame
times, backlog/dropped seconds, active bodies and Broadphase rebuild/reuse counts.
On frames without a tick, stage times and pair/contact counts are zero; body
counts remain current. CSV appends the same counters and buffer growth/AABB work.
Dispatch and Wait are overlapping measurements, not additional physics time.

Run from out/build/x64-release:

```powershell
./PhysicsRegression.exe
./PhysicsRegression.exe --stress counts.csv
./PhysicsRegression.exe --stress-long long.csv
./PhysicsRegression.exe --benchmark benchmark.csv
./WhispEngine.exe --stability-scenario lifecycle.csv
./WhispEngine.exe --fixed-state --stability-scenario lifecycle-fixed.csv
./WhispEngine.exe --stress 500 --frames 300 --slow-frame 50 1000 --diagnostics stall.csv
```

The regression suite compares 30/60/120/144 Hz visual cadence in both physics
modes using the production clock, including poses, velocities, sleeping and
collision events. Broadphase tests enable a quadratic AABB oracle, exercise
geometry edits, overflow bodies, entity generations and reuse after World.Clear.
The oracle is disabled during ordinary runs and benchmarks.

Tracy adds PhysicsFixedFrame and PhysicsFixedTick outside Render.
CapturePhysicsTracy.ps1 accepts -EngineExecutable and -Frames for saved baseline
executables; use matching Tracy 0.14.1 tools. The detailed measured results and
limitations are in PHYSICS_FIXED_TIMESTEP_REPORT.md.

## Render interpolation

Render Interpolation is enabled by default. Physics Stress Test includes its
ON/OFF checkbox, alpha, eligible dynamic-body count and Physics Debug Pose
checkbox. Default collider debugging uses the same interpolated pose as meshes;
Physics Debug Pose deliberately displays the true ECS pose.

History is captured around completed fixed ticks. Alpha is accumulator/(1/60);
rendering uses position/scale lerp and quaternion shortest-path SLERP. The ECS
Transform is never replaced with a visual pose. This adds up to one fixed tick
of visual delay. Stop/scene/update-mode transitions clear history. Inspector
and other edits between ticks automatically reset it. For teleport performed
inside a physics collision callback, change the Transform and call
Application::NotifyTeleport(entity) before returning.

Run RenderInterpolationRegression.exe from out/build/x64-release. It checks
mathematics, lifecycle and submitted MVP/debug matrices, and benchmarks identical
pose snapshots with OFF/ON/OFF/ON for 100/250/500/1000. PhysicsRegression also
checks render ON/OFF/toggling while comparing full physics states and collision
event order at 30/60/120/144 Hz.

WhispEngine supports --no-interpolation for a visual OFF comparison.
From repository root, ./tests/RunRenderInterpolationDiagnostics.ps1 records
controlled and ordinary DX12 Tracy profiles, size smoke tests, lifecycle and
stall checks. Matching Tracy 0.14.1 capture CLI is required at its existing path.
Artifacts remain local under out/diagnostics/interpolation; the script overwrites
its own output. Do not run another engine/capture concurrently. See
RENDER_INTERPOLATION_REPORT.md for measured costs and unperformed checks.

## GPU instancing (DX12)

Physics Stress Test now includes GPU Instancing (default ON) and real scene
submission counters. Compatible consecutive mesh/shader/texture packets share
an instanced draw with individual prepared model/MVP/tint. Unsupported shaders,
single objects and other backends retain the ordinary path. Statistics exclude
ImGui and asset uploads; capacity is pooled across frame slots.

Run GpuInstancingRegression.exe for common RenderSystem checks and
GpuInstancingRegression.exe --dx12 for actual GPU pixel equality and lifetime
checks. From out/build/x64-release, WhispEngine.exe --render-benchmark 500
render-500.csv compares OFF/ON/OFF/ON on one stopped scene. Ordinary runs accept
--no-instancing; physics remains active with --stress.

From repository root, ./tests/RunGpuInstancingDiagnostics.ps1 runs DX12 tests,
100/250/500/1000 comparisons with matching Tracy 0.14.1 capture tools, active
physics smoke and lifecycle. It overwrites its own local diagnostic outputs.
See GPU_INSTANCING_REPORT.md for exact commands, measured CPU costs, GPU memory
contract, Vulkan fallback and unperformed checks. GPU timestamps are unavailable;
reduced draw counts alone do not establish GPU speedup.
