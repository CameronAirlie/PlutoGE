# Standoff performance plan (2026-10-15)

Source: editor profiling capture of CoD's Standoff map in play mode, frames 159505–159744
(240 retained frames, Vulkan RHI). The pasted export was truncated after the first frame's
trace, so per-frame detail below comes from frame 159505. Percentiles come from the 240-frame
summary.

## Capture context and caveats

| Context | Value | Effect on the numbers |
|---|---|---|
| Host | Editor play mode, debugger attached | CPU times are inflated and spikier than a standalone build |
| VSync | On | Hides GPU headroom; the frame never exceeds the display interval |
| Resolution | 1175×738 internal → 1762×1107 output (0.667 scale, temporal upscaler) | GPU costs are at roughly 0.87 MP before upscaling |
| Scene load | 11 bot soldiers, 12 animation components, 839 render commands (450 visible) | Typical Hardpoint population |
| Memory | 5.7 GiB private / 1.9 GiB resident; 6.1 M cumulative page faults | Worth checking for paging during spikes |

The summary pools 240 frames. Scopes with `n=480` (Geometry, Post Process) are observed twice
per frame, once for the main camera and once for the weapon overlay camera, so their means
mix two very different passes. Use the p50 (overlay) and p95 (main) values. GPU values are
asynchronous observations; parent scopes include their children.

## Baseline

| Metric | p50 | p95 | p99 | Max |
|---|---|---|---|---|
| CPU frame | 6.70 ms | 8.11 ms | 10.06 ms | 10.77 ms |
| Scene GPU | 2.42 ms | 2.83 ms | 3.12 ms | 3.16 ms |

### CPU, frame 159505 (6.70 ms)

| Area | Time | Notes |
|---|---|---|
| Viewport render | 3.72 ms | Three scene recordings per frame (see P0-1, P0-2) |
| ↳ Render textures (`RenderCamera`) | 0.68 ms | Weapon rendered into a texture nothing displays |
| ↳ Main camera recording | 2.12 ms | Shadows 0.53 ms, geometry 0.38 ms, post 0.20 ms |
| ↳ Camera stack composition (weapon overlay) | 0.84 ms | Includes its own shadow pass, 0.62 ms inclusive |
| Scene update | 2.21 ms | |
| ↳ Animation components (12) | 0.71 ms | Layers 0.76 ms > base sampling 0.42 ms (28 pose evaluations) |
| ↳ Skeleton attachments (23) | 0.29 ms | "Joint 6 - Head" attachments 0.03–0.06 ms each; hand sockets 0.001 ms |
| ↳ Scripts (39) | 0.30 ms | MultiplayerSession 0.11, PlayerHud 0.07, PlayerInventory 0.05 |
| ↳ Mesh submission | 0.60 ms | Retained scene: 82 producers, 10 publications, 16 rebuilds |
| Editor UI | 0.57 ms | Scene Hierarchy 0.27 ms (editor-only) |
| Runtime UI | 0.33 ms | Draw 0.15, input/layout 0.12 |
| Present | 0.14 ms | No fence waits |

Other per-frame RHI figures: 3.58 MB uniform upload (0.33 ms CPU, 0 bytes persistent),
2207 descriptor binds, 3066 indexed draws, and shadow packet preparation recorded three
times (0.14 ms each).

### GPU (Scene p50 2.42 ms)

| Pass | p50 | p95 | Notes |
|---|---|---|---|
| Temporal upscaler | 0.62 | 0.96 | Largest single pass; bimodal |
| Main opaque geometry | — | 0.98 | n=480; the main pass is the p95 value (~1.0 ms for 271 k triangles) |
| Post Process (main) | — | 0.38 | n=480; the overlay observation is ~0 |
| Virtual shadow pages | 0.20 | 0.24 | |
| Output post process | 0.20 | 0.20 | Max 0.55 (single spike) |
| VSM GPU planning | 0.16 | 0.17 | |
| Transparency | 0.14 | 0.17 | 6 glass snapshots, 105 k copied pixels |
| Bloom | 0.11 | 0.11 | Max 0.46 (single spike) |
| Shadow cascade 0 (overlay) | 0.07 | 0.08 | Overlay camera only |
| VSM receiver depth | 0.06 | 0.08 | |

