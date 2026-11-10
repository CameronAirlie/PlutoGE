# Foliage and tree collision

PlutoGE foliage renders many copies of a mesh without creating one entity per
copy. Foliage types can optionally add simple static collision, making the same
system suitable for both non-colliding details such as grass and colliding
environment objects such as trees. Types can also sway in the scene's wind; see
[Foliage wind](FOLIAGE_WIND.md).

## Quick guide: paint colliding trees

If the tree is a prefab containing multiple static mesh components, export it
first:

1. Find the prefab in the Content Browser.
2. Right-click it and select **Export as Static Mesh**. The same action is also
   available in the selected asset's details area.
3. PlutoGE creates `<PrefabName>_Static.plutomesh` in the currently viewed
   Content Browser folder. If that name exists, a numeric suffix is added.
4. Assign the generated mesh to the foliage type. Every prefab mesh component
   appears as a submesh and retains its material.

The exporter includes enabled static `MeshComponent`s on the prefab root and
its active descendants. It bakes entity, mesh-offset, and submesh-offset
transforms into the generated vertices. Referenced materials remain shared;
inline material overrides are written as companion `.plutomaterial` assets.
Skeletal and animated-node meshes are rejected because the output is a static
mesh asset.

Then configure and paint the foliage:

1. Select an entity containing both `TerrainComponent` and
   `FoliageComponent`.
2. Drag the tree mesh or model from the Content Browser onto the inspector's
   type list. This adds a type named after it.
3. Open the type's **Collision** section and turn on **Capsule Collision**.
4. Set **Center**, **Radius**, and **Height** so the capsule surrounds the
   trunk. Keep the capsule tight and exclude branches and leaves.
5. Leave **Cell Size**, under **Rendering**, at `32` initially. Smaller cells
   make local edits and physics rebuilds more granular, while larger cells
   create fewer Bullet objects.
6. Click **Paint** and paint the trees onto the terrain.
7. Enter play mode and test movement and raycasts against the trunks.

Collision is configured independently for every foliage type. Leave it disabled
for grass, flowers, and other small decorative meshes.

## The foliage tool

The tool has three modes. **Paint**, **Erase** and **Select** are at the top of the
foliage inspector and in the viewport's Paint menu; clicking the active mode turns the
tool off. While the tool is on, a bar at the bottom of the viewport shows its settings
and the selected type, and the tool owns the mouse: the entity gizmo, terrain brush and
entity picking stand aside. It turns off when another entity is selected, the scene
changes or play starts.

| Input | Paint / Erase | Select |
|---|---|---|
| Click or drag | Paints or erases the selected type | Selects an instance; a click on nothing clears |
| Shift | Hold to swap Paint and Erase | Adds to the selection |
| Ctrl | | Toggles an instance |
| `[` and `]` | Change the radius | |
| W, E, R | | Move, rotate, scale gizmo |
| Delete | Does nothing, so the entity is safe | Removes the selected instances |
| F | | Frames the selection |
| Esc | Leaves the tool | Clears the selection, then leaves the tool |

The bar says why painting cannot work, such as a missing terrain or mesh. Painting needs
an enabled `TerrainComponent` on the same entity. Brush radius and density belong to the
editor session and are not saved; older scenes' brush records are ignored. Each foliage
type has its own **Scale Range** and **Growth**.

## Editing individual instances

Select instances in the viewport with **Select**, or in the list under the inspector's
**Instances** section, which stays responsive at any instance count. Double-click a row
to frame it. Selecting from the list turns on Select so the gizmo appears.

- **One instance** gets a gizmo on its own axes, and the fields show its transform in the
  entity's local space.
- **Several instances** turn about their shared middle. The fields show the primary
  (the last selected, with a yellow box). A field edit moves the others by the same
  amount and scales them by the same ratio, so a group keeps its arrangement. A
  non-uniform gizmo scale applies along each instance's own axes, so it is only exact
  for one instance.
- **Frame**, **Snap To Terrain**, **Delete** and **Deselect** act on the selection.

Each gesture is one undo step that stores only the instances it touched. A selection
holds instance IDs, so it follows instances as others are added or removed, and drops
any that undo or a brush removes. Picking tests triangles among the nearest instances
and falls back to the instance whose bounds the ray passes closest to the middle of, so
a click through a gap in a canopy still selects it. From C++, use
`FoliageComponent::FindInstance`, `SetInstances`, `RemoveInstances`, `RestoreInstances`
and `PickInstance`.

While a gizmo drags, the component's render commands rebuild every frame, so a component
with very many instances may drag slowly until local updates arrive
([environment milestone E06](dev/ENVIRONMENT_DESIGN_MILESTONES.md)).

## Painting strokes

