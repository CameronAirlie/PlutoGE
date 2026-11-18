# Terrain volumes: caves and destructible ground

Written 2026-11-18. Status (2026-11-18): phases 0-9 are built, with the phase 4-7 follow-ups (debris, replication,
remembered foliage, levelkit caves on stamps) done.

- 0-3: tile holes are saved; the volume core, surface nets mesher, coupling rules and meshing queue live in
  `engine/scene/volume` (`PlutoGETerrainVolumeTests`); `TerrainVolumeComponent` puts a volume on a terrain or a world
  tile (`PlutoGETerrainVolumeComponentTests`).
- 4: chunks draw with the terrain's layers through the terrain shader's vertex-layer, triplanar mode
  (`PlutoGEOpenGLTerrainLayerTests`, `PlutoGEVulkanTerrainLayerTests`).
- 5: `scene::EditTerrainVolumes` (world-space edits over every volume under a shape, budget, foliage removal, rock
  accounting), runtime edits kept across tile streaming, save-game capture and restore, and the C# `TerrainVolumes` API
  (`PlutoGETerrainVolumeScriptApiTests`, `PlutoGETerrainVolumeManagedTests`).
- 6: generated caves (`CaveDensity`, a `CAVES` record in `ProceduralTerrainDefinition`) and ground materials.
- 7: `VolumeStampComponent` and the Terrain Editor's Dig tool (`PlutoGETerrainEditorTests` covers the tool state).
- 8: the `DualMesher` frame with surface nets and dual contouring, blended layer seams, per-vertex cave occlusion.
- 9: far proxies: a streamed-out tile's dug ground and cave mouths drawn coarsely in the world's overview.
- Follow-ups: debris (`TerrainDebris`), edits as data and C# replicators, foliage removals remembered per tile,
  composite layers culled by their bounds, and SurvivalGame's crag cave carved by stamps (Tools/levels/cave.py).

User guide: [Terrain volumes](../TERRAIN_VOLUMES.md).

## Goal

Caves, overhangs, tunnels and digging on the existing terrain, placed by hand or generated, without giving up what the
heightfield does well: kilometre-scale streamed worlds, LOD, biomes, detail grass, cheap shadows and collision.

## Decisions

| Question | Decision |
| --- | --- |
| How caves are drawn | **Meshes made from a density field**, not ray marching. Every system (virtual shadow maps, DDGI/VCT, occlusion culling, Bullet, navigation, foliage, surface queries) consumes triangles; a ray-marched cave would need a second path through each of them and costs far more on the iGPU target. Ray marching is reserved for tools (the 3D brush preview) and possible later SDF effects. |
| Heightfield or voxels | **Both: a sparse density volume over the heightfield.** The heightfield stays the ground everywhere. Volume chunks exist only where something is underground or dug. Where a chunk takes over, the heightfield's cells are cut with the existing hole system and the chunk's mesh draws that ground instead. |
| Hand-placed caves | Two routes, both kept. **Mesh + holes** (built, `docs/TERRAIN_HOLES.md`) for bespoke indestructible set pieces. **Stamps**: a shape, mesh or spline tunnel subtracted from the volume, so the cave can then be dug. |
| Procedural caves | Density rules (tunnels, chambers, noise and masks) evaluated per chunk with a fixed seed, like biome scatter: the same seed gives the same caves on every machine, so only edits need storing or sending. |
| Destruction | Edits are CSG operations (subtract, add, smooth, paint) on the density. Edited chunks store their full density; generated, unedited chunks are never stored. |
| Surface extraction | **Surface nets** first (simple, robust, smooth rock, few triangles). The mesher sits behind an interface so dual contouring (sharp edges) can follow without touching its callers. |
| Voxel size | Per volume; default **0.5 m** (shovel-sized digs, about 64 KB per 32³ chunk with material). |

## Architecture

