# Environment design milestones

Requested 2026-10-07. Work proceeds in the order below. This supplements, rather than replaces, FEATURE_MILESTONES.md.

## Delivery rules

- Keep authoring sessions, previews and preferences in EditorUI; keep reusable surface sampling, instance data and generated geometry in the engine.
- Use existing asset references, prefab resolution, scene transactions and RHI. Do not introduce a second world representation or renderer.
- Pure placement/stroke calculations must be testable without a window. Preview must not instantiate scripts, physics or permanent scene objects.
- Scene edits are undoable and preserve prefab overrides. Shared asset writes have explicit Save/Revert and are not represented as scene undo.
- Version new persistent formats, accept old scenes, and test project reopen, failed input, parent transforms and play/stop transitions.
- Each milestone requires affected-target builds, focused regression checks and documented interactive OpenGL/Vulkan acceptance. Record unrun checks honestly; do not mark a milestone complete on partial delivery.
- Profile representative data before introducing concurrency or incremental cache machinery. Derived data remains disposable; authored records are authoritative.

## Ordered milestones

| ID | Delivery | Status |
| --- | --- | --- |
| E01 | Reliable terrain foliage strokes | Implemented; automated checks passed; interactive acceptance pending |
| E02 | Surface placement for meshes and prefabs | Implemented; focused checks passed; normal build and interactive acceptance pending |
| E03 | Environment asset palettes | Planned |
| E04 | Modular building assembly | Planned |
| E05 | Foliage assets, mixed painting and filters | Planned |
| E06 | Incremental foliage edits and compact undo | Planned |
| E07 | Incremental terrain sculpting | Planned |
| E08 | Regenerable spline dressing | Planned |
| E09 | Environment section workspace and diagnostics | Planned |

### E01 — Reliable terrain foliage strokes

Distance-spaced dabs in terrain-local X/Z units, one initial dab and no repeated growth while stationary. Density means candidates per dab, independent of render frequency. Reset sampling across invalid surfaces and tool interruptions; bound work for discontinuous cursor jumps. Validate each candidate against terrain bounds, finite height and a local surface normal. Keep the existing single-stroke undo and stable IDs. Preserve the legacy native brush API.

Acceptance: replay equivalent paths at 30/60/144 input samples with equivalent dab positions and counts; stationary input adds no extra dabs; leaving/re-entering terrain does not bridge gaps; edge candidates never paint outside terrain; sloped/scaled owners produce valid local transforms; undo/redo and save/reload retain IDs. Minimum spacing and density reapplication belong to E05.

### E02 — Surface placement

A persistent placement session with mesh/prefab ghost preview, cursor surface hit, yaw/scale/offset, normal alignment, grid snap, repeat stamp and cancel. Reuse ground-placement support bounds and parent conversion. Resolve asset loading before committing; keep previews free of scene side effects. Define no-hit behavior visibly. Every stamp is one scene transaction.

Acceptance: place a multi-child prefab and a mesh on terrain and static geometry, including rotated/scaled parents; preview and committed pose agree; cancel changes nothing; undo/redo restores prefab references; scene/project replacement ends the session safely. Validate both graphics backends.

### E03 — Environment palettes

Project-owned named asset collections with thumbnails, search, recent assets and placement presets. Reuse Content Browser thumbnail/cache and asset pickers. Store references, not file copies or raw pointers. Missing assets remain visible and repairable; palettes do not alter runtime cooking dependencies unless referenced by runtime content.

Acceptance: create, reorder, reopen and repair a palette; stamp from it using E02; isolated project preferences; malformed files recover safely.

### E04 — Modular building assembly

Temporary placement pivots, named compatible prefab sockets, vertex snapping, repeat-last-transform, linear/grid arrays with preview, and assembly isolation/selection locks. Start with imported modular kits. Keep structural prefab variants out of scope until a separate override design exists.

Acceptance: build and rearrange a room using corners, walls, doors and floors without gaps; rotated assemblies snap correctly; arrays commit atomically and respect budgets; scene undo and prefab references survive reload. Validate kit units, missing collision and invalid sockets.

### E05 — Foliage presets and controlled scatter

Dedicated foliage asset creation/editing using normal Save/Revert. Version the existing policy format to support complete prototype references and placement settings without breaking v1 assets. Weighted multi-type painting, explicit seed, minimum spacing via spatial lookup, slope/height/surface filters, exclusions and selective reapply. Add mesh-surface painting through a reusable surface query, separate from gameplay collision. Ordinary interactive prefabs remain entities.

Acceptance: paint a mixed forest edge reproducibly; exclusions keep paths/buildings clear; reapply only chosen attributes; moving/removing neighbors keeps spacing index correct; assets cook with transitive mesh/material dependencies; v1 projects load unchanged.

### E06 — Local foliage editing