The terrain brush paints one dab on press and additional dabs as the cursor moves.
Dabs are spaced by one quarter of the brush radius (at least 0.05 terrain-local
units). Holding the cursor still does not continue adding instances. **Density**
is the number of candidates per dab; terrain-edge candidates may be rejected.
The result follows the sampled cursor path rather than the number of rendered
frames. A discontinuous jump exceeding 256 dab intervals starts at the new cursor
position instead of filling the gap in one frame.

Each candidate samples its own local height and normal. Candidates outside the
terrain or with invalid surface data are skipped. Choose **Growth** per foliage type under its **Placement** section:
**Upright (World Up)** is the default for trees; **Terrain Normal** follows the
slope for grass and ground cover. Upright means world +Y even on rotated/scaled
terrain. Random yaw works with either option. This setting affects new instances;
existing instances keep their authored transforms. The choice persists with scenes
and prefabs; older data without the setting defaults to upright. Invalid hits and viewport/tool interruptions reset the distance
sampler so re-entry does not paint across the gap. Selection/type/mode changes
finish the current stroke and require mouse release before starting another.
Scene replacement and play transitions discard obsolete stroke state.

Stroke undo still stores whole-component instance snapshots; compact history and
local cache updates are planned in [environment milestone E06](dev/ENVIRONMENT_DESIGN_MILESTONES.md).
Existing scenes and serialized density values remain compatible. Old scenes are
not repainted or moved automatically.

## Rescaling existing instances

A type's **Scale Range** only affects new instances. To change foliage that is
already placed, open **Rescale Instances** under the inspector's **Instances** section,
choose a mode, and apply it:

- **Multiply** multiplies current scales, keeping each instance's size variation and
  proportions. Use this to make everything, say, 20% larger.
- **Set** gives every instance the same uniform scale.
- **Randomize In Range** gives each instance a uniform scale in the range; **Use Type's
  Range** copies the selected type's. The result depends only on the range and the instance's
  ID, so the same range always gives an instance the same scale.

**Selected Type** applies it to the selected foliage type, **All Types** to every type
on the entity, and **Whole Scene** to every foliage type on every entity in the scene.
Instances scale around their own pivots, so they stay where they are. Each application
is one undo step. Collision capsules are in world units and do not change; resize them
under the type's **Collision** section. From C++, use
`FoliageComponent::ApplyInstanceScaleEdit`.

## Meshes for foliage