```
engine/scene/volume/            pure data and algorithms, no scene/renderer dependency (like TerrainHoleMask)
  VolumeGrid                    chunk/voxel coordinates <-> world space
  VolumeChunk                   N³ density samples (int8 signed distance) + material (uint8)
  VolumeDensitySource           what the density is before edits: heightfield, cave rules, composites
  VolumeShape / VolumeEdit      signed distance primitives and CSG edits (subtract, add, paint)
  TerrainVolume                 sparse chunk map: base source + stored chunks + dirty set + revision
  VolumeFile                    compact stored-chunk files for tile files and save games
  VolumeMesher                  interface; SurfaceNetsMesher implementation
                                (input: a padded DensityBlock; output: render::MeshData + materials)
  TerrainVolumeCoupling         TerrainSurface (the drawn heightfield, an immutable snapshot), ComputeVolumeHoles,
                                FitVolumeMeshToTerrain, ChunksAlongSurface: the rules joining a volume to its terrain
  VolumeMeshQueue               gathers on the caller's thread, meshes + finishes on workers, drops superseded results
  TransformedShape              (in VolumeShape) a shape seen from another space: world-space edits on a local volume;
                                owning or borrowing its inner shape
  CaveDensity                   (phase 6) generated worm tunnels and chambers per world cell, deterministic, bucketed
  VolumeEditArchive             (phase 5) named blobs of edits: a world's tiles, a scene's terrains (save games)
  EncodeTriangleMaterials       (phase 4, VolumeMesher.h) one layer per triangle, written to uv.x for the shader

engine/scene/components/
  TerrainVolumeComponent        owns a TerrainVolume beside a TerrainComponent or a world tile; glue only: snapshots,
                                scheduling, chunk entities (mesh + mesh collider), derived holes, files, navigation
  TerrainComponent              derived hole layers (SetDerivedHoles by source; GetEffectiveHoles is what draws,
                                collides and answers queries; GetHoles and the saved HoleMask stay authored)
  VolumeStampComponent          (phase 7) an authored shape (carve, fill, paint) folded into the bases under it

engine/scene/TerrainVolumeEditing (phase 5) gameplay edits in world space: every volume under a shape, the budget,
                                foliage removal, flushing, save-game capture/restore
engine/scripting/TerrainVolumeScriptApi + ScriptCore TerrainVolumes.cs   (phase 5) the C# bridge (optional export)
editor TerrainViewportTool Dig  (phase 7) the 3D brush; strokes are undo steps saving each volume's file
```

A volume's **base** is built by the component from everything that is not an edit, and rebuilt when any of it changes
(`TerrainVolumeComponent::BaseInputs`): the terrain's ground (`MakeTerrainDensity`: the drawn surface, its painted layer
at the top and an optional rock layer below a depth), then generated caves subtracted, then stamps in order. Sources
report **feature bounds** (`VolumeDensitySource::AppendFeatureBounds`), so the volume meshes the chunks caves and stamps
reach without storing them, and the hole pass looks for cave mouths under them. Edits always apply to the current base.

Principles that keep it open to change:

- **Data, algorithms and engine glue are separate layers.** The volume library knows nothing about entities, Bullet or
  the renderer; components adapt it. Tests run it without an engine.
- **Interfaces at the points expected to vary**: the density source (heightfield today, cave rules and noise later,
  composites of both), the shape (primitives today, mesh SDFs and splines later), the mesher (surface nets today, dual
  contouring or Transvoxel LOD later).
- **The mesher never sees chunks.** It meshes a padded `DensityBlock` and owns exactly the cells of its core region, so
  neighbouring chunks meet without seams and any container (a tile's volume, a test, an editor preview) can use it.
- **Deterministic everywhere**, so edits can be replicated as operations and regenerated content is never saved.

### Density convention

Signed distance in metres, **negative inside rock**, positive in air. Stored as int8 in units of
`voxelSize / 32`, clamped to about ±4 voxels: precise near the surface, which is all the mesher needs. The heightfield
base is `y - height(x, z)` (vertical distance, clamped), where height is the terrain's drawn surface: smoothed samples,
each cell split along its (x + 1, z) - (x, z + 1) diagonal.

### Coupling to the heightfield (phase 3, as built)

1. The volume lives in the terrain's local space (sample (0, 0, 0) at the terrain's origin) and its base is a
   `TerrainSurface` snapshot of the drawn surface, so an unedited chunk's surface is the terrain's. Sculpting the
   terrain replaces the base (`TerrainVolume::SetBase`); stored chunks keep their samples.
2. **Holes**: a sample column of a stored chunk is *breached* when one of the three samples around the surface moved
   more than `breachTolerance` (a quarter voxel) from the base. Every cell holding a breached column is cut. A cave under
   the ground changes no sample near the surface (subtracting only raises distances that were already lower), so it cuts
   nothing.
