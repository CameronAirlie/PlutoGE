# Renderer modernisation: clustered lights, precise synchronisation, bindless GPU-driven opaque, frame graph

This pass makes four structural changes recommended after comparing the RHI renderer with a from-scratch
Vulkan/OpenGL design. Each one keeps the previous path behind a switch, leaves the image unchanged (checked
byte-for-byte against that path), and degrades by device capability rather than by API name: OpenGL 4.3 is the
floor, Vulkan 1.3 the primary target.

| Phase | Switch (default on) | Vulkan | OpenGL |
| --- | --- | --- | --- |
| Clustered local lights | `BasicRenderer::SetClusteredLightingEnabled` | yes | yes |
| Scoped barriers, batched uploads | `PLUTO_VULKAN_CONSERVATIVE_BARRIERS=1` restores all-commands barriers | yes | scoped `glMemoryBarrier` bits |
| Bindless GPU-driven opaque | `BasicRenderer::SetGpuDrivenOpaqueEnabled` | with descriptor indexing and multi-draw indirect | bound path (no core bindless) |
| Frame graph, async compute | `PLUTO_VULKAN_ASYNC_COMPUTE=0` runs async passes inline | with a compute-only queue family and timeline semaphores | inline |

## Phase 1: clustered local lights

`BasicLit.slang` used to evaluate every local light (up to 32) for every shaded fragment, rejecting lights by
distance inside a shader with high register pressure.

- `LightClusterGrid` (`src/LightClusterGrid.*`) fits a 16x9x24 froxel grid (`LightClusterConfig.h`) to the view.
  Depth slices are logarithmic between the nearest and farthest *light reach* along the view axis, not the camera's
  clip range, so slices concentrate where lights are and fragments beyond every light skip the loop.
- `LightClusterCulling.slang` tests each cluster's view-space box against each light's inflated bounding sphere and
  writes one mask word per 32 lights per cluster.
- `LightClusters.slang` finds a fragment's cluster from its own view-space position through the projection the
  culling used, not from `SV_Position`, so jitter, upscaling and viewport offsets cannot misassign it. It visits
  lights in ascending index order, the order of the unculled loop, so sums are bit-identical.
- Unchanged views and light sets reuse the previous masks: no upload and no dispatch.
- Clustering is skipped for non-rigid views (the depth bound assumes distances are preserved) and when the
  shader or compute support is missing; the shader then evaluates every light as before.

Geometry-pass outputs were already slimmed by `GeometryOutputLayout`, which selects the attachments the active post
effects read; the eight-target layout is the diagnostics view only. No format changes were made.

## Phase 2: scoped synchronisation and batched uploads (Vulkan)

- Each image tracks the pipeline stages that accessed it since its last barrier. A transition uses those as its
  source scope instead of `ALL_COMMANDS`; unknown history (first use, parallel segment entry) stays conservative.
  Images in the bindless table always transition conservatively because their reads are not bind-tracked.
- Bound textures and storage images count as accessed by every shader stage. Bindings outlive pipeline switches,
  so the stage of the pipeline bound at bind time does not say which stage reads them. This also fixed a latent
  hazard where a destination scope of `FRAGMENT_SHADER` missed vertex- or compute-stage reads.
- `ICommandContext::PipelineBarrier(before, after)` expresses buffer dependencies with `BarrierAccess` stage and
  kind flags. The light-cluster, occlusion, VSM planning/publishing and GPU-skinning chains use it: a broad entry
  barrier, narrow compute-to-compute barriers inside, and an exit scoped to the consumers. The VCT storage-image
  chains still use `ShaderMemoryBarrier`.
- Storage, vertex and index buffer updates queue on the recording context and are recorded together, between one
  barrier pair, before the next command that could read them. An update overlapping a queued one records the queue
  first, so writes keep their order. Large payloads still stream through frame-local staging.
- OpenGL implements `PipelineBarrier` with the `glMemoryBarrier` bits of the later consumers, and only after shader
  stores.

## Phase 3: bindless materials and GPU-driven opaque draws

- RHI: `IRenderDevice::BindlessTextureCapacity`, `RegisterBindlessTexture` and `ReleaseBindlessTexture`, and
  `GraphicsPipelineDescriptor::bindlessTextures`. Vulkan uses one update-after-bind, partially bound array of
  combined image samplers in descriptor set 4, indexed non-uniformly. A released slot is reused only after the
  submissions that may read it complete. OpenGL 4.3 reports no capacity.
- `BasicLitBindless.slang` is the standard material with its parameters and textures read from storage tables
  (`BindlessMaterials.slang`): the material values become per-invocation statics loaded once per fragment, so the
  shading code is shared with every other `BasicLit` variant. The four vertex entry points now share one transform.