Each instance draws its whole mesh, so a foliage mesh should hold one plant. Scanned
sets (Poly Haven's grass, shrubs and ferns, for example) usually ship several plants
laid out in a row in one file; imported as they are, every instance is a row of plants,
and its pivot is nowhere near most of them. Split such a set into one model per plant
before painting it. The submesh picker is not a substitute: it
keeps a submesh's offset from the original pivot. ThirdPersonProject's level tools do
the split with `"split": "spatial"` in an asset manifest (its `docs/LEVELS.md`).

## Data model

`FoliageComponent` owns the placed instances and a collection of foliage types.
Each instance contains:

- A persistent 64-bit instance ID
- Local position
- Euler rotation
- Scale

The ID remains stable when other instances are removed. Old scenes whose
instance records do not contain IDs receive unique IDs when they are loaded.

A foliage type holds its name, its placed instances, what the renderer needs at run time,
and a `FoliageTypeAsset` with all its authored settings:

- **Prototype:** mesh or model reference and identity, material override, mesh parts
- **Placement:** scale range and growth orientation for new instances
- **Rendering:** cell size, draw distance (`0` inherits the component setting), LOD
  distance scale
- **Collision:** whether it is enabled, and the local capsule's center, radius and height
- **Wind:** see [Foliage wind](FOLIAGE_WIND.md)

The component keeps what applies to all its types: cast shadows, static, the default draw
and shadow distances, and the finest LODs it renders and casts shadows from. New
components use every LOD (`MinRenderLod` and `MinShadowLod` of `0`).

## Foliage type assets

A `.plutofoliage` asset defines a plant once for any number of scenes, and is an
ordinary asset: it has its own type, icon and picker, it is scanned for references, and
its mesh and material cook with it.

- **Create one:** **Create > Rendering > Foliage Type** in the Content Browser,
  **Create Foliage Type** on a mesh or model, or **Save as Asset** on a type in the
  inspector, which writes to the project's `Foliage` folder and links the type.
- **Use one:** drop it on the inspector's type list, or choose it under **Link to
  existing asset**.
- **Edit one:** double-click it in the Content Browser, or click **Edit Asset** on a
  linked type in the inspector. Both open the **Foliage Editor** (also under **Window >
  Asset Editors**).

The Foliage Editor shows the mesh's thumbnail, the types in the open scene that use the
asset, any setup problems, and the same settings sections as the inspector. It has undo
and redo (Ctrl+Z, Ctrl+Y) and asks before discarding unsaved changes.

- **Live** shows unsaved changes on every linked type in the open scene, without touching
  the file. Turn it off if a slider feels slow on a very large scene: each change rebuilds
  the render commands of the components involved. The preview pauses while the game runs.
- **Save** (Ctrl+S) writes the file and updates every linked type. Revert, closing without
  saving, or turning Live off puts them back to the saved settings.
- **Used In This Scene** lists the linked types. Click one to show it in the inspector, or
  its brush to start painting it. **Add to \<entity\>** adds a linked type to the selected
  entity's Foliage component.

The thumbnail shows the mesh's own materials, so it does not reflect a Material Override.

A linked type's settings are read-only in the inspector, so the file is the single
source of truth. **Make Local** drops the link and keeps the current settings in the
scene. The type's name and placed instances always belong to the scene. If the file
cannot be read, the type falls back to the copy of its settings the scene stores and the
inspector says why.

The format is text:

```text
PLUTOFOLIAGE	2
MESH	project://Trees/Oak.plutomesh
MODEL_OBJECT	12345	oak-model-id
MATERIAL	project://Materials/Bark.plutomaterial
SUBMESHES	1	3
GENERATED_LODS	0
SCALE_RANGE	0.8	1.2
ALIGN_TO_NORMAL	0
CELL_SIZE	32
MAX_DRAW_DISTANCE	60
LOD_DISTANCE_SCALE	1
COLLISION_ENABLED	1
COLLISION_CENTER	0	1.5	0
COLLISION_CAPSULE	0.4	3
WIND_ENABLED	0
WIND_RESPONSE	0.5	0.5	1	0
PROXY_PREFAB	project://Nodes/Oak.plutoprefab
```

In version 2, an omitted record takes its default. Version 1 files hold only the rendering,
collision and wind records: a type linked to one keeps the mesh and placement settings
from its scene, and files still load unchanged. Saving writes version 2.

From C++:

```cpp
bool SaveFoliageTypeAsset(const std::string& path,
                          const FoliageTypeAsset& asset,
                          std::string* errorMessage = nullptr);

bool LoadFoliageTypeAsset(const std::string& path,
                          FoliageTypeAsset& asset,
                          std::string* errorMessage = nullptr);

foliage->SetTypeAssetReference(typeIndex, "project://Trees/Oak.plutofoliage");
scene->RefreshFoliageTypeAsset("project://Trees/Oak.plutofoliage");
```

## Interactive foliage (proxies)

Games usually want every tree to be choppable and every bush harvestable, without
paying for thousands of entities. A foliage type can name a **proxy prefab**: the
entity one of its instances becomes when gameplay needs it to behave like an object.

1. Make the prefab: the gameplay object for one plant, with its pivot at the base,
   the plant's mesh at scale 1, a collider and the scripts that handle it (for a
   tree, say, a script that takes axe hits and falls). It spawns with the
   instance's position, rotation **and scale**, so build it the size of an unscaled
   instance.
2. Open the type's **Interaction** section, in the inspector or the Foliage Editor,
   and choose the prefab under **Proxy Prefab**.
3. Leave **Capsule Collision** on, so rays and weapons can find the instance. The
   setup checks warn when a type has a proxy but no collision.

At run time:

- **Promoting** an instance removes it from the foliage and spawns the proxy where
  it stood. The proxy's scripts have run `OnCreate` by the time the call returns,
  and its `FoliageProxyComponent` records which instance it replaced. Rendering and
  collision drop the instance at once.
- **Releasing** the proxy puts the instance back exactly as it was and destroys the
  proxy. Destroying a proxy without releasing it removes the plant for the rest of
  the session.
- Only the plants being interacted with are entities. The rest stay instanced, with
  cell collision and cheap rendering.

From C#, the hit of any physics query carries the foliage instance it reached:

```csharp
if (Physics.Raycast(origin, forward, reach, Character, out var hit))
{
    // A painted tree becomes its proxy; anything else is hit as it is.
    var target = hit.FoliageInstance.TryPromote(out var proxy) ? proxy : hit.Entity;
    Damage.Apply(target, damage);
}

// Later, on the proxy (for example once the tree has regrown):
GameObject.GetComponent<FoliageProxyComponent>()?.Release();
```

`FoliageComponent.FindInstances(center, radius)` lists the instances near a point,
for interaction prompts or area effects. `FoliageInstance.TryGetInfo` gives an
instance's type and world transform. From C++, use `PromoteFoliageInstance` and
`ReleaseFoliageProxy` (`scene/FoliageProxies.h`).

Instance IDs are stable, so a game can save what happened to its plants (felled
trees, picked bushes) as a list of IDs and replay it after loading: promote or
remove those instances again. The engine does not save play-session changes to
foliage itself.

## Spatial cells and rendering

Instances are assigned to an X/Z cell from their local position:

```text
cell.x = floor(position.x / cellSize)
cell.z = floor(position.z / cellSize)
```

The configurable cell size is shared by render clustering and collision
partitioning. Render commands continue to use hardware-instanced model matrix
arrays, with one cluster per foliage type and cell.

