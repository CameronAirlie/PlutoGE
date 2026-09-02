# RocketLeg

RocketLeg is a compact local two-player car-football sample for PlutoGE. It uses
only built-in meshes, project materials, Bullet rigidbodies, and documented C#
scripting APIs, so no external art is required.

## Play

1. Build `RocketLeg.Scripts.csproj` (or use **Runtime -> Build Scripts**).
2. Open `RocketLeg.plutoproject` in PlutoGEEditor.
3. Press **F5**.

### Controls

| Player | Drive / steer | Air pitch / yaw | Air roll | Boost | Jump |
|---|---|---|---|---|---|
| Blue | W/S and A/D | W/S and A/D | Q/E | Left Shift | Space |
| Orange | Arrow keys | Arrow keys | Comma/Period | Right Shift | Enter |

Gamepads are assigned by player: controller 1 controls Blue and controller 2
controls Orange. Right trigger drives forwards and left trigger brakes/reverses.
The left stick steers left/right and pitches up/down in the air. Hold left bumper
in the air to change stick left/right from yaw to free air roll. A jumps, B boosts,
and X air-rolls right.

All keyboard and controller bindings are configured in
`Assets/Input/RocketLeg.plutoinput`. The car, camera, and match scripts reference
that asset through an Input Mapping Asset field in the scene.

Press **R** to restart the match. The first player to five goals wins.

The camera starts behind the blue car. Press **C** to toggle Rocket-style ball
cam, and **Tab** to switch the chase camera between the blue and orange cars.

## Scene structure

- A physics ball and two rigidbody arcade cars
- A walled 44 x 28 arena with recessed blue/orange goals
- Third-person chase/ball camera and directional/point lighting
- Document-backed RmlUi HUD for scores, status, controls, and branding
- Script-driven scoring, kickoff freeze, resets, boost, jump, and recovery

All gameplay tuning values are serialized fields and can be changed in the
Inspector after selecting a car or the Match Manager.
