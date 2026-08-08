# Surface Cache GI implementation tracker

Last updated: 2026-08-08

## Status legend

- `[x]` complete and verified
- `[~]` implemented, verification in progress
- `[ ]` not started

## Milestone 1 — surface cache visualization

Current step: **4.1 — half-resolution gather target and pass boundary**

- [x] 1.1 Define module boundaries and ownership.
- [x] 1.2 Define surface-card, atlas-rectangle, statistics, and stable card-ID types.
- [x] 1.3 Implement deterministic six-direction card generation from submesh geometry.
- [x] 1.4 Implement a padded deterministic atlas allocator.
- [x] 1.5 Implement an RAII-owned persistent MRT atlas for albedo/metallic, normal/roughness, emission, and depth.
- [x] 1.6 Implement budgeted capture of static, opaque, non-skinned render commands.
- [x] 1.7 Register `SurfaceCacheGI`, expose serialized parameters, and provide atlas debug views.
- [x] 1.8 Add focused tests, build the project, and resolve regressions.
- [x] 1.9 Perform interactive visual validation in representative scenes.
- [x] 1.10 Mark milestone 1 complete after visual acceptance.

Validation recorded on 2026-08-08:

- `PlutoGEEditor` Debug target builds successfully.
- `PlutoGESurfaceCacheTests` passes.
- `PlutoGEPostProcessInitializationTests` passes with the new effect and shaders.
- `git diff --check` reports no whitespace errors.

### Milestone 1 acceptance criteria

- Card generation is deterministic and produces finite projections.
- Atlas content rectangles do not overlap and retain guard padding.
- Material capture writes albedo/metallic, normal/roughness, emission, and card depth.
- Capture work is capped by a per-frame card budget.
- Only explicitly supported static opaque standard materials participate.
- Atlas resources resize and clean up safely.
- Debug views expose every atlas layer without changing normal scene output when set to `Scene`.
- CPU tests and the renderer build pass.
- Interactive inspection confirms correct orientation and material capture.

### Explicit milestone 1 limitations

- Cards are bounds-derived, not triangle-cluster fitted.
- Each eligible render command currently receives its own card set.
- Dynamic transforms, skinned geometry, instancing, masked/blended materials, foliage, terrain-specialized rendering, and custom shader graphs are excluded.
- The atlas is rebuilt as a batch; eviction and incremental residency arrive in a later milestone.
- Milestone 1 captures material properties; milestone 2 adds direct radiance. Visibility tracing, GI gathering, denoising, and compositing remain later milestones.
- Atlas mip generation is deferred until radiance sampling is introduced.

## Remaining roadmap

- [~] 2. Direct-radiance cache capture.
  - [x] 2.1 Add an HDR direct-radiance atlas layer and debug view.
  - [x] 2.2 Capture diffuse outgoing radiance from directional, point, and spot lights.
  - [x] 2.3 Support cascaded shadows for the primary directional light.
  - [x] 2.4 Add physical-sky/environment diffuse contribution.
  - [x] 2.5 Add light/environment revision hashing and budgeted radiance repopulation.
  - [x] 2.6 Add radiance intensity, environment intensity, light-count, shadow, and HDR clamp controls.
  - [x] 2.7 Visually validate atlas presentation and captured material/radiance content.
  - [x] 2.8 Add and validate a ping-pong-safe accumulated-radiance layer before enabling indirect feedback.
    - [x] Allocate two independent HDR history layers.
    - [x] Resolve direct radiance into alternating history layers without texture feedback hazards.
    - [x] Expose history blend and accumulated-radiance debug controls.
    - [x] Visually validate accumulated output.
  - [x] 2.9 Defer point and spot shadow sampling until profiling demonstrates that it is required by the target quality level.
  - [x] 2.10 Mark milestone 2 complete after visual acceptance.
