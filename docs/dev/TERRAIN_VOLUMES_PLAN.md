# Terrain volumes: caves and destructible ground

Written 2026-11-18. Status (2026-11-18): phases 0-2 are built. Tile holes are saved (`TerrainHoleFile`,
`TerrainWorldComponent::SaveTileHoles` / `StoreTileHoles`, the Holes brush on worlds). The volume core and surface nets
mesher live in `engine/scene/volume` and are tested by `PlutoGETerrainVolumeTests`. Nothing uses the volume in a scene
yet: phase 3 is next. Later phases are design only.

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

engine/scene/components/
  TerrainVolumeComponent        (phase 3) owns a TerrainVolume beside a TerrainComponent or a world tile; meshes dirty
                                chunks on workers; makes chunk entities (mesh + mesh collider); derives the terrain's
                                holes from the edited footprint
```

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
`voxelSize / 64`, clamped to ±2 voxels: precise near the surface, which is all the mesher needs. The heightfield base
is `y - height(x, z)` (vertical distance, clamped).

### Coupling to the heightfield (phase 3)

1. A chunk's base density comes from the terrain's own height sampler, so an unedited chunk's surface is the terrain's.
2. Each edit marks the **terrain columns it changed** (dilated by one cell). Those cells become holes in the tile.
3. The chunk mesh keeps only triangles over cut columns plus a one-cell overlap ring. The ring's vertices are snapped to
   the heightfield and dropped a few centimetres (as terrain skirts are), so the terrain wins the depth test where both
   exist and the volume fills the cracks a cell grid and a voxel surface would otherwise leave.
4. Volume chunks are tile-owned files (`tile_x_z_volume.plutovolume`) beside the height, weight and hole files.

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

## Open questions

- Whether edits in save games store whole chunks (simple, larger) or an operation log replayed on the generated base
  (small, needs every op deterministic forever). Phase 5 starts with chunks; the op log is what the network sends.
- Whether the whole world can be dug or only marked zones. The design supports both; zones keep the far terrain simple.
