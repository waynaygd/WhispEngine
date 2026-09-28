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