3. **Fitting** (`FitVolumeMeshToTerrain`, on the meshing worker): a triangle stays when it is off the terrain, over a cut
   cell, not on the surface (any corner further than `surfaceTolerance`, half a voxel, from it: caves, overhangs, pit
   walls), or over the `overlapCells` ring around cut cells. Ring vertices on the surface sink by up to `overlapSink`
   (5 cm), ramping in over a voxel from the cut, so the terrain wins the depth test where both draw.
4. **Ordering**: new holes go onto the terrain only once every mesh queued for them has landed, so no frame shows an
   empty hole. When holes change, the chunks along the changed cells' surface (`ChunksAlongSurface`, ring included) mesh
   again, edited or not.
5. Holes reach the terrain as a **derived layer** (`TerrainComponent::SetDerivedHoles("TerrainVolume", mask)`), never
   mixed into the authored, saved `HoleMask`.
6. Stored chunks persist in the component's `VolumeFile`; a world gives each tile `tile_x_z_volume.plutovolume` beside
   its height, weight and hole files, read on the streaming worker with the heights.

### What each engine system needs

| System | Work |
| --- | --- |
| Rendering | A triplanar terrain material for volume meshes using the terrain's layer stack (per-vertex material). Chunk meshes go through `Mesh::ReplaceGeometry`; shadow pages and GI over the chunk's bounds are invalidated through the existing content-revision path (to verify). |
| Physics | One `ColliderShape::Mesh` per chunk, rebuilt after edits; `Scene::MarkColliderShapesDirty` makes queries in the same frame see it. |
| Navigation | `NavigationSystem` is already a layered heightfield (16 surfaces per column) with `RebakeRegion`: call it over an edited chunk's bounds. |
| Queries | Terrain surface queries already return false over holes; `GroundSurface`-style checks and raycasts must include chunk meshes (they are ordinary colliders). |
| Foliage and grass | Edits remove foliage instances in the changed region (generated IDs are positional, so the suppression is permanent) and hide detail grass over cut columns. |
| Streaming | Chunks load with their tile (worker thread) and mesh with an activation budget per frame. |
| Distance | The overview stays solid; cave mouths close up far away until a coarse volume LOD exists (phase 9). |

## Phases

Each phase lands with tests for what it changed and a doc update; nothing later is needed for an earlier phase to be
useful.

0. **Tile holes are saved.** Holes in terrain world tiles go in `tile_x_z_holes.plutoholes` beside the height file,
   load with the tile on its worker, and are painted, saved and undone in the editor like tile weights. Unblocks
   mesh + holes caves on streamed worlds today.
1. **Volume core** (`scene/volume`): grid, chunk, density sources (heightfield, constant, composite), shapes (sphere,
   box, capsule), edits, the sparse `TerrainVolume` with dirty tracking, and the volume file. Unit tests.
2. **Mesher**: `VolumeMesher` interface, `DensityBlock` gathering from a `TerrainVolume` (neighbour chunks or base),
   `SurfaceNetsMesher` (gradient normals, per-vertex material, outward counter-clockwise winding). Unit tests: closed
   sphere, seamless neighbouring chunks, flat heightfield.
3. **TerrainVolumeComponent**: volume beside a terrain or tile, worker meshing, chunk entities with mesh colliders,
   derived holes, overlap ring, tile files, navigation rebake.
4. **Rendering**: triplanar volume material from the terrain layers; shadow/GI invalidation checks; Vulkan and OpenGL
   render tests.
5. **Runtime destruction API**: `TerrainVolume` edits from C++ and C# (`TerrainVolume.Dig`), budgets, save-game
   persistence of edited chunks, replication as operations, debris from the removed volume through `FractureMesh`,
   foliage suppression.
6. **Procedural caves**: a `caves` section in `ProceduralTerrainDefinition` (worm tunnels, chambers, noise and mask
   rules) as a density source; per-tile generation on workers.
7. **Authoring**: `VolumeStampComponent` (primitive, mesh SDF, spline tunnel), a 3D sculpt brush in the Terrain Editor
   with a ray-marched preview, and the levelkit `caves` tools moving onto stamps.
8. **Surface quality**: dual contouring mesher, material blending at seams, cave interior fog and ambient occlusion
   (the crag cave follow-ups).
9. **Distance**: coarse volume LOD (Transvoxel or skirts) so cave mouths keep their shape far away.

## Phases 4-7 as built

