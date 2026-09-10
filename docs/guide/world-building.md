# Editor, assets, and worlds

[Manual](../README.md) · Previous: [First game](first-game.md) · Next: [Gameplay](gameplay.md)

## Scenes, entities, and transforms

A scene contains a hierarchy of entities. An entity supplies identity, tags,
activation, and a transform; its components supply rendering, collision, audio,
and behaviour. Disabling a parent affects its descendants. A mesh by itself
does not provide physical collision, and an editor viewport camera is not a game
Camera component.

`GameObject.Position` and `Rotation` are local to the parent; `WorldPosition` is
world space. Rotation uses Euler degrees. Forward is local `-Z`. Use a root with
unit scale for characters and move visual offsets into children where practical.
Non-uniform or zero ancestor scale can make physics, camera orientation, and
world-to-local operations difficult or invalid.

**Example: a reusable lamp.** Create a root named Lamp, a child with the lamp mesh,
and a child with a point light. Move the root to place the entire assembly; move
the light child to position the bulb. Add collision to the solid stand if players
must not walk through it. Turn off the root to disable the whole assembly.

## Daily editor workflow

Use the Hierarchy to select and parent, the Inspector to edit, the Content Browser
to manage assets, and the Console to diagnose import/script failures. With the
viewport focused, W/E/R select translation/rotation/scale and F frames selection.
Use local axes to slide a rotated door along its own frame, world axes to align
props to a room, and snapping to build modular walls without gaps.

Make one modular wall, duplicate it with Ctrl+D, and place the copies using the
same snap increment. Save a viewport bookmark named `Room overview` before
detailing the room. Use **Edit > Drop Selection onto Ground...** for loose props;
the target ground needs non-trigger collision. For repeated selections, consult
[multi-entity editing](../MULTI_ENTITY_EDITING.md).

Undo/redo covers serialized scene edits but not every shared asset-file write.
Save materials and other asset editors explicitly. Autosave keeps recovery scene
backups, including untitled scenes; it does not back up unsaved external scripts
or texture pixels. Learn [recovery and undo coverage](../EDITOR_WORKFLOWS.md)
before relying on them for a large level.

## Importing models and textures

Import source files through the Content Browser/import workflow, then inspect
the generated assets before populating a level. The model pipeline handles
FBX and glTF/GLB and creates mesh, material, and animation assets. Keep source
textures available during import, especially when a model references separate
image files. Project-owned generated assets use `project://`; built-in primitives
use references such as `engine://builtin/mesh/cube`.

**Example: import a crate.** Place one imported crate beside the first-game Player.
Check its scale, orientation, pivot, material assignments, and texture appearance.
Give it a Box collider sized to the actual crate. Only duplicate or prefab it
after that test. For a physics crate, add a non-kinematic Rigidbody and check that
it settles on the floor. Avoid marking a moving object as static.

Use the Mesh Editor to inspect mesh/submesh data and the Material Editor to tune
appearance. Imported render geometry and collision geometry serve different
purposes; use simple collision when it describes the gameplay shape adequately.
LOD reduces distant geometry cost, but inspect silhouette changes and transitions
from the actual game camera.

