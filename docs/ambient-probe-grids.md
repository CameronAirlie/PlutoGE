# Ambient probe grids

## Sphere debug view

Open **Window > Tools > Debug Drawing...** and enable **Diffuse probe spheres**
and/or **Reflection capture spheres**. The Scene viewport draws depth-tested spheres:
diffuse spheres evaluate that probe's stored SH in the sphere-normal direction;
chrome spheres sample that capture's HDR cubemap. They bypass direct lighting and
cross-volume blending so each source can be inspected independently, while retaining
the viewport's normal exposure/tone mapping. Adjust sphere radii and reflection
roughness in the same window. **Rejected probes (red)** shows cells excluded from
the bake. The diffuse display limit defaults to the nearest 512 eligible probes.

These previews are editor-only, supported by both RHI backends, and excluded from
Game view, capture/bake inputs, shadows, voxel GI and scene serialization. Walls
occlude the spheres; move the editor camera inside the room to inspect its probes.

## Baking

Create **Ambient Light Volume** from the hierarchy's Create menu, or add the
component to an existing object in the inspector. Position, rotate or scale the object and edit
**Size** to define its bounds; the viewport draws the box. The bake and probe interpolation follow the
transformed box, including rotation and inherited parent transforms. **Bake Volume** bakes that
component; **Clear Bake** clears only that component's data. Baked probes are stored
on the component and travel with scene saves, entity copies and prefabs. Moving, rotating or
resizing the box invalidates its old bake until rebaked.

**Scene > Lighting > Bake Ambient Grid...** bakes all enabled, active volume components. If none
exist, the first bake creates an **Ambient Light Volume** object fitted to static
geometry. Old scene-level grids are migrated into this component when loaded.
This action does not generate UV2 or replace lightmaps. Mark bake geometry **Static**.

Vulkan/OpenGL RHI supports eight active baked volumes. In overlaps, the volume with
the smallest actual maximum cell dimension supplies ambient lighting. A small,
dense volume around stairs or a narrow passage can therefore override a coarse
building-wide volume. Probe debug spheres include all active volumes. Reflection
captures retain their separate shared four-capture budget.

Start with **Probe Spacing = 1**, **Direction Count = 128**, **Probe Budget = 4096**.
For tight gaps, use a small volume with spacing below the gap width where possible
(minimum 0.05 world units). A tightly bounded volume still has at least two cells
across a thinner axis. Resolution is limited to 128 per axis and 16384 total cells per
volume; the baker reduces resolution to fit the budget while balancing cell
spacing in world units across all axes. The inspector shows the actual baked
X/Y/Z spacing and warns when the budget or axis limit increases it. **Surface Clearance**
defaults to 0.02 and relocation is limited by the opposing wall, so probes stay
inside narrow free spaces. The shader caps receiver bias at 0.002 world units,
uses bilinear visibility weights and bounds hit-plane extrapolation near silhouettes.

Each valid probe stores nine RGB spherical-harmonic coefficients and an 8×8
octahedral map of ray distances. Vulkan and OpenGL RHI evaluate directional diffuse
lighting from eight neighbouring cells, reject invalid/occluded neighbours, weight
by the receiver normal, and normalize the remaining weights. Within the grid,
missing visible probes produce darkness rather than unoccluded global ambient.
New bakes retain geometric hit normals alongside ray distances. Visibility
reprojects each hit plane onto the receiver ray, keeping planar surfaces
consistently lit between coarse samples instead of imprinting the ray-map pattern.
Rebake grids created before this change to obtain surface normals. Older grids
remain readable and use approximate triangle reconstruction from ray endpoints.
Visibility is conservative and approximate: narrow openings and closely spaced
surfaces may need smaller spacing. The legacy OpenGL pipeline retains the older
RGB volume sampling path.

The ambient-only bake snapshots current lights (including lights marked dynamic),
emissive surfaces and the physical sky. Ordinary full-scene baking retains its
static-light selection. Probe surface lighting always traces occlusion, even when
a light disables real-time shadows. Directional light contributes bounced ambient
from visible lit surfaces; sealed rooms without emissive or local light sources
remain dark. Sky misses include broad atmospheric radiance; direct
lighting owns the solar disk. Retained HDR cubemap pixels are also supported as an
environment source. Geometry and lighting changes require another bake; this is
baked lighting, not continuously updated GI. The grid currently captures one diffuse
bounce, independently of the lightmap bounce-count setting.

With **Automatic Reflection Captures** enabled, baking also places sparse 128px HDR
captures at valid grid cells. Placement spreads the captures across the grid and
prioritizes uncovered box volume instead of corner distance. Bounds retain each
axis independently and use visibility hit planes to fit room dimensions, so long
rooms no longer collapse to small cubes. Bounds are approximate; use manual
captures for complex or adjoining rooms. The
**Reflection Capture Budget** is capped at four total local captures, shared with
manual capture components. Manual captures are preserved. Generated components
carry a serialized ownership flag; subsequent ambient bakes reuse those components
and update their locations. The editor processes one six-face capture per frame.

The grid, validity, SH coefficients, visibility and generated HDR capture pixels
are serialized with the scene. Existing saved scenes are updated after baking and
after the reflection queue finishes; save a new scene normally. Older RGB-only
probe volumes remain readable and are treated as isotropic lighting in RHI. Both
local reflection captures and the diffuse grid are disabled while capturing an
environment to prevent lighting feedback.

Regression coverage lives in `IblRenderingChecks.h` and `IblCaptureTests.cpp`:
directionality, visibility rejection, invalid cells, upload revision changes,
scene persistence, capture budgeting, emissive-room baking, closed-solid rejection,
physical sky and six-face linear HDR capture on both RHI backends.

### Large imported scenes

The bake snapshot respects each MeshComponent's selected submesh range and section
transform. Split imports such as Bistro share one source mesh across many objects;
unselected sections must not be duplicated into each object's bake snapshot.
Material configurations are snapshotted once per material. Fallback lightmap UV
geometry owns only the selected sections, preserving section indices and the shared
source; changing the selection restores the source if another section is needed.
Completed lightmap pixels are read from the written PFM files one at a time for
upload rather than retained for the entire bake.

For a read-only geometry stress check with a local SCGI checkout, run
`PlutoGEIblCaptureTests --bistro S:\PlutoGE\SCGI`. This uses the actual Bistro mesh
with one object per section, a neutral material, and a 4096-probe budget. It does
not load the authored scene materials or write any files in the SCGI project.

Ambient interpolation rejects probes behind the receiver surface so a bright probe
in the next room cannot light the back of a tunnel wall. Environment samples are
captured separately for each volume's Direction Count; a component override must
use sky radiance evaluated along its own ray directions. Rebake existing volumes
after changing the bake sampling implementation.

To inspect an authored scene without saving it, run
`PlutoGEIblCaptureTests --scene-bake S:\PlutoGE\SCGI S:\PlutoGE\SCGI\Assets\Scenes\bistro.plutoscene`.
This uses the project's materials, lights and component bake settings. Optional
`SceneBakeSettings::logProbeDiagnostics` reports each valid probe's sky, reflected
and emissive source luminance, escaped ray count and brightest sampled surface.
These diagnostics are disabled in normal editor bakes.