Camera rendering rejects whole cells outside a type's draw distance, then
filters individual instances only for cells crossing the cutoff. This is useful
for grass and other meshes too small for geometry LODs to save meaningful work:
give those types a short **Draw Distance** in the inspector while
leaving trees on the component's longer range.

The serialized source of truth remains the per-type instance array so existing
scenes and prefab overrides continue to work. Spatial collision data is derived
lazily and cached. It is regenerated only when:

- Instances are added, removed, or transformed
- A type's collision or cell configuration changes
- The owning entity's world transform changes

An ordinary frame does not rebuild the cell cache.

### Level of detail and culling per instance

A cluster's command picks one LOD from the cell's bounds. The camera passes then refine it per instance
(`BasicInstanceLodChain` in `BasicRenderer.h`, `InstanceBinning` in `engine/render/src`). Each frame, every instance
of a cluster with a LOD chain is:

1. **Culled** when its bounding sphere lies outside the camera's side planes, or wholly beyond the cluster's draw
   distance (`BasicDraw::instanceDrawDistance`, from `RenderCommand::maxDrawDistance`). Without this, a visible
   48–128 m cell draws all of its instances, and the runtime, which hands every scene command to the RHI renderer,
   draws whole cells past their draw distance.
2. **Assigned a LOD** by its own projected radius, measured on the displayed image (the output height, not an
   upscaler's lower render height), as the scene's mesh LODs are. The switch radii come from triangle density, not
   the importer's shared thresholds. When the chain is built, `BasicInstanceLodChain::ApplyTriangleDensity` keeps
   each LOD until the whole mesh would spend more than `MaxTrianglesPerPixel` (0.15) triangles per pixel of the
   projected bounding disc. A mesh therefore changes detail where its own triangle counts say it should, and a tree's
   bark and leaves switch together. The shared vegetation thresholds dropped a 60k-triangle tree to 25% of its
   triangles while it still filled the screen. They would also leave a multi-million-triangle scan with tens of
   thousands of triangles on a tree a few dozen pixels across. On Verdant, 0.15 keeps the canopy close to full
   detail at about a third of the triangles that full detail costs.
3. **Crossfaded** near a switch. An instance within `TransitionWidth` (15%) of a switch radius bins into both LODs
   (`BasicInstanceLodChain::SelectBlend`). Its bin entries carry complementary dither codes in their top byte, and
   the shaded and depth passes discard by screen-space interleaved gradient noise (`BasicLit.slang`
   `lodDitherDiscards`), which a temporal resolve turns into a blend. The hand-over to an impostor fades the same way:
   the mesh's chain and the impostor's derive the same fade from the same radius. GPU instance culling fades the same
   way, from the switches `BasicInstanceLodChain::BlendSwitches` computes for both.

Before any per-instance work, a cluster whose bounds lie wholly outside the view or beyond its draw distance is
culled whole (`InstanceView::ClusterVisible`). It is neither binned nor recorded: the camera passes make the same test
before binding anything for a draw. A Verdant frame hands the renderer about 23,500 cluster draws, of which about
21,900 fail this test. Staging empty bins for them, then recording each through both camera passes, cost 7 ms of
binning and over 20 ms of geometry recording at 1080p on the Vega 8. Each remaining LOD draws once over its binned
instances. The depth prepass uses the same bins as the shaded pass. The profiler reports the skipped instances
("outside the view or draw distance skipped"), and instance preparation and binning CPU time separately. Shadow
passes keep the cluster LOD, floored by `MinShadowLod`, and are not culled per instance.

**Foliage detail** (`BasicLighting::foliageDetail`, capped per quality tier by `GraphicsQuality::foliageDetail`)
scales every instance's projected radius before selection. CPU binning, GPU binning and the unbinned chunk fallback
all take it from `InstanceView::lodProjection`, so they agree. Below 1, instances take coarser LODs, and hand over to
their impostors, from nearer. The switch radii scale with it, so the effective triangle density falls with the
square of the detail. Engine tiers: Low 0.6, Medium 0.75, High and Ultra 1. A project manifest's
`QUALITY_PRESET` record carries it as a fifteenth field; older fourteen-field records take the tier's engine default.
On Verdant at 1080p Low (Vega 8), 0.6 takes the opaque triangles from 2.27 M to 1.68 M and scene GPU time from 62 to
52 ms. Below 0.6 little more is saved (1.5 M at 0.35): the rest is instances already at their last LOD and
non-foliage geometry. Leaf-card LODs lose coverage as they thin (handover E3), so mid-distance canopies look sparser
at 0.5 and below. The benchmark's `--foliage-detail=<value>` replaces the selected tier's value for comparisons.

**LOD Distance Scale** (`FoliageTypeAsset::lodDistanceScale`, in the Inspector per foliage type; `render/LodDistanceScale.h`)
multiplies the same projected radius for one type, so at 2 its LOD switches and impostor hand-over all happen twice
as far away, and its triangles grow with the square. `BasicInstanceLodChain::distanceScale` applies it in `Select` and
`SelectBlend`, `InstanceCulling.slang` in GPU binning, and `RenderSceneFrontend` to the cluster LOD that shadow passes
keep. It composes with foliage detail, and floors such as `MinRenderLod` still apply. The range is 0.1 to 10.

Without a LOD chain, or before the bins exist, a cluster falls back to chunks of 64 in instance order, each at the LOD
its nearest instance needs (`InstanceChunkLod`). Instances should therefore be stored in spatial order; the level
kit's `levelkit.vegetation` sorts each type by cell, then along a Morton curve. With per-instance LOD and culling, a
cell's size sets only cluster culling granularity and draw count, so large cells (64–128 m) are cheaper than small
ones.

### Instance storage

Instanced draws read their instances' matrices from persistent storage buffers (`InstanceStorage` in
`engine/render/src`). Each frame a draw binds one 32-byte uniform: its first record and the constants every instance
shares. Retained foliage clusters upload their records once, and an unchanged forest costs no per-frame instance
uploads. Before, every chunk copied 64 × 144 bytes into the per-frame uniform arena, which a dense forest exhausted.
Arrays unused for four frames are released, and their pages are reused once no frame in flight can read them.