Save before **Find References...** on an asset. The search examines saved content,
so unsaved edits and references computed in C# are not a complete dependency map.
See [asset reference search](../EDITOR_WORKFLOWS.md#find-asset-references) before
removing assets based on an empty result.

## Prefabs and variants

A prefab stores a reusable entity tree. Keep related objects, components, and
internal references together so spawning one prefab creates a complete object.
Prefab asset changes are shared; scene instance overrides specialize a copy.

**Example: enemy family.** Author a basic Enemy hierarchy with mesh, collider,
body, and scripts, then save it as a prefab. Instantiate it, change serialized
speed or appearance, and choose **Create Variant** on its root to create a FastEnemy
variant. Changing the base can then update common properties across the family.

Use **Update From Prefab** to resolve the latest base while retaining local
overrides, and **Revert Instance Overrides** to return to inherited values.
**Apply To Prefab** writes the shared asset. Scene undo does not undo that shared
file write. Variants support property changes; structural hierarchy/component
changes belong in the base. See [variant restrictions](../PREFAB_VARIANTS.md).

## Terrain

TerrainComponent generates a heightfield surface. Width/depth control sample
counts, Cell Size controls horizontal sample spacing, and Height Scale controls
vertical height scale. Chunk Size partitions the surface and LOD Count controls
available detail levels. Start small so sculpting and collision are easy to test.

**Example: outdoor test patch.** Begin with 65 by 65 samples and Cell Size `1`.
That describes 64 intervals across each axis. Assign a material, sculpt a low
hill with a broad, low-strength brush, and flatten a spawn area. Save the scene
and any externally authored heightmap/painted texture assets. Add a Collider
using Terrain shape on the terrain entity for gameplay collision.

Test the slope with your real controller. A rendered hill is not proof that
collision is available, nor that navigation considers the hill walkable. Increase
sample density only where the shape needs it; more samples increase geometry,
editing, serialization, and collision work. Recheck navigation after reshaping.

## Foliage

FoliageComponent places many mesh instances without one entity per plant. Add it
to the terrain entity, create a foliage type, assign a mesh/material, enable
painting, and paint a small patch first. Tune size, density, and draw distance
from the player camera before covering a map.

**Example: a forest edge.** Use a grass type with collision disabled and a tree
type with capsule collision enabled. Fit the tree capsule to the trunk, leaving
branches out, and start with Cell Size `32`. Paint several trees, then walk and
raycast between them. A multi-mesh tree prefab can be converted with **Export as
Static Mesh** before assignment. This is for static content, not skeletal meshes.

Foliage collision is configured per type. Current instances are not independent
scriptable tree entities, and foliage collision does not automatically create
navigation obstacles. Use ordinary prefabs for trees that need independent
destruction logic. See [foliage settings and limits](../FOLIAGE.md).

## Splines and roads

SplineComponent uses control points to generate a strip of geometry and optional
collision. Width and Thickness shape the strip; Closed joins its ends. Render
sampling and collision sampling are separate, so a smooth visible bend can still
have coarse collision. UV Meters Per Tile controls texture repetition along it.

**Example: a test road.** Add a spline, place several points across a flat terrain
patch, disable Closed for an open road, and assign a road material. Enable mesh
and collision generation. Start with a broad bend, drive or walk over it, then
increase collision sampling if contacts reveal angular steps. Lower chord-error
and tangent-angle tolerances where curves need finer approximation; inspect the
cost before applying dense sampling to a long road.

Keep generated road geometry above the ground enough to avoid overlapping
surfaces flickering. Spline collision is not an AI route: bake navigation or
implement a route-following behaviour separately.

## Navigation meshes and agents

NavigationMeshComponent owns walkability data; NavAgentComponent supplies native
path following. Bake a mesh over the level's intended walking area before testing
an agent. Agent dimensions must match the character: a large radius should reject
narrow gaps instead of letting the rendered character intersect walls.

**Example: guard crossing a courtyard.** Place a Navigation Mesh entity covering
the courtyard, configure its bake settings, and bake it. Add Nav Agent to a guard,
assign the navigation-mesh entity and a destination/target, and enable navigation
on start. Start with Speed `3.5`, Radius `0.5`, Height `1.8`, and Stopping Distance
`0.15`, then tune for your character. These are current config defaults, not a
guarantee they fit a particular imported model.

If the guard does not move, check mesh coverage, bake availability, a reachable
destination, agent clearance, and active components. Do not simultaneously drive
the same entity with a custom movement script. Navigation does not promise that
all changing obstacles or foliage will be baked automatically.

For scripted queries, use `Navigation.ProjectPoint` and `Navigation.FindPath`
with the specific navigation-mesh GameObject. A returned path is data: your
script must follow its waypoints. Check `Complete` before using `Points`, and
repath on a timer or meaningful target movement rather than every render frame.
The [managed navigation reference](../CSHARP_SCRIPTING.md#navigation) contains a
query example. Native NavAgent authoring does not imply a public managed
`NavAgentComponent` wrapper exists.
