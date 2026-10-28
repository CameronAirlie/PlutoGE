# RocketLeg

RocketLeg is a local two-player car-football sample for PlutoGE: timed matches
with kickoffs, boost, aerials, dodges, supersonic demolitions and golden-goal
overtime, in a lit stadium with a native-UI HUD. All art is generated
(`generate_arena.py`) or built in, so no external assets are required.

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
Tap jump for a hop; hold it for up to 0.2 s to jump higher. Throttle still
nudges the car forwards in the air. Each player's boost gauge shows flip
availability/countdown, powerslide, supersonic and demolition state.

### Handling

- **Turning** follows a curvature that is tight at low speed (2 m radius when
  nearly stopped, about 3 m at 8 m/s, 7.5 m at top drive speed, 13 m at full boost). The car carves a line instead of
  spinning out, and powerslide tightens the turn while the rear slides.
- **Throttle** pulls hard until close to top speed (23 m/s), then fades. Throttle
  against the motion brakes strongly; letting go coasts down. Boost reaches
  36 m/s; supersonic starts at 33 m/s.
- **Air control** accelerates pitch, yaw and roll directly. Holding an input
  keeps rotating up to 5.5 rad/s; releasing pitch or yaw stops the car where it
  points. Roll always settles.
- **Dodges** push 9 m/s along the car's heading flattened to the ground, so a
  nose-up car still dodges forwards. Side dodges gain up to 90% more at top
  speed. A dodge cancels vertical speed and holds the car level for 0.3 s. It
  spins at a fixed rate (about one turn) for 0.65 s, then hands back to air
  control. Holding the stick against a forward or backward dodge pushes back on
  its flip (harder with more stick) until the spin stops, leaving the dodge's
  speed. This is a flip cancel.
- **Gravity** in the stadium is 15 m/s² for cars and ball (`Core/Gravity.cs`).
  The engine supplies 9.81 m/s² and each body adds the rest.
- **Stuck on the roof or a side?** Press jump, or just wait a second: the car
  hops and rolls back onto its wheels.

All keyboard and controller bindings are configured in
`Assets/Input/RocketLeg.plutoinput`. The car, camera, and match scripts reference
that asset through an Input Mapping Asset field in the scene.

Press **R** (or Start) to restart the match and **H** (or Back) to show or hide
the controls panel.

The camera starts behind the blue car. Press **C** to toggle ball cam, and
**Tab** to switch the chase camera between the blue and orange cars. The field
of view widens while the followed car boosts or is supersonic.

## Match rules

- Matches last **5:00**. Each kickoff counts down 3-2-1 from one of five
  mirrored positions (two diagonal, two off-centre, one straight back), so both
  cars are always equidistant from the ball.
- The clock starts with the first touch after each kickoff. At 0:00 play
  continues until the ball next touches the floor.
- A tied match goes to **golden-goal overtime**: the clock counts up and the
  next goal wins.
- The whole ball must cross the goal line. The last car to touch it gets the
  goal (or an own goal). The ball bursts in the scoring team's colour, and cars
  can keep driving for three seconds before the next kickoff.
- Hitting an opponent nose-first at **supersonic** speed (33 m/s or more, shown by
  a white trail) demolishes it. The wreck respawns near its own goal after three
  seconds with a full tank.
- The end-of-match card shows each player's goals, demolitions and touches.

## Scene structure

- A panelled physics ball (3 m across) with a ground marker under it
- Two rigidbody cars with a generated low-poly body, glass, trim, spinning and
  steering wheels, a team-coloured boost flame and a supersonic trail
- An enclosed stadium with a 120 x 82 m floor, 134 x 96 m between the walls and
  30 m high, with 24 x 9 m recessed goals in tinted glass and team-coloured goal
  lights. `Scripts/Core/ArenaLayout.cs` holds the dimensions.
- Seven-metre concave floor/ceiling banks and 10 m rounded plan-view corners
- Team-tinted field halves, field markings, and emissive trim strips along each
  team's half of the walls
- Third-person chase/ball camera and directional/point lighting
- Native-UI HUD (see below)

All gameplay tuning values are serialized fields and can be changed in the
Inspector after selecting a car or the Match Manager.

## Boost and stadium

Each car starts with 100 boost. Holding boost spends 25 points per second, even
at the speed limit; an empty tank supplies no thrust. Six **big pads** (corners
and midfield sides, with a floating orb) fill the tank and recharge after 10 s;
twenty-four **small pads** add 12 and recharge after 4 s. Pads dim when collected and
recharge only during live play. Full cars leave pickups for the other player.
Kickoffs, rematches and respawns refill the tank; kickoffs also reset all pads.

