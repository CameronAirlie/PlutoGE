# Surface Cache GI implementation tracker

Last updated: 2026-08-08

## Status legend

- `[x]` complete and verified
- `[~]` implemented, verification in progress
- `[ ]` not started

## Milestone 1 — surface cache visualization

Current step: **2.7 — interactive direct-radiance validation**

- [x] 1.1 Define module boundaries and ownership.
- [x] 1.2 Define surface-card, atlas-rectangle, statistics, and stable card-ID types.
- [x] 1.3 Implement deterministic six-direction card generation from submesh geometry.
- [x] 1.4 Implement a padded deterministic atlas allocator.
- [x] 1.5 Implement an RAII-owned persistent MRT atlas for albedo/metallic, normal/roughness, emission, and depth.
- [x] 1.6 Implement budgeted capture of static, opaque, non-skinned render commands.
- [x] 1.7 Register `SurfaceCacheGI`, expose serialized parameters, and provide atlas debug views.
- [x] 1.8 Add focused tests, build the project, and resolve regressions.
- [ ] 1.9 Perform interactive visual validation in representative scenes.
- [ ] 1.10 Mark milestone 1 complete after visual acceptance.

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
  - [ ] 2.7 Visually validate light direction, attenuation, cascade transitions, environment orientation, and HDR values.
  - [ ] 2.8 Add a ping-pong-safe accumulated-radiance layer before enabling indirect feedback.
  - [ ] 2.9 Add point and spot shadow sampling if required by the target quality level.
  - [ ] 2.10 Mark milestone 2 complete after visual acceptance.
- [ ] 3. Extract a reusable visibility interface and initially back it with VCTGI voxel cascades.
- [ ] 4. Add half-resolution hybrid screen/voxel gather and surface-card lookup.
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
