# RocketLeg

RocketLeg is a local two-player car-football sample for PlutoGE. It uses
built-in meshes, generated stadium meshes, project materials, Bullet rigidbodies, and documented C#
scripting APIs, so no external art is required.

## Play

1. Build `RocketLeg.Scripts.csproj` (or use **Runtime -> Build Scripts**).
2. Open `RocketLeg.plutoproject` in PlutoGEEditor.
3. Press **F5**.

### Controls

| Player | Drive / steer | Air pitch / yaw | Air roll | Boost | Jump / flip | Powerslide |
|---|---|---|---|---|---|---|
| Blue | W/S and A/D | W/S and A/D | Q/E | Left Shift | Space | Left Ctrl |
| Orange | Arrow keys | Arrow keys | Comma/Period | Right Shift | Enter | Right Ctrl |

Gamepads are assigned by player: controller 1 controls Blue and controller 2
controls Orange. Right trigger drives forwards and left trigger brakes/reverses.
The left stick steers left/right and pitches up/down in the air. Hold left bumper
to powerslide on the ground or change stick left/right from yaw to free air roll
in the air. A jumps, B boosts, and X air-rolls right.

Press jump a second time within **1.5 seconds** of leaving contact: neutral input
performs a double jump; stick/WASD direction performs a forward, backward, side,
or diagonal flip. Holding jump does not repeat it. The second jump/flip is spent
once used and is restored when **at least three of the four wheels** touch a
surface or the ball. Two wheels or a body-only ball collision cannot reset it.
The RML gauge shows flip availability/countdown and powerslide state.

All keyboard and controller bindings are configured in
`Assets/Input/RocketLeg.plutoinput`. The car, camera, and match scripts reference
that asset through an Input Mapping Asset field in the scene.

Press **R** to restart the match. The first player to five goals wins.

The camera starts behind the blue car. Press **C** to toggle Rocket-style ball
cam, and **Tab** to switch the chase camera between the blue and orange cars.

## Scene structure

- A physics ball and two rigidbody arcade cars
- An enclosed 80 x 52 arena, 22 units high, with recessed blue/orange goals
- Five-unit concave floor/ceiling banks and rounded plan-view corners
- Opaque floor and lower banks; translucent walls, upper curves, ceiling, and goals
- Third-person chase/ball camera and directional/point lighting
- Document-backed RmlUi HUD for scores, status, controls, and the followed car's boost tank
- Script-driven scoring, kickoff freeze, resets, boost, jump, and recovery

All gameplay tuning values are serialized fields and can be changed in the
Inspector after selecting a car or the Match Manager.

## Boost and stadium

Each car starts with 100 boost. Holding boost spends 25 points per second, even
at the speed limit; an empty tank supplies no thrust. Gold floor pads restore
35 points (capped at 100), dim when collected, and recharge after six seconds of
active play. Full cars leave pickups available for the other player. Kickoffs,
rematches, and car recovery refill the tank; kickoffs also reset all pads.
The bottom-right RML gauge follows the camera selection when pressing Tab.

Cars ride on four raycast spring/damper contacts. A smaller, low-friction chassis
box handles car/ball impacts while the suspension keeps the belly clear of the
track. Visible wheels follow the suspension travel, and the front wheels turn.
Steering comes from forces at those front tyre contacts, rather than a yaw-rate
motor. Powerslide reduces rear grip more than front grip, letting the rear slide
while the fronts still guide the turn. Releasing it restores normal traction.
Surface alignment and curvature support carry the car through banks and walls.
Ceiling-facing surfaces release suspension and alignment: the car retains its
momentum and **falls from the ceiling** under gravity.
Jumping pushes away from that surface and temporarily disables suspension, so
wall jumps detach cleanly. Airborne cars retain gravity and manual air control.
These are Rocket League-inspired arcade controls, not a reproduction of its
proprietary vehicle physics.

The engine now sizes continuous-collision sweep spheres to fit the actual
collider and centre of mass, including thin box chassis. Rebuild/restart the
editor after updating the engine. Suspension, steering, and adhesion settings
are exposed on each car's script in the Inspector.

The stadium uses the engine's existing static Mesh collider
support (Bullet concave triangle meshes), sharing geometry with the visuals.
Run `python samples/RocketLeg/generate_arena.py` from the repository root to
regenerate the GLB meshes and stadium entities. The generator preserves gameplay
entities and their tuning while replacing stadium geometry and boost pads.

### Driving regression checks (Windows GCC build)

Run `powershell -ExecutionPolicy Bypass -File samples/RocketLeg/Tests/run.ps1` from the repository root
after building the engine. This runs the production car controller in a headless
Bullet host against the actual stadium GLB triangles. It checks suspension ride
height, boosted/unboosted wall driving at 60/120 Hz, ceiling release, rounded
corners, wall jumps, directional flips and expiry, three-wheel floor/ball resets,
front-wheel steering, powerslide, airborne gravity, and boost limits. Engine CCD sizing has a separate
`PlutoGEColliderCcdTests` CTest target.