Measure large-forest stroke latency and history memory first. Track dirty cells by stable IDs; patch affected render clusters and collision compounds. Use transactional added/removed/transformed instance deltas for undo. Handle cross-cell moves, type removal, owner transforms and stale history safely. Avoid caching raw pointers across vector edits.

Acceptance: local edits match a full reference rebuild; untouched cell resources remain unchanged; undo memory follows the edited set; collision raycast IDs stay correct; compare edit latency and memory at 10k/100k instances on both backends.

### E07 — Local terrain sculpting

Dirty sample rectangles expanded for smoothing/normals and shared chunk/LOD seams. Rebuild affected render/collision chunks; bounded GPU updates through RHI. Keep serialization independent of derived chunks. Add explicit reviewable resnap for dependent foliage and road conformity rather than silent movement.

Acceptance: local/full rebuild equivalence, seam continuity, valid collision and navigation invalidation, undo/reload equivalence; record 65/257/1025 sample terrain timings before/after.

### E08 — Regenerable spline dressing

Extend RoadPlacement with deterministic placement settings, generated-output ownership, replace preview, cancellation and output budgets. Reuse for fences, lamps, hedges and roadside props. Preserve manual overrides or report conflicts; replacement must never delete unrelated children. Define local/world distance and spacing explicitly.

Acceptance: rebake repeatedly without duplicate groups; point edits update owned output; manually edited and unrelated objects are preserved; prefab overrides and undo restore the prior generation; export remains compatible.

### E09 — Sections and environment diagnostics

Editor section list with bounds, ownership, load/isolate/edit and validation. Reuse SceneStreaming lifecycle. Diagnose references crossing sections and generated content ownership. Expose edit timings, visible instance/cell counts, draw/shadow cost and collision-body counts. Profile activation/navigation hitches before proposing frame-budgeted runtime activation.

Acceptance: edit adjacent sections, reopen, unload/reload without stale selection/history; cross-section reference errors navigate to owners; cooked builds retain required assets; stress-test transitions on OpenGL/Vulkan.

## Baseline tasks and completion record

Record build/configuration, hardware, backend, content scale, task completion time, corrective actions, p50/p95 edit latency, undo bytes and save/load time. Use: modular room, dressed courtyard, mixed forest edge and adjacent streamed sections. Capture baselines before claiming speed improvements.

2026-10-07: Initial code/document investigation completed. Existing foundations include group editing, ground placement, prefab property variants, cell-instanced foliage/collision, spline baking and additive streaming. E01 implementation started. Interactive baselines and backend acceptance remain pending.

### E01 delivery — 2026-10-07

- Added a UI-independent, bounded distance stroke sampler and per-viewport stroke ownership. Scene revision guards prevent old snapshots being resolved against a replacement scene; type/mode/selection changes finish the old stroke.
- Added optional per-candidate foliage surface samples and bounded terrain height/normal queries. Retained the legacy brush entry point. Corrected yaw/tilt composition for the existing Y/X/Z render transform order.
- Added input-frequency, stationary, corners, invalid input, reset and jump-budget checks. Extended foliage checks for candidate rejection, per-candidate heights, normal alignment, instance IDs, history restoration and serialization.
- Direct MSVC compilation passed for modified scene components, viewport and its EditorShell consumer. A validation editor linked against existing RelWithDebInfo dependency libraries. Five focused executables passed: BrushStrokeSampler, FoliageCollision, SceneHistory, GroundPlacement and MultiEntityEdit. No engine renderer or managed bridge changes were needed.
- Normal CMake/MSBuild validation is blocked in this session: Visual Studio installer discovery returns no instances; explicitly selecting the installed version proceeds but MSBuild FileTracker initialization fails with access denied. Validation therefore used direct compiler/linker invocations, not a successful normal CMake build. Artifacts are isolated in out/environment-validation; existing engine libraries were not overwritten.
- Interactive OpenGL/Vulkan painting, mid-stroke tool/play transitions and baseline task timings remain pending. E01 is not marked fully accepted. E02–E09 remain planned.

### E01 follow-up — foliage growth orientation

Per-type Growth Orientation offers Upright (World Up, default) and Terrain Normal. New strokes respect the choice independently for trees and ground cover; existing instances remain authored. The setting persists in Type.N.AlignToTerrainNormal scene/prefab properties. Regression checks cover both choices, nonuniformly scaled/rotated owners, per-type persistence and missing-field fallback.

Validation: updated FoliageCollisionTests passed; FoliageComponent, ViewportPanel and InspectorPanel compiled with MSVC; the isolated editor validation executable linked. Normal MSBuild and interactive backend acceptance retain the limitations recorded above.

### E02 delivery — 2026-10-07