### Vegetation LODs

Scanned plants often carry millions of triangles in thousands of disconnected leaves and twigs. The standard
importer LODs lock every island's border, so they barely reduce such a mesh. Turn on **Vegetation LODs** in a
model's import settings (Content Browser; `MODEL_OPTIONS` bit 8, `MeshImportOptions::vegetationLods`) for a deeper
chain. Each step keeps a share of the finest LOD, but never more than its cap on the whole model's triangles (shared
among submeshes by their triangles), so every model's distant LODs end at a few hundred triangles:

| LOD | Share of triangles | Model cap | Used until (projected radius) |
| --- | --- | --- | --- |
| 1 | 25% | — | 420 px |
| 2 | 8% | — | 220 px |
| 3 | 2.5% | 4,000 | 110 px |
| 4 | 0.8% | 1,500 | 55 px |
| 5 | 0.25% | 500 | 25 px |

Leaf submeshes take card LODs instead: each LOD keeps an evenly spread, enlarged subset of the leaf cards, with its own
vertices so enlarging does not move the finer LODs' leaves. Other submeshes prune small islands first. When
topology-preserving simplification stalls at more than
twice its target, it retries permissively with only tearing seams locked, and falls back to clustering only if that
stalls too (at small targets clustering turns bark into spikes). `VegetationLodVersion` in the artifact settings
rebuilds vegetation imports when the chain changes.

### Triangle budgets

A model's **Triangle budget** (Content Browser; `MODEL_TRIANGLE_BUDGET` in its `.plutometa`,
`MeshImportOptions::triangleBudget`) caps its finest LOD. A denser static source is reduced to about the budget before
its LODs are built (`ApplyTriangleBudget`, `engine/import/include/PlutoGE/import/MeshTriangleBudget.h`); its original
geometry is dropped and unused vertices compacted, so the import is also smaller and faster to cook.

- Leaf submeshes (any non-opaque material) become **leaf cards** first (`MakeLeafCards`,
  `engine/import/include/PlutoGE/import/FoliageCards.h`). A modelled leaf whose UVs map affinely onto it becomes one
  quad over its UV rectangle, so the texture's alpha keeps its outline: a 24-triangle scanned leaf becomes 2 triangles
  with no change in shape. Parts that do not fit (twigs, wrapped UVs) are kept as they are.
- Each distinct submesh range gets a share of the budget by its triangles, leaf ranges weighted six times opaque ones:
  leaves define what a plant looks like, and its branches mostly hide behind them.
- **Leaf** ranges over their share keep an evenly spread subset of whole leaves, each enlarged about its centre (at most
  twice) so that the canopy covers about as much as before (`ThinCards`). Leaves are never simplified, because
  simplification collapses each leaf into a stray triangle.
- **Opaque** ranges are simplified, retrying permissively with only tearing seams locked (UV and skin seams) when
  split normals or seams lock them. The finest LOD never falls back to clustering, which smears texture coordinates.
- A range is never emptied, and skinned meshes ignore the budget.
- The artifact settings include the budget and `TriangleBudgetVersion`, so changing either reimports affected
  models only.

