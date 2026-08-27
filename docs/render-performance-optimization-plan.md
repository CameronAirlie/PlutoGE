# Render Performance Optimisation Plan

Constraint: preserve all post-process effects and visual fidelity.

## 2026-08-27 profile pass

- [x] Correct scene timing attribution: audio previously appeared inside render submission.
- [x] Retain audio component/state scratch capacity across frames.
- [x] Resolve clip paths only for audible emitters and reuse the result for one-shots.
- [x] De-synchronize recurring audio-occlusion refreshes while preserving their
  80-120 ms cadence and full ray sample count.
- [x] Bound audio-occlusion work to two emitter refreshes per frame (one above
  48 active emitters), preventing residual refresh collisions from creating spikes.
- [x] Update cached Bullet query transforms and broadphase AABBs only for colliders
  whose world transform changed, rather than all colliders before each frame's first query.
- [x] Expose audio as its own editor-profiler metric for follow-up measurement.
- [x] Rebuild the complete RelWithDebInfo editor.

The supplied capture's apparent 8.12 ms scene render-submission cost was mostly audio:
mesh, terrain, and foliage submission accounted for approximately 0.19 ms. GPU work is
led by shadows (3.81 ms) and post-processing (2.92 ms), but changing their quality or
effect set is intentionally outside this fidelity-preserving pass. The next captured
profile can now distinguish backend audio, occlusion queries, and true submission cost.

## 1080p GPU pass

- [ ] Remove the redundant `RGB32F` world-position G-buffer attachment after migrating every
  consumer to depth reconstruction. Ambient lighting, direct lighting, SSR, and decals have
  been migrated; LPV, SSGI, RSM, VCTGI, and shared post-process bindings remain.
- [x] Move the expensive lens-flare ghost, halo, and glare synthesis to its existing
  half-resolution bright-signal domain, followed by a native-resolution two-sample composite.
- [x] Enable volumetric fog's temporal, depth-aware half-resolution path in the CoD preset.
- [x] Reuse directional fog-shadow visibility for two adjacent integration steps while retaining
  the preset's 32 density-integration steps and native-resolution bilateral composite.
- [x] Replace the volumetric-cloud nine-fetch tent upsample with its mathematically equivalent
  four-fetch bilinear form at native output resolution.
- [x] Honour the directional shadow filter's configured render scale in production and use the
  existing depth/normal-aware reconstruction for the native-resolution lighting mask.
- [x] Scissor spot-light accumulation against a conservative minimal sphere around the shader's
  actual finite cone instead of the much larger point-light range sphere.
- [x] Front-load exact direct-light rejection: background, unlit/baked-static materials, and
  pixels outside local-light attenuation now exit before unnecessary material G-buffer reads.
- [x] Tune the CoD main-scene cloud trace scale from 0.5 to 0.4, retaining 24 primary steps,
  three light steps, temporal jitter, native-resolution tent reconstruction, and final TAA.
- [x] Reject analytically zero-density cloud-border samples before procedural FBM evaluation.
- [x] Reject SSR rays before marching when their mathematically bounded roughness contribution is
  below one 10-bit display step; retain the full 48-step trace for contributing surfaces.
- [x] Trace SSR radiance and confidence at half resolution, then reconstruct and composite at
  native resolution with depth/normal-aware cross filtering. The configured ray distance,
  48-step march, five-step binary refinement, material response, and source alpha are retained.

- [x] Hierarchical cluster accept/reject culling using cached aggregate foliage bounds.
- [x] Renderer-owned reusable visibility scratch buffers, removing per-frame vector allocations
  without changing the public `RenderCommand` layout.
- [x] Static GPU-resident geometry instance arena with a separate triple-buffered stream for
  dynamic, skinned, and partially visible instance data. Static topology/LOD signatures avoid
  redundant uploads while indirect groups preserve cross-command batching within each arena.
- [x] Capacity-retaining eight-slot ring for shadow instance and indirect-command uploads.
- [x] Cached static shadow caster ordering and transformed instance bounds; per-region membership
  remains exact and dynamic for scrolling cascades.
- [x] Unified command-level visibility, distance, projected-size, and LOD traversal, followed by
  per-instance culling only for candidate clusters.
- [ ] Optional GPU-driven culling and indirect-command generation. *(The resident static arena and
  explicit submitted-snapshot ownership required by GPU compaction are now in place.)*
- [x] Renderer-wide OpenGL state caching for framebuffer/read-draw bindings, viewport, common
  capabilities, active texture unit, and per-unit 2D/3D/cubemap bindings. Cache state is reset at
  context and external RmlUi boundaries, and deleted resource IDs are invalidated before reuse.
- [x] Pre-generated indexed uniform names across lighting, transparent, ocean, fog, LPV, VCTGI,
  and SSAO paths; shader locations and values remain cached by `Shader`.
- [ ] Uniform buffers for shared camera, light, cascade, and effect constants.
- [ ] Full build, tests, profiler comparison, and visual-equivalence validation.

## Validation notes

- `git diff --check` passes for the implementation changes.
- The `PlutoGERender` RelWithDebInfo library rebuilt successfully after items 1 and 2
  (`PlutoGERender.lib`, 2026-08-17 11:46:05). The command runner timed out while MSBuild continued
  in the background, so validation uses the updated target artifact rather than its missing console exit status.
- A compact-index field was initially added to `RenderCommand`, but this exposed an ABI mismatch
  when the editor and engine libraries were rebuilt at different times. The field was removed and
  the original layout restored; pooled transform scratch storage now lives privately in `Renderer`.
- The complete RelWithDebInfo editor rebuilt successfully with the ABI-safe layout
  (`PlutoGEEditor.exe`, 2026-08-17 11:58:44).
- The shadow upload ring and cached caster ordering both pass the RelWithDebInfo `PlutoGERender` build.
- Static shadow instance bounds are cached per immutable instance snapshot and mesh/submesh, with
  weak ownership validation and topology-triggered pruning. Dynamic/skinned casters bypass the cache.
- Command LOD selection and aggregate visibility now share one traversal and camera calculation.
  The complete RelWithDebInfo editor linked successfully (`PlutoGEEditor.exe`, 2026-08-17 12:30:37).
- Indexed uniform names are generated once per fixed-size uniform family instead of formatting and
  allocating strings inside render loops.
- Shadow-map filtering is now configured only when depth textures are created; lighting and
  transparency no longer reapply immutable `GL_NEAREST` parameters on every bind.
- The complete RelWithDebInfo editor rebuilt successfully after the half-resolution SSR trace
  and native-resolution bilateral composite were added.