Car touches give the ball an extra "hit assist" impulse proportional to the
closing speed, aimed partly along the car's travel. A hit's power therefore
depends on how fast the car arrives rather than on the car/ball mass ratio.

Cars ride on four raycast spring/damper contacts. A smaller, low-friction chassis
box handles car/ball impacts while the suspension keeps the belly clear of the
track. Visible wheels follow the suspension travel, and the front wheels turn.
The scripts never overwrite the cars' or ball's Collider and Rigidbody values:
edit size, centre, mass, drag, friction and bounce on the components. The car
reads its collider box for rotational inertia. It warns if Angular Drag is above
0 or Friction above 0.2, since its handling relies on both staying low.
Steering drives yaw towards the curvature target, and authority scales with the
number of front tyres in contact, so a car on its rear wheels alone cannot turn.
Tyre grip acts along the chassis axes and only removes side-slip. Powerslide
reduces rear grip more than front grip, letting the rear slide while the front
holds the line. The pure handling maths is in `Car/CarHandling.cs`.
Surface alignment and curvature support carry the car through banks and walls.
Ceiling-facing surfaces release suspension and alignment: the car retains its
momentum and **falls from the ceiling** under gravity.
Jumping pushes away from that surface and temporarily disables suspension, so
wall jumps detach cleanly. Airborne cars retain gravity and manual air control.
These are genre-style arcade controls with their own tuning, not a
reproduction of any particular game's vehicle physics.

The stadium shell is a thin triangle mesh. A body whose centre crosses it,
such as a car crawling over the roof edge, would be pushed out of the wrong side.
`ArenaLayout.TryContain` describes the shell's profile, and the car and ball use
it as a safety net to return to the inside. A solid slab above the roof backs
the mesh up.

The engine now sizes continuous-collision sweep spheres to fit the actual
collider and centre of mass, including thin box chassis. Rebuild/restart the
editor after updating the engine. Suspension, steering, and adhesion settings
are exposed on each car's script in the Inspector.

The stadium uses the engine's existing static Mesh collider
support (Bullet concave triangle meshes), sharing geometry with the visuals.
Run `python samples/RocketLeg/generate_arena.py` to regenerate every procedural
mesh (stadium, field lines, trims, car, ball) and the generated scene entities:
stadium pieces, pads, effects, and the car and ball visual rigs. Generated
entities carry the `generated` tag and are replaced on each run. Everything else
(cars, ball, match, camera, HUD, your own additions) is kept with its tuning.
Missing materials are created, but existing ones are never overwritten.

## HUD

The HUD is built in code with the native UI (`Scripts/UI`) on the `HUD`
entity's canvas. Its look comes from `Assets/UI/Themes/RocketLeg.plutotheme`,
so colours, fonts and corner radii can be restyled in the UI Theme Editor
without touching code. It shows:

- the scoreboard and clock (with KICKOFF / OVERTIME / FINAL captions)
- a radial boost gauge per player, with a car status line
- centre banners for the countdown, GO, goals and own goals
- an event feed for goals and demolitions
- the controls panel, which closes after ten seconds of play or on **H**
- the end-of-match card

`RocketLegHud` only presents state. It polls `RocketLegMatch.Snapshot` and
subscribes to its `PhaseChanged`, `GoalScored` and `Demolition` events.

## Scripts

| Folder | Responsibility |
|---|---|
| `Car` | `ArcadeCarController` (physics, input, boost, jumps, supersonic, demolition) and `CarVisuals` (wheels, flame, trail, burst), which only reads car state |
| `Ball` | `RocketBall` (limits, hit assist, last touch), `BallMarker`, `GoalExplosion` |
| `Arena` | `BoostPad` |
| `Match` | `RocketLegMatch` (rules and clock), `KickoffLayout` (mirrored spawns), shared match types |
| `Camera` | `RocketChaseCamera` |
| `UI` | `RocketLegHud` presenter, one view class per widget, and the `Kit` node/screen helpers |

### Driving regression checks (Windows GCC build)

Run `powershell -ExecutionPolicy Bypass -File samples/RocketLeg/Tests/run.ps1` from the repository root
after building the engine. This runs the production car controller in a headless
Bullet host against the actual stadium GLB triangles. It checks suspension ride
height, boosted/unboosted wall driving at 60/120 Hz, ceiling release, rounded
corners, wall jumps, directional flips and expiry, three-wheel floor/ball resets,
front-wheel steering, powerslide, airborne gravity, boost limits, variable jump
height, supersonic entry/exit, demolition respawn, carving at top speed without spin-out,
dodge stall and flip cancel, air-control settling, shell containment, stadium gravity and
turtle righting. Engine CCD sizing has a separate
`PlutoGEColliderCcdTests` CTest target.