- Added an editor-owned surface-placement session with transient cyan mesh commands. Drag a mesh/prefab from Content Browser to preview and drop to stamp; repeat clicks place additional instances. Controls include yaw, scale, offset, surface-normal alignment, pivot placement, X/Z grid snapping, repeat and target parent. Escape/Finish cancels the session without removing prior stamps.
- Queries terrain and exact rigid mesh triangles without requiring gameplay colliders; collidable surfaces also participate. Grid snapping reprojects onto an upward-facing surface rather than moving the object off the surface. No hit means no preview and no stamp. Placement reuses hierarchy support bounds and converts poses through parent transforms.
- Added a geometry-only prefab/variant load path: hierarchy, meshes and support colliders only; no scripts, lights, audio, physics simulation or environment captures. Ghosts never become scene entities. Actual stamping uses ordinary prefab instantiation and records transform overrides in one existing scene transaction. Parent/asset changes are revalidated before mutation; scene/project/play changes cancel the session.
- Static rigid mesh geometry is previewed; animated geometry is rejected. Procedural effects and behavioral components are not previewed. Source-model drag retains its existing workflow; import/use a mesh or prefab for surface placement. Surface queries currently scan candidate rigid mesh triangles after bounds rejection; no large-scene performance claim is made.
- Added PlutoGESurfacePlacementTests covering hierarchy support, offsets, scale/yaw, rotated/nonuniform parents, collider-free terrain/meshes, transformed normals, no-hit/invalid rays, geometry-only variants, preview isolation, preview/stamp agreement, repeat IDs, parent/asset invalidation, mesh references and prefab snapshot undo/redo.
- Direct MSVC compilation and validation editor link passed against existing RelWithDebInfo libraries. SurfacePlacement, GroundPlacement and SceneHistory suites passed. PrefabVariantTests is blocked at its existing SaveVariant fixture by weakly_canonical access denied for a new temporary path; the unmodified HEAD implementations reproduce the same failure. The new variant-read fixture passes.
- Normal CMake/MSBuild remains blocked by FileTracker initialization E_ACCESSDENIED during dependency regeneration. Artifacts are isolated in out/environment-validation. Interactive acceptance remains unrun: on both OpenGL/Vulkan verify ghost opacity/depth/occlusion, drag delivery, numeric controls, repeat/finish, grid projection, parent selection, Ctrl+Z/Ctrl+Y and scene/project/play transitions. E02 is not marked complete until those checks and a normal build pass.

### E02 drag/drop correction — 2026-10-07

- Removed a redundant parent cursor restore after the placement window: ImGui Begin/End already preserves the parent cursor; SetCursorScreenPos at the post-image position triggered its boundary assertion. Limited the controls window height for small viewports.
- Static imported node metadata is no longer classified as animation. Added one shared editor bind-transform helper used by preview commands, exact mesh queries and support bounds, matching MeshComponent transform order. Skinning/animation clips remain unsupported; invalid/cyclic bind chains are rejected.
- Added regression coverage for static two-level bind nodes, preview/contact/query agreement, cyclic nodes and actual animation metadata. SurfacePlacement and GroundPlacement suites passed; affected sources compiled and the validation editor linked with MSVC. Interactive graphics validation remains pending.

### E02 model/scale correction — 2026-10-07

- Routed imported source-model drops through the same preview/stamp session as meshes and prefabs. Added a shared ModelAsset resolver used by viewport placement and the existing hierarchy workflow, preserving authored sibling mesh preference and imported material bindings. Unimported models fail visibly without scene mutation.
- Enabled skinned/animated asset ghosts in authored bind pose without constructing animation controllers in the prototype. Committed mesh/model stamps retain a valid sibling animation asset. Animated surfaces remain excluded from the rigid surface query.
- New asset selections reset yaw/offset/scale. Added explicit 0.01× centimeter conversion, 1× reset, smaller positive scale values and a world-bounds size readout. No guessed automatic unit conversion or project asset rewrite. Audi source/native geometry is roughly 472×320×1091 units, while UAL is 1.83 units tall; per-asset conversion is necessary.
- Added model fallback/authored mesh selection, missing import, centimeter conversion, skinned ghost and animation-reference regression coverage. SurfacePlacement and GroundPlacement passed; modified targets compiled and the validation editor linked with direct MSVC against existing RelWithDebInfo libraries. A read-only headless probe of the actual CoD assets produced 2 UAL ghost parts and 32 Audi parts; Audi at 0.01× measured 4.72×3.20×10.91 world units. Interactive OpenGL/Vulkan validation and the normal build remain pending as recorded above.

### E02 placement-session release correction — 2026-10-07

- Browser drag release now leaves the placement panel open, including release over its controls or outside the image. Removed implicit drop stamping and drag-end cancellation. The first stamp requires a subsequent viewport click, allowing scale, parent and alignment settings to be changed before creating any entity. Escape/Finish and scene/project/play transitions retain explicit cancellation.
- Viewport compilation and the validation editor link passed with direct MSVC; interactive release/control checks remain pending.