- [x] 3. Extract a reusable visibility interface and initially back it with VCTGI voxel cascades.
  - [x] 3.1 Define a non-owning, per-frame world-visibility snapshot contract.
  - [x] 3.2 Make VCTGI expose directional volume textures and cascade metadata through the contract.
  - [x] 3.3 Move fallback voxel-volume ownership into the renderer so multiple GI consumers do not duplicate work.
    - [x] Add a Surface Cache-owned fallback provider so users do not need to add VCTGI separately.
    - [x] Prefer an enabled shared provider when one already exists.
    - [x] Add a world-visibility-only update entry point and remove the temporary 1x1 cone-trace resolve.
    - [x] Add a renderer-owned, frame-deduplicated world-visibility service shared by fallback consumers.
    - [x] Remove concrete VCT ownership from `SurfaceCacheGIEffect`.
  - [x] 3.4 Add surface-cache lookup metadata and world-hit-to-card candidate indexing.
    - [x] Add a provider-independent CPU uniform-grid spatial index.
    - [x] Build conservative world-space bounds for resident card instances.
    - [x] Add deterministic point-query tests, including overlapping candidates.
    - [x] Upload compact sorted-cell, candidate-ID, and card-bounds SSBO tables.
  - [x] 3.5 Add visibility and card-lookup debug instrumentation.
    - [x] Add a screen-space visibility-cascade coverage view consuming `IWorldVisibilityProvider`.
    - [x] Distinguish missing-provider, warming-up, valid-cascade, and outside-coverage states.
    - [x] Add a GPU `Card Candidates` view that validates exact bounds after cell lookup.
    - [x] Filter candidate diagnostics by G-buffer normal/card-facing compatibility and tolerate small bounds precision errors.
    - [x] Add best-card selection using world-to-card projection, neighborhood depth validation, and a geometric warm-up fallback.
    - [x] Add selected-card and atlas-UV debug views.
    - [x] Make projected-card containment conservative while clamping sampling to each card's content rectangle.
- [~] 4. Add half-resolution hybrid screen/voxel gather and surface-card lookup.
  - [~] 4.1 Add an owned half-resolution HDR gather target and explicit gather-pass boundary.
    - [x] Allocate and resize the gather target independently of the output target.
    - [x] Downsample receiver position/normal inputs and expose a `Gather Inputs` diagnostic.
    - [ ] Visually validate receiver coverage and half-resolution resizing.
  - [~] 4.2 Generate stable hemisphere directions and trace screen-space visibility first.
    - [x] Add configurable deterministic cosine-weighted hemisphere rays.
    - [x] March projected rays against the world-position buffer at half resolution.
    - [x] Store per-receiver screen-hit confidence and expose a `Screen Trace` diagnostic.
    - [ ] Visually validate contact coverage, stability, and useful miss regions for voxel continuation.
  - [ ] 4.3 Continue screen misses through the shared voxel visibility cascades.
  - [ ] 4.4 Project ray hits into candidate cards and sample accumulated radiance.
  - [ ] 4.5 Accumulate confidence-weighted diffuse indirect radiance into the gather target.
- [ ] 5. Add motion-vector temporal accumulation, rejection, and history reset rules.
- [ ] 6. Add spatial filtering, depth-aware upsampling, and lighting-pass composition.
- [ ] 7. Add revision-driven invalidation, residency, eviction, and texel-based update budgets.
- [ ] 8. Complete editor controls, serialization coverage, debug tooling, profiling, and regression scenes.
- [ ] 9. Optionally add screen probes and energy-limited multi-bounce feedback.
- [ ] 10. Optionally add fitted cards, dynamic/skinned capture, and glossy indirect lighting.

## Architecture notes

- `SurfaceCardGenerator` is CPU-only and has no renderer ownership.
- `SurfaceCacheAtlasAllocator` owns placement policy but no GPU resources.
- `SurfaceCacheAtlas` exclusively owns OpenGL atlas objects.
- `SurfaceCacheGIEffect` orchestrates synchronization and capture but does not implement visibility or GI policy.
- Later visibility backends must sit behind an interface so surface caching does not depend directly on VCTGI.
- Runtime references should move to generation-checked handles when eviction is introduced.

## Milestone 2 design constraints

- Direct radiance is stored separately from material properties, so later gather code does not re-evaluate scene lights.
- Lighting changes reset the capture cursor and repopulate cards within the existing frame budget instead of forcing a blocking atlas redraw.
- The first shadow-capable directional light uses up to four existing shadow cascades. Additional lights currently contribute unshadowed radiance.
- Outgoing radiance is clamped in linear HDR space to limit pathological light and emissive values.
- Indirect feedback remains disabled until a separate ping-pong accumulation target exists. Sampling and rendering to one atlas texture simultaneously would be undefined OpenGL behavior.
- Atlas debug views frame the occupied allocation region. A red checkerboard means no eligible cards are resident; disable `Static Geometry Only` for diagnostic capture or mark participating scene meshes static.
- Atlas previews preserve the occupied region's aspect ratio. Six-card meshes commonly appear as six tiles in a wide shelf row; this is allocator layout, not a screen-space GI result.
- Visibility debug colors: magenta checker means provider creation failed; orange checker means the provider is progressively building its first volume; blue/green/orange surfaces are covered cascades; solid dark purple means a surface lies outside all completed cascades. Surface Cache GI now creates a fallback voxel provider when no enabled shared VCTGI provider exists.
- Surface Cache's fallback requests three cascades by default. When consuming a separately enabled VCTGI provider, the displayed count follows that provider; a two-cascade VCTGI configuration correctly shows blue, green, then purple with no orange region.