**4. Rendering.** `TerrainLayers.slang` gained two switches in its parameters (`terrainMapping`): weights from the
maps or from the vertex (`uv.x` names one layer per triangle; `render::TerrainLayerWeights`), and top-down or
triplanar projection (`render::TerrainLayerProjection`, whiteout-blended normals). No new pipeline variants: chunks use
the terrain shader with a derived material (`TerrainVolumeComponent::LayeredTerrainMaterial`: the terrain's layered
material, vertex layers, triplanar, no macro albedo) that follows the terrain's layers. `EncodeTriangleMaterials`
copies vertices where materials meet, so seams are hard edges until phase 8. Shadows: chunks are static meshes whose
`ReplaceGeometry` bumps the content revision that the shadow draw signatures and VSM static pages hash; not checked
by eye.

**5. Runtime destruction.** Edits report the rock they removed (per material) and added. `EditTerrainVolumes` sends a
world-space shape to every enabled volume under it (lone terrains and tiles alike), refuses shapes wider than
`maxExtent` (32 m by default), optionally filters volumes (`accept`) and removes foliage instances whose pivot a dig
carves out. A terrain world keeps tiles' unsaved edits while they stream out (`m_volumeEdits`, never written to tile
files) and exports/imports them as a `VolumeEditArchive`; `CaptureTerrainVolumeEdits` / `RestoreTerrainVolumeEdits`
do the whole scene (worlds and lone volumes named by entity path). The C# API (`TerrainVolumes.Dig/Fill/Tunnel/Paint/
Edit/Flush/CaptureEdits/RestoreEdits/ClearEdits`, `TerrainVolumeComponent`) goes through one optional bridge export,
`RegisterTerrainVolumeApi`. Fixed on the way: unedited chunks at a terrain's edge meshed a wall (the base was air past
the edge); the base now continues the edge (`TerrainSurface::ExtendedHeight`) and fitting drops triangles off the
terrain.

**6. Procedural caves.** `CaveNetworkSettings` (in `ProceduralTerrainDefinition::caves`, a `CAVES` record older readers
skip): per world cell, tunnels wander through a band of world heights with chambers and wall noise. Generation is in
world space with a 16 m margin, so a world's tiles agree at their seams; `CaveDensity` buckets the pieces for fast
`Fill`. Holes now compare samples with the terrain's own ground (`TerrainGroundDistance`), not the base, so a generated
cave crossing the surface cuts a mouth; only feature columns that reach the surface are scanned. Ground materials:
`TerrainSurface` carries each sample's strongest painted layer, and `TerrainGroundMaterials` adds a rock layer below a
depth; `TerrainLayerStack::GetWeightsRevision` lets the base follow painting and generation (after a quiet frame).

**7. Authoring.** `VolumeStampComponent` (sphere, box, capsule; carve, fill, paint; order) is folded into the base of
every volume under it, so stamps move, resize and disappear with their entity and are saved with the scene, never as
edits. Stamps live in a registry rather than a scene walk. The Terrain Editor's **Dig** tool (Dig / Fill / Paint,
radius, rate, layer) aims with physics so it digs into dug ground, edits only the selected terrain or world's tiles,
gives a lone terrain a volume file on its first stroke (`Assets/TerrainVolumes/<scene>_<entity>.plutovolume`), saves
each touched volume's file when the stroke ends, and records one undo step (`TerrainWorldComponent::StoreTileVolume`
for tiles that have streamed out). The cursor is a wireframe sphere. Biomes gained a Caves block.

## Phases 8-9 and the follow-ups as built

**8. Surface quality.**
- `SurfaceNetsMesher` became one implementation of `DualMesher`: one vertex per crossed cell, one quad per crossed
  edge, with the edge ownership, winding and seams shared. Implementations only place the vertex (`PlaceVertex`).
- `DualContouringMesher` places it with a regularised quadratic error fit to the crossings' planes, which keeps box
  corners and cut faces. It about halves the corner error; gradient normals from quantized density limit it.
  `TerrainVolumeComponent::Mesher` selects it, and `MakeVolumeMesher` maps a kind to a mesher.
- Seams blend: `VertexLayerEncoding` packs a triangle's three layers in uv.x, the weights of the first two in uv.y and
  uv2.x, and occlusion in uv2.y. The shader's `sampleVertexLayers` blends up to three layers, so neighbouring triangles
  agree along their shared edges.
- Cave darkness: the mesher measures each vertex's openness (the share of air around it). `ApplyBurialOcclusion` then
  darkens by depth under the terrain (`VolumeShadingSettings`: BurialDepth, BuriedOpenness), and BasicLit multiplies it
  into the material's ambient occlusion.
- Fog is not part of this: darkening the fog in caves still needs a baked Ambient Light Volume, because the engine has
  no unbaked sky-visibility source.

**9. Distance.**
- When a world tile streams out, `TerrainVolumeComponent::BuildFarProxy` builds a `volume::VolumeFarProxy`
  (`BuildVolumeFarProxy`). It re-samples the volume at coarse voxels (`TerrainWorldComponent::FarVolumeVoxel`, 2 m by
  default) over the overview's own heights: the tile's stored chunks where it has them, and the base made again over
  the overview's surface elsewhere (`MakeBaseOver`). Untouched ground therefore agrees with the overview.
- The overview tile is cut where the proxy breaches it, and coarse meshes fill those cells, drawn with a copy of the
  tile's layered material that the world owns. Proxies survive overview rebuilds and are rebuilt each time the tile
  streams out.
- A tile never visited has no proxy, so its generated caves show in the distance only after the first visit.

**Follow-ups.**
- Debris: `volume::MeshRemovedRock` meshes the rock a Subtract would remove (the volume intersected with the shape)
  as one closed mesh. `SpawnTerrainDebris` breaks it with `Shatter` into pieces of the rock's main layer. They draw with
  a process-lifetime single-layer material, and fracture caps are normalised to it. The pieces are enabled through
  `TerrainVolumeEditOptions::debris` and returned to C# (`TerrainVolumeEditResult.Debris`).
- Replication: `TerrainVolumeEdit` (C#) is an edit as 72 bytes. `TerrainVolumeServerReplicator` applies edits and
  broadcasts them, sends joining clients the whole state (`CaptureEdits`) and accepts client requests through `Accept`.
  `TerrainVolumeClientReplicator` restores the state, applies edits in order and sends requests. Debris stays local.
- Remembered foliage: digs record the procedural foliage instance IDs they remove on a world tile
  (`TerrainWorldComponent::SuppressFoliage`). They are removed again after each regeneration and saved with the edits
  (`foliage:x,z` archive entries).
- Culling: `CompositeDensity` keeps each layer's feature envelope and skips Union, Subtract and Paint layers that
  cannot reach a block, so a base of many stamps costs only the stamps near each chunk.
- SurvivalGame:
  - The crag cave is 37 capsule stamps on Main's terrain volume, written by `cave.py --cave-only`, which leaves the
    heights the user sculpted alone.
  - LakeDistrict's tiles generate caves and dig to rock (`Tools/levels/terrain_volumes.py`).
  - Shovels and pickaxes dig through `GroundDigging`, and the dug ground is saved by `TerrainPersistence`.

## Remaining limits

- Not checked by eye: the triplanar look and seam blending on real layers, cave darkness at real exposure, shadow and
  GI updates after remeshing, the Dig tool in the editor (its stroke needs a window), generated caves and far proxies
  in a streamed world, and debris look.
- Far proxies are built on the main thread as a tile streams out (tens of milliseconds for a tile full of caves).
- Painting the terrain rebuilds the base a frame after the strokes stop, remeshing every chunk of the volume.
- Stamps support primitives only (mesh SDFs and splines are open); a ray-marched brush preview is open.
- Stamps and caves are re-sampled when the base is rebuilt; a world with many tiles rebuilds each tile's base when a
  stamp over it moves.

## Phase 3 limits and follow-ups

- Holes are recomputed over every stored chunk's columns after each change: fine for occasional edits; continuous
  digging over many chunks wants an incremental pass over the changed box.
- A lone terrain reads its volume file on the main thread when the volume is first made; tiles read on the worker.
- Stored chunks do not follow later sculpting of the terrain under them.
- `ProvideFileContents` / `SetVolumeFile` / grid changes rebuild the volume from its file: unsaved edits go.

## Open questions

- Whether edits in save games store whole chunks (simple, larger) or an operation log replayed on the generated base
  (small, needs every op deterministic forever). Phase 5 stores chunks; an op log is what a network would send.
- Whether the whole world can be dug or only marked zones. The design supports both; zones keep the far terrain simple.