VSM state: 366 pages requested, pool 256, **110 overflow every frame**, 14 dirty pages
updated with about 1.55 M caster/page triangles, resolution scale 8, 54 MB allocated.

## Targets

| Metric | Now (p50 / p99) | Target (p50 / p99) |
|---|---|---|
| CPU frame, editor, debugger detached | 6.7 / 10.1 ms | ≤ 5.0 / ≤ 7.0 ms |
| Scene GPU, VSync off | 2.42 / 3.12 ms | ≤ 2.0 / ≤ 2.4 ms |
| VSM overflow pages | 110 | 0 |
| Scene recordings per frame | 3 | 2 (main + overlay) |

## Work items

Priority reflects expected savings divided by effort. Each item gives the evidence, the
change, the expected effect, and how to verify it. Estimates come from frame 159505 and must
be re-measured.

### P0: quick wins (about 1.3 ms CPU)

**P0-1. Remove the unused `RenderCamera` (CoD).**
- **Evidence:** `RenderCamera` (Player camera child, entity 21269 in Standoff) renders
  `Weapon`-tagged geometry into `project://Textures/RenderTexture.plutorendertexture` every
  frame. It is present in all five map scenes. Its only consumer, `RenderMaterial`, is not
  referenced by any scene, prefab, script or RML document. It costs 0.68 ms CPU (its own
  command translation, shadow packet preparation and VSM work) plus GPU time.
- **Change:** delete the camera, or disable it until a feature needs it. If it is kept for a
  future portrait, render it on demand.
- **Expected:** about 0.7 ms CPU, plus a share of GPU geometry and shadow time.
- **Verify:** "Render textures" disappears from the trace; one fewer "Command translation"
  and "Shadow packet preparation" scope.

**P0-2. Weapon overlay camera re-records shadows (engine).**
- **Evidence:** inside "Camera stack composition", the overlay scene records "Shadows" with
  0.57 ms self, a second shadow packet preparation (0.14 ms) and a cascade shadow (0.53 ms CPU
  recording, 0.08 ms GPU). The overlay draws only `Weapon`-tagged geometry (22 draws, 0.07 ms GPU
  geometry).
- **Change:** let overlay cameras reuse the base camera's directional shadow data for the same
  light. Otherwise add a per-camera "Cast/receive shadows" option and default it off for
  overlays. Confirm in `BasicRenderer` camera stack composition how the overlay chooses its
  shadow path first.