The plan's asset rules (`docs/dev/JUNGLE_SCENE_PLAN.md` section 2) set the budgets the Verdant level kit uses: 60,000 for
canopy trees and 40,000 for understorey (the plan's upper limits, since scanned canopies need room for leaves),
12,000 for deadwood, 6,000 for shrubs, plants, rocks and roots, 800 for ground cover.

Leaf cards need the outline in the texture's alpha. Scanned trees with modelled leaves often ship a plain colour map,
because their geometry carves the outline. The level kit masks those at install (`levelkit.leaf_masks`): it draws the
leaves' own triangles in UV space and keys out the atlas's black filler, writing `<colour>_alpha.png` and making the
material MASK. `leaf_masks.repair_installed` applies it to models already installed.

### Impostors

**Bake impostor** (Content Browser; `MODEL_OPTIONS` bit 16, `MeshImportOptions::bakeImpostor`) renders the finest LOD
into an octahedral impostor at import (`BakeImpostor`, `engine/import/include/PlutoGE/import/ImpostorBaker.h`). The
layout is shared with the renderer in `ImpostorLayout.h` and `ImpostorLayout.slang`.

- **The atlas:** 8 x 8 orthographic views over the upper hemisphere (hemi-octahedral; the grid's edges are the
  horizon), 128 px each. It is rasterized on the CPU, so imports need no GPU, with 2 x 2 supersampled coverage and
  dilated edges.
- **Two maps, published as the model's textures:**
  - albedo with coverage in alpha (sRGB);
  - normal and depth (linear). Normals are in each frame's own basis (right, up, view), so a card whose tangent frame
    is that basis decodes them as an ordinary tangent-space normal map.
- **In the mesh asset:** the `.plutomesh` (format 6) records the layout and the two texture references.
  `FoliageComponent` loads them with the mesh (`FoliageType::impostor`). Every cluster command carries the impostor,
  and one submesh's commands own it: the first alpha-masked submesh, whose material the cards shade with.

At render time (`RhiSceneRenderer`, `ImpostorDraws.h`), a mesh with an impostor hands every instance whose projected
radius is below the impostor's threshold (40 px by default) to an impostor draw over the same instances. Both measure
on the same bounding sphere, so each instance draws exactly once.

The impostor draw uses `BasicLitImpostor.slang`'s vertex stages: one card per instance, in the plane of the atlas
frame nearest the view. Its fragments are BasicLitStandard's unchanged, with the atlas bound as base colour and normal
maps, so cards light, fog and receive shadows like the meshes. Cards skip shadow casting, GI and VSM receiver depth:
those keep the mesh at the cluster's shadow LOD.

### GPU instance culling

On by default where the device has compute and the `InstanceCulling` shaders load (`RhiRenderService` and
`RhiSceneRenderer` enable it; `BasicRenderer::SetGpuInstanceCulling` defaults off for direct users such as tests).
`PLUTO_GPU_INSTANCE_CULLING=0`, or the benchmark's `--cpu-instance-binning`, bins on the CPU instead.
`GpuInstanceCulling` (`engine/render/src`) bins instanced draws on the GPU instead of the CPU loop above. It applies the
same culling, wind padding, LOD selection and crossfades (`BasicInstanceLodChain::BlendSwitches`, so an instance near a
switch bins into both LODs with complementary dither codes), and adds occlusion in two phases, as GPU-driven
renderers do:

- **Phase 0**, before the depth prepass, tests the previous frame's depth hierarchy (`OcclusionCulling::Hierarchy`, used
  only when it was built last frame at the same size and the camera moved under 4 m) and marks the instances it hid.
- The coverage depth draws phase 0's bins, then `OcclusionCulling::RecordIntermediateHierarchy` reduces that depth.
- **Phase 1** re-tests only the marked instances against it, into slots and bins of its own, and the coverage depth
  draws those too.

The shaded pass draws both phases. Together they are exactly the instances this frame's depth does not hide, so
something the previous frame hid but this one shows (a camera move, an occluder gone) is drawn in the same frame, not
one frame late; the shaded pass, which tests depth for equality with the coverage depth, draws the same instances at
the same LODs. Without a depth prepass, or with occlusion off, there is one phase and no occlusion at all.
Three compute passes run per phase:

1. **Count:** each instance's slots (draw and LOD; two while it crossfades) are counted.
2. **Scan:** prefix sums give each bin's offset and fill the indirect draw arguments.
3. **Scatter:** record indices, with their dither codes, are written into the bins.

A draw's slots are its LODs in phase 0, then its LODs in phase 1 (`GpuInstanceCulling::Bins::PhaseSlot`), so the
phases a pass draws are one contiguous range. Draws read the bins through the same index buffer binding as CPU bins:

- **Vulkan with indirect first instances:** that range goes as one indirect batch whose first instances are the bin
  offsets: the coverage depth draws phase 0's LODs and later phase 1's, the shaded pass both at once.
- **Otherwise (OpenGL):** each LOD is an indirect draw whose vertex shader reads its bin's offset from a header
  (`instanceRange.z == 2`).

Limits and fallbacks:

- Chains of more than 8 LODs keep the CPU path.
- Every LOD of every binned draw is an indirect draw per phase, empty or not: the RHI has no indirect draw count
  (`vkCmdDrawIndexedIndirectCount`, `GL_ARB_indirect_parameters`) to drop empty bins. On 2026-11-03 that cost more than
  occlusion saved (42 to 94 ms). Since whole clusters outside the view are skipped before binning, it no longer does:
  in Verdant on the Vega 8 (2026-11-09, 1280x720, the scene's quality), scene GPU time fell from 76.5 to 59.9 ms at the
  trailhead and from 79.7 to 65.0 ms with the camera travelling, for about 0.55 ms of culling and hierarchy passes.
  Coverage depth went from 24.4 to 16.9 ms and the shaded pass from 38.2 to 29.0 ms. Review captures match CPU binning
  within capture noise, still and moving.
- An indirect draw count would let the GPU skip the empty slots, but it was measured and is not worth an RHI
  capability: leaving out about 9,200 mostly empty phase-1 draws saved about 0.5 ms of GPU time on Verdant (five
  alternating pairs), within noise. Merging each draw's phases into one batch halves the shaded pass's indirect calls,
  with no measurable change to recording time either way.
- Swaying instances' bounds are padded by this frame's wind's reach at the mesh's top (`FoliageWindFrame::MaxDisplacement`,
  as virtual shadow maps pad casters), not the any-wind bound CPU binning uses: that one roughly doubles a tree's
  sphere, and with it the occlusion test hid little (75 ms against 60 ms on Verdant). Without the padding occlusion would
  hide more (51 ms), but crowns swaying out from behind an occluder would be cut off.
- Triangles and instances of GPU-binned draws are not counted in the frame statistics, because the bins are never
  read back. `gpuBinnedDraws` reports how many draws were binned on the GPU, and `InstanceCullingStats` (read back
  without waiting, so it describes an earlier frame) how many instances were tested, outside the view or range,
  hidden by the previous frame's depth, drawn after all by the second phase, drawn, and crossfading. The editor
  profiler shows them ("RHI GPU instance culling"), and the benchmark CSV has `gpu_cull_*` columns. On Verdant's
  trailhead (still camera): 50,064 instances, 35,792 outside, 5,687 hidden by the previous depth, 422 of them drawn by
  the second phase (dithered crossfades change the depth's holes every frame), 9,007 drawn.

## Physics representation

Every populated collision cell becomes one static Bullet compound body. Each
foliage instance contributes one capsule child shape to that body. This avoids
creating an entity, rigidbody, and broadphase object for every tree.

The same representation is added to:

- The runtime Bullet dynamics world, for character and rigidbody collision
- The physics query world, for raycasts and other scene queries

Foliage collision is kept apart from entity bodies in both worlds. When a
component's instances change, only the cells whose capsules changed are rebuilt:
each cell carries a signature of its contents (`FoliageCollisionCell::signature`),
and unchanged cells keep their Bullet bodies. Felling one tree replaces one cell.
It does not reset the simulation, so other bodies keep their velocities, contacts
and sleep state. Adding or removing entities, such as a spawned proxy, never
rebuilds foliage cells. Queries made after an edit in the same frame already see
it. Static foliage bodies are not transformed or updated every physics step.

Capsules are scaled from the complete owner and instance transform. Radius uses
the larger horizontal scale, while height uses vertical scale. Non-uniformly
scaled trees therefore retain a conservative trunk collider.

### Raycast results

`PhysicsRaycastHit` contains both identifiers:

```cpp
PhysicsRaycastHit hit;
if (scene.Raycast(origin, direction, distance, hit))
{
    // The entity that owns the foliage component.
    EntityID owner = hit.entityId;

    // Non-zero only when a foliage compound child was hit.
    std::uint64_t tree = hit.foliageInstanceId;
}
```

Ordinary entity collisions return `foliageInstanceId == 0`. Foliage instance
IDs are component-local handles. `Raycast`, `RaycastByTag`, `RaycastBatch`,
`RaycastAll` (one hit per instance) and `SweepSphere` report them, in both the
query world and the fixed-step simulation world. Overlap and capsule queries
report only the owner. [Interactive foliage](#interactive-foliage-proxies)
promotes the instance a hit reports.

## Choosing cell and collider settings

Start with these values and profile representative scenes:

| Setting | Suggested starting point | Tradeoff |
|---|---:|---|
| Tree cell size | 32 world units | Balanced body count and edit granularity |
| Dense forest cell size | 32–64 | Fewer Bullet objects, larger compounds |
| Sparse large trees | 16–32 | More precise spatial updates |
| Capsule radius | Trunk radius plus a small margin | Larger values feel disconnected from the mesh |
| Capsule height | Walkable trunk height | Avoid including thin canopy geometry |

Very small cells increase Bullet object and render-command counts. Very large
cells make a small foliage edit rebuild a larger compound and reduce culling
granularity. A cell should normally contain tens or hundreds of trees, not one
tree and not an entire forest. Per-chunk LOD (above) makes the render side favour
larger cells. The Verdant Gorge sample uses 128 m for trees, rocks and deadwood,
and 48–64 m for ground cover.

## Serialization and compatibility

Current scene serialization adds the following per-type fields:

```text
Type.N.MinScale
Type.N.MaxScale
Type.N.AssetReference
Type.N.CellSize
Type.N.MaxDrawDistance
Type.N.LodDistanceScale
Type.N.CollisionEnabled
Type.N.CollisionCenter
Type.N.CollisionRadius
Type.N.CollisionHeight
Type.N.WindEnabled
Type.N.WindStiffness
Type.N.WindFlutter
Type.N.WindBendHeight
Type.N.WindPivotHeight
Type.N.ProxyPrefab
```

The wind fields ([Foliage wind](FOLIAGE_WIND.md)) default to disabled, so older scenes keep still foliage.
`ProxyPrefab` defaults to empty, so older scenes and `.plutofoliage` files keep their types as scenery; the
`PROXY_PREFAB` record is written only when set, and older readers skip it.
`LodDistanceScale` defaults to 1, so older scenes keep their LOD distances. Types without a
scale range start with the component's old `MinScale` and `MaxScale`.

`PaintEnabled`, `BrushMode`, `BrushRadius`, `Density`, `MinScale` and `MaxScale` at
component level are retired: older files may contain them, and loading reads the two
scales as above and ignores the rest. `Component::IsRetiredProperty` lets prefab
variants and overrides naming a retired property load instead of failing. Prefab overrides
stay indexed by type (`Type.N.*`); removing a type in the inspector renumbers them with
`RemapFoliageOverridesAfterTypeRemoval`, so they keep naming the same types.

Instance records now begin with their ID:

```text
id,position.x,position.y,position.z,rotation.x,rotation.y,rotation.z,scale.x,scale.y,scale.z
```

The loader still accepts the former nine-value transform record. It assigns a
new non-zero ID and saves the upgraded form the next time the scene is written.
Collision defaults to disabled, so loading an existing scene does not change
its physics behavior.

## Current scope

This milestone provides static capsule collision. It does not yet provide:

- Boxes, spheres, or multi-shape collider authoring
- Navigation obstacle generation
- Collision-layer settings per foliage type
- Painting on anything but a terrain on the same entity
- Per-field overrides of a linked type's settings
- Marquee selection of instances
- Binary or streamed instance storage
- Per-cell render-command patching during an editor stroke

Render commands are cell-clustered and remain cached during ordinary frames,
but an edit currently invalidates the foliage component's complete render
command cache. Physics collision cells themselves are cached and revisioned.

## Relevant implementation files

- `engine/scene/include/PlutoGE/scene/FoliageTypeAsset.h`
- `engine/scene/src/FoliageTypeAsset.cpp`
- `engine/scene/include/PlutoGE/scene/components/FoliageComponent.h`
- `engine/scene/src/components/FoliageComponent.cpp`
- `engine/scene/src/Scene.cpp` (foliage collision cells in both physics worlds: `SynchronizeFoliageBodies`)
- `engine/scene/include/PlutoGE/scene/FoliageProxies.h`, `engine/scene/src/FoliageProxies.cpp` (promotion and release)
- `engine/scene/include/PlutoGE/scene/components/FoliageProxyComponent.h`
- `engine/scripting/src/FoliageScriptApi.cpp`, `engine/scripting/managed/PlutoGE.ScriptCore/FoliageComponent.cs` (C#)
- `editor/ui/src/inspectors/FoliageInspector.cpp` (the inspector, top to bottom)
- `editor/ui/src/foliage/FoliageTypeSections.cpp` (settings UI shared with the asset editor; add new settings here)
- `editor/ui/src/panels/FoliageEditorPanel.cpp` (the Foliage Editor)
- `editor/ui/src/foliage/FoliageTypeSession.cpp` (the open asset: undo, saving and live preview, no UI)
- `editor/ui/src/foliage/FoliageTypeUsage.cpp` (which types use an asset)
- `editor/ui/src/foliage/FoliageViewportTool.cpp` (tool bar, brush strokes, picking, instance gizmo)
- `editor/ui/src/foliage/FoliageEditTool.cpp` (session state and selection, no UI)
- `editor/ui/src/foliage/FoliageEditing.cpp` (undoable edits, dirty state and prefab overrides)
- `editor/ui/src/foliage/FoliageDiagnostics.cpp` (setup checks; add new checks here)
- `engine/render/src/InstanceBinning.h`, `engine/render/src/InstanceBinning.cpp`
- `engine/render/include/PlutoGE/render/GraphicsQuality.h` (foliage detail per tier)
- `tests/FoliageCollisionTests.cpp`, `tests/FoliageEditingTests.cpp`, `tests/FoliageProxyTests.cpp`,
  `tests/managed/PlutoGE.FoliageSmokeTests`
- `tests/InstanceLodChecks.h`, `tests/GpuInstanceCullingChecks.h`