- `GpuDrivenOpaque` (`src/GpuDrivenOpaque.*`) turns eligible draws (standard material, opaque, one object; no graph,
  terrain, glass or outline) into instance and material records. `BindlessMaterialTable` registers material textures
  once, validates records by content and evicts records unused for eight frames.
- Draws are bucketed by pipeline (layout, cull mode, prepass) and vertex/index buffers, and each bucket is one
  multi-draw-indirect call; the command's first instance selects the instance record. Meshes in a geometry-arena
  page share buckets.
- `GpuDrivenVisibility.slang` copies GPU occlusion results into the commands' instance counts, so occlusion culling
  applies without CPU readback.
- The depth prepass, transparency, outlines and every ineligible draw keep the bound path.

## Phase 4: frame graph and async compute

- RHI: `ICommandContext::SupportsAsyncCompute`, `BeginAsyncCompute` and `EndAsyncCompute(consumers)`. Vulkan picks a
  compute-only queue family, shares buffers between the families (`VK_SHARING_MODE_CONCURRENT`) and orders the queues
  with one timeline semaphore each. Beginning an async section submits the frame recorded so far (the fork); ending
  it submits the graphics work recorded meanwhile and makes the next submission wait for the async work only at the
  consumers' pipeline stages (the join). The async recorder binds buffers only, so images never change queue.
- `RenderGraph` (`include/PlutoGE/render/RenderGraph.h`) records passes that declare buffer accesses. It inserts one
  merged barrier per pass where accesses conflict, forks a section at the first async pass, and joins it before the
  first graphics pass that conflicts with the section, waiting only at the stages the remaining passes use those
  buffers with. Without an async queue, async passes run inline and their dependencies become barriers.
- `BasicRenderer` runs light clustering as an async pass beside the shadow and geometry recording. Only fragment
  shading waits for the masks, so vertex, compute and transfer work after the join (VSM planning, occlusion) overlaps
  it.

## Validation

- `PlutoGEVulkanRhiTests` and `PlutoGEOpenGLRhiTests`:
  - `LightClusterChecks.h`: clustered lighting equals the unculled loop byte-for-byte for perspective, moved-light
    and orthographic views; unchanged frames reuse masks; on Vulkan, culling runs on async compute.
  - `GpuDrivenOpaqueChecks.h`: 50 draws in 3 multi-draw calls equal the bound path byte-for-byte for colour,
    occlusion-culled and surface-output layouts.
  - `BufferUploadChecks.h`: five updates are recorded in two batches, and overlapping inline and staged writes keep
    their order.
- `PlutoGERenderGraphTests`: barrier placement, fork/join placement and join stages, section restarts, the inline
  fallback, and light-cluster fitting.
- The full suites also pass with `PLUTO_VULKAN_CONSERVATIVE_BARRIERS=1` and `PLUTO_VULKAN_ASYNC_COMPUTE=0`.

## Measurements (RTX 4070 SUPER, Release, before/after builds alternated)

| Workload | Before | After |
| --- | --- | --- |
| `PlutoGEVulkanRhiTests --benchmark`: 1532 shared-material draws, geometry recording CPU | 0.34 ms | 0.13 ms |
| Same, CPU from frame setup to recording (includes GPU-driven preparation) | 0.17 ms | 0.30 ms |
| ThirdPersonProject startup view, `--frozen-render` (15 draws), scene GPU | 1.67 ms | 1.71-1.77 ms |
| ThirdPersonProject game view (14 draws, 720 triangles), scene GPU | 3.09 ms | 3.12 ms |

On the draw-heavy benchmark the geometry pass records about 0.2 ms less CPU for 1532 draws, and the two CPU spans
together drop from 0.51 ms to 0.44 ms. The sample project's current views are very light (no local-light load, a
dozen draws), so they measure only the fixed cost of the new machinery: the light-cluster dispatch, the visibility
pass and the async fork/join submissions add a few hundredths of a millisecond. Clustered lighting and GPU-driven
draws pay off with many local lights and many draws; measure a representative heavy scene before tuning further.
Benchmark captures differ from the baseline only as much as two baseline runs differ from each other.

## Not done yet

- The depth prepass, alpha-tested and graph materials, terrain and skinned meshes still draw through the bound path.
  Masked materials need the bindless coverage shader; graphs need bindless graph textures.
- GPU-driven commands are not compacted; occluded draws stay as zero-instance commands. An indirect-count RHI call
  would remove them.
- Only light clustering runs on async compute. The graph is ready for heavier independent work (GPU instance culling,
  SSAO, volumetric fog), but those passes use images, and the async recorder does not yet transfer image ownership.
- OpenGL could implement bindless textures through `ARB_bindless_texture` where drivers offer it.
- Without the Vulkan SDK's synchronisation validation layer, barrier correctness rests on image-equivalence tests.