- **Expected:** about 0.5–0.6 ms CPU, 0.08 ms GPU. Check the viewmodel still looks right in
  shadowed areas (receive-only from the base camera's maps).
- **Verify:** no "Shadow Cascade 0" observation; camera stack composition CPU ≤ 0.3 ms.

**P0-3. Share shadow packet preparation across views (engine).**
- **Evidence:** "Shadow packet preparation" runs once per scene recording (0.141, 0.144 and
  0.142 ms) over the same 815 shadow candidates.
- **Change:** cache prepared shadow packets per light per frame, keyed by the caster set.
- **Expected:** about 0.14 ms per extra view after P0-1/P0-2; larger in scenes with more views.

### P1: renderer (about 1 ms CPU, about 0.5 ms GPU)

**P1-1. VSM page pool overflow.**
- **Evidence:** 366 pages requested for a 256-page pool, 110 overflowing every frame, which
  loses shadow resolution and adds churn.
- **Change:** size the pool from the requested peak (for example 384–512 pages, about 27 MB
  extra at current page size). Alternatively lower the resolution scale from 8 and compare
  quality.
- **Expected:** stable shadow quality; fewer re-requests. GPU may rise slightly; measure it.
- **Dirty updates:** the 14 dirty pages carry 1.55 M caster/page triangles, which suggests
  animated bots invalidate fine pages. Evaluate rendering dynamic skinned casters into coarse
  levels only, or caching static casters separately from dynamic ones.

**P1-2. Temporal upscaler cost and variance.**
- **Evidence:** p50 0.62 ms, p95 0.96 ms GPU, the largest pass. It is bimodal, which suggests
  periodic extra work such as history invalidation, reactive masks or a resize path.
- **Change:**
  - Find what drives the 0.96 ms frames (correlate with camera cuts, resize and transparency).
  - Measure native rendering against the 0.667 scale. At this resolution the upscaler costs
    about 70% of the main opaque pass.
  - Test the cheaper quality modes.
- **Expected:** 0.3 ms GPU at p95 if variance is removed.

**P1-3. Uniform upload volume.**
- **Evidence:** 3.58 MB uploaded per frame (0.33 ms CPU) with 0 persistent bytes, even though
  625 geometry parameter sets are reused.
- **Change:** move static per-object constants (world matrix, material indices) to persistent
  buffers updated only on change. Upload only dynamic objects and per-view data each frame.
- **Expected:** at least 0.25 ms CPU; less PCIe traffic.

**P1-4. Main opaque geometry (~1.0 ms GPU, 3066 indexed draws, 2207 descriptor binds).**
- **Change:**
  - Try occlusion mode 1 (measure), then 2 (cull), on Standoff. It is currently 0 (off);
    see [occlusion culling](OCCLUSION_CULLING.md). The village blocks long sightlines.
  - Audit LOD selection: 264 visible commands have several LODs, while 271 k triangles are
    submitted at 0.87 MP.
  - Reduce descriptor rebinds by sorting draws by material and descriptor set.
- **Expected:** 0.2–0.4 ms GPU and lower CPU recording.

**P1-5. Post-process CPU recording (0.96 ms inclusive).**
- Re-measure after P0-2, since camera stack composition is inside it. If more than 0.3 ms
  remains, profile per-effect recording (bloom, auto exposure, output) for per-frame
  pipeline or descriptor creation.

### P2: simulation CPU (about 0.5 ms)

**P2-1. Animation layers cost more than base sampling.**
- **Evidence:** layers 0.76 ms against 0.42 ms base sampling over 28 pose evaluations
  (12 components, 0.06 ms each; max 0.12 ms on Enemy 122058).
- **Change:**
  - Profile the layer blend for per-bone mask lookups and temporary allocations.
  - Update bots in parallel; animation is independent per actor.
  - Add animation LOD: reduce update rate for distant or off-screen bots.
- **Expected:** 0.3–0.5 ms CPU.

**P2-2. Head skeleton attachments are expensive.**
- **Evidence:** "Joint 6 - Head" attachments cost 0.03–0.06 ms each (11 bots), while hand
  sockets cost 0.001 ms. The head attachment drives the `HeadCollider` hitbox, so it probably
  pays for a physics transform update or a joint-chain walk.
- **Change:** read the joint world matrix from the animation pose output instead of
  recomputing it; batch collider pose updates per frame.
- **Expected:** about 0.25 ms CPU.

**P2-3. Mesh submission still 0.60 ms with a retained render scene.**
- Break down per producer. Skinned and animated producers probably republish each frame
  (10 publications, 16 command rebuilds). Publish transforms only for dynamic producers and
  keep static producers leased.

**P2-4. Scripts (0.30 ms): low priority.**
- MultiplayerSession 0.11 ms includes 0.03 ms physics query synchronisation. PlayerHud
  0.07 ms is mostly minimap DOM writes, which are already diffed.

### P3: frame-time tail and editor overhead

**P3-1. CPU p99 10.1 ms against p50 6.7 ms.**
- The pasted export lacks the worst frames.
- Re-capture with the debugger detached and export the full capture to a file. Inspect the
  frames above p95.
- Also check page faults during spikes, given 5.7 GiB private memory. Bloom (max 0.46) and
  output post process (max 0.55) show one-off GPU spikes that likely line up with them.

**P3-2. Editor-only costs.**
- The Scene Hierarchy panel costs 0.27 ms. That matters for editor iteration, not shipped
  builds. Cache the flattened tree between hierarchy changes.

## Implementation status (2026-10-16)

None of the figures above have been re-measured yet. Follow the measurement
protocol below before closing any item.

| Item | Status | Change |
|---|---|---|
| P0-1 | Partly done | `RenderCamera` removed from `Standoff.plutoscene`. It remains in Main, Shipment, Terminal and Foundry, and in the `Tools/Native` smoke copies. `RenderMaterial` and `RenderTexture` assets are kept. |
| P0-2 | Done | `BasicRenderer` publishes its directional VSM receivers (`GetDirectionalShadowReceivers`). `RhiCameraStackCompositor` passes them to world-lit overlays (`ReuseDirectionalShadowsForFrame`). The overlay skips directional caster preparation, signatures, cascades and VSM recording. Reuse requires the same light direction and no `FilterLightsByTags`. Covered by `PlutoGE{Vulkan,OpenGL}VsmReceiverReuseTests`. |
| P0-3 | Deferred | After P0-1 and P0-2, Standoff prepares shadow packets once per frame, so sharing them across views saves nothing there. Revisit for scenes with live render-texture cameras. |
| P1-1 | Done (content) | Standoff's directional light uses `VSM Pool Pages` 576, the 24×24 tile tier. Any value from 257 to 576 allocates this tier (about 42 MB more depth and colour than the 256 tier). The dirty-page caster cost is not addressed. |
| P1-2, P1-4, P1-5, P2-3, P3-1 | Open | These are measurement and investigation items. |
| P1-3 | Open | Persistent per-object uniforms are an RHI change. Size it after re-measuring. |
| P2-1 | Open | Parallel animation updates and animation LOD need a thread-safety audit of `AnimationComponent` and its scene timing hooks first. |
| P2-2 | Partly done | Skeleton attachments now publish one combined local transform (`Entity::SetLocalTransform`), so a moving bone invalidates its subtree once instead of up to three times. Re-measure to confirm whether the head cost was caused by this or by world-transform evaluation. |
| P3-2 | Done | The hierarchy panel collects component types for the filter combo only while the combo is open, not on every frame. |

## Measurement protocol

1. Release editor, debugger detached, Standoff Hardpoint with 11 bots.
2. Use the same spawn and a 10-second camera path (stationary and turning segments).
3. Record three 240-frame captures with VSync on (CPU), and three with VSync off (GPU).
4. Export each capture to text and compare:
   `python tools/Compare-ProfilerCaptures.py before.txt after.txt`
   (it separates shadow reuse/update frames and reports context mismatches).
5. Record p50, p95 and p99 for CPU frame, Scene GPU, camera stack composition CPU, shadow
   recording CPU, VSM overflow and upscaler GPU.
6. Repeat the final measurement in a standalone runtime build to confirm the editor-free
   numbers.

## Expected outcome

| Item | CPU saving | GPU saving | Area |
|---|---|---|---|
| P0-1 Remove RenderCamera | ~0.7 ms | small | CoD content |
| P0-2 Overlay shadow reuse | ~0.55 ms | ~0.08 ms | Engine renderer |
| P0-3 Shared shadow packets | ~0.1 ms | — | Engine renderer |
| P1-1 VSM pool sizing | — | quality / stability | Engine renderer |
| P1-2 Upscaler variance | — | ~0.3 ms p95 | Engine renderer |
| P1-3 Persistent uniforms | ~0.25 ms | — | Engine RHI |
| P1-4 Occlusion, LOD, binds | ~0.1 ms | 0.2–0.4 ms | Engine renderer |
| P2-1 Animation | 0.3–0.5 ms | — | Engine animation |
| P2-2 Head attachments | ~0.25 ms | — | Engine scene / CoD |
| **Total (estimate)** | **~2.2–2.5 ms** | **~0.6–0.8 ms** | |

That would bring the editor CPU frame from 6.7 ms to about 4.3–4.5 ms p50. It would also
bring Scene GPU p50 to about 1.7–1.9 ms, within the targets above, before any P3 tail work.

## Open questions

- Was `RenderCamera` intended for a weapon preview or inventory portrait? If so, the
  inventory already uses cached `SetScenePortrait` portraits, which render only on change.
- Should the viewmodel receive world shadows? This decides whether P0-2 reuses base shadows
  or disables them.
- Is the 0.667 render scale a deliberate quality preset for this GPU, or a default?
