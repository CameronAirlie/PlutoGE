# Frame pipeline: render thread, parallel animation and visibility, GPU-driven shadow submission

The runtime's frame used to run in order on one thread: simulate, translate the scene into draws, record, submit,
present. This pass overlaps simulation with recording, spreads animation and visibility across the task pool, and cuts
the per-draw cost of submission. Every change leaves the image unchanged and has a switch for comparisons.

## Results

ThirdPersonProject, Kestrel Basin, `PlutoGERuntime --benchmark-project` (240 frames after 240 warm-up, vsync off,
RTX 4070 SUPER), median of three alternating runs of the build before and after:

| Configuration | Mean FPS | Frame p95 | GPU |
| --- | --- | --- | --- |
| Native 720p, before | 91.7 | 13.0 ms | 10.1 ms |
| Native 720p, after | **97.3** | **11.3 ms** | 10.3 ms |
| FSR2 Performance, before | 96.9 | 12.3 ms | 9.2 ms |
| FSR2 Performance, after | **105.2** | **10.3 ms** | 9.5 ms |

This view is GPU-bound (frame time follows GPU time), so most of the CPU saving shows as headroom rather than frame
rate; the render thread's overlap keeps the GPU busier and removes most long frames. CPU work per frame:

| Counter | Before | After |
| --- | --- | --- |
| Shadow recording | 2.9 ms | 1.5 ms |
| VSM shadow-page draw batches | ~1,260 | ~37 |
| Descriptor binds | 7,190 | 5,350 |
| Uniform bytes copied to the frame arena | 7.95 MB | 0.77 MB |
| Scene components (animation sampled in parallel) | 2.32 ms | 2.10 ms |

Next limits in this view are on the GPU: the geometry pass (5.6 ms) and VSM receiver depth (2.1 ms).

## Render thread (runtime, Vulkan)

`RhiRenderService::SetRenderThreadEnabled` (on in the runtime unless the profiler window is open,
`PLUTO_RENDER_THREAD=0` is set, or a project benchmark passes `--serial-render`). A frame then runs as:

```
Game thread:    [update N+1 ][wait][prepare N+1 + UI][update N+2 ][wait]...
Render thread:               [record, submit, present N+1   ]
```

- **The cut.** `RhiSceneRenderer::Render` is `Prepare` then `Record`. `Prepare` does everything that reads the scene,
  assets or render commands (translation, visibility, GI selection, particles, decals, new resource uploads) and returns
  a `PreparedFrame`; `Record` reads only that, the renderer and device resources. The frame borrows the renderer's
  preparation caches, so the next `Prepare` waits for the render thread (`RhiRenderService::WaitForFrame`).
- **Runtime UI** is laid out and drawn on the game thread (scripts edit its documents during the update) into an
  `rhi::DeferredCommandList`, which the render thread replays after the scene. `RmlUiRhiRenderer::BeginFrame(target,
  commands)` records into any context. Frames with overlay cameras (they composite from the scene while recording) and
  the first runtime-UI frame (it creates the UI's pipelines) render on the game thread.
- **Device contract.** `IRenderDevice::SetRecordingThread` names the thread recording frames. While it is set, buffer and
  texture updates and resource destruction from other threads are queued and applied, in request order, when the next
  frame begins (calls made outside the render thread's frame queue behind earlier ones too); resource creation stays
  immediate. Threads bound to a parallel-recording context count as the recording thread. Queue access (submits,
  presents, immediate uploads, idle waits) is serialized by one lock, taken after the device's resource lock, never
  before it.
- **Clock.** The shader-graph clock is pinned per frame (`ScopedShaderGraphClock`): `Prepare` captures it, `Record` and
  its parallel-recording workers read that value while the game advances the global clock.
- **Statistics.** With the render thread, `GetTimingStats` and `GetTemporalUpscalerStatus` return the last completed
  frame, published by `WaitForFrame`. Every other call into the service waits for the frame in flight first; code that
  uses the device's command context directly (the loading screen, frame captures) must call `WaitForFrame` first.

`core::FrameWorker` is the thread: one job in flight, failures rethrown at the next `Wait` or `Submit`.

## Parallel animation and visibility

- **Animation.** Inside `Scene::Update`, `AnimationComponent::Update` advances playback and leaves the pose pending; right
  after the component pass, `AnimationPoseBatch` evaluates every pending pose: mesh resolution, then base poses (sampling,
  retargeting, layers, hierarchy) across the task pool, then IK, ragdoll and timing serially. Base-pose evaluation touches
  only the component and immutable clip and skeleton data. A pose read during the pass is still evaluated on demand, so
  results equal per-component evaluation (`PlutoGEAnimationPoseBatchTests`). `Scene::SetParallelAnimationEnabled`.
- **Visibility.** `Renderer::UpdateRenderCommandLods` (LOD selection and frustum, distance and size tests) and instance
  culling run in ordered ranges (`TaskPool::RunRanges`), concatenated in order. `Renderer::SetParallelVisibilityEnabled`.
- **Task pool.** Batches from different threads now run at the same time and share the workers, so the game thread's
  batches no longer wait for the render thread's parallel recording (or the reverse).

## GPU-driven submission

- **Persistent uniforms.** A uniform buffer bound after 8 frames without an update moves into the arena's persistent
  region, so binding it stops copying it into every frame's arena; an update moves it back, and its persistent range is
  freed once the frames that may read it retire. Applies to every renderer on Vulkan.
- **Geometry arena (Vulkan).** Rigid meshes suballocate shared vertex and index buffers (`GeometryArena`, 256K vertices
  and 1M indices per page). Indices are stored rebased by the mesh's first vertex, so the vertex offset is always zero,
  and `BasicMesh::BaseIndex` locates the mesh. Most passes bind with `BasicRenderer::BindMeshBuffers` (index buffer at the
  mesh's offset, mesh-relative first indices, as before); VSM binds at zero with absolute first indices so draws of
  different meshes can batch. Freed ranges are reused once the device's completed submission passes the one pending at
  release (`IRenderDevice::PendingSubmission`, `CompletedSubmission`). OpenGL keeps a buffer per mesh: it rejects indirect
  draws from an index buffer bound at an offset. `BasicRenderer::SetGeometryArenaEnabled`.
- **Batched shadow pages.** Every VSM caster chunk, rigid or instanced, draws through the batched pipeline: a draw table
  (model count, alpha mode, first model) and a model table replace per-chunk uniforms, so consecutive chunks in one arena
  page and with the same alpha texture are one multi-draw indirect call. `lighting.virtualShadowBatching = false` draws
  one chunk at a time (the reference the checks compare against).

## Checks

- `PlutoGETaskPoolTests` (ranges, concurrent batches), `PlutoGEFrameWorkerTests`, `PlutoGEAnimationPoseBatchTests`,
  `PlutoGERetainedRenderSceneTests` (parallel against serial visibility).
- `PlutoGEVulkanRhiTests`: `CheckRenderThreadRecording` (frames prepared on one thread and recorded on another equal
  single-threaded frames; cross-thread device calls are deferred), `CheckGeometryArena` (identical images with the arena
  and unbatched pages; 294 page batches become 6 for 48 distinct meshes), `CheckParallelRecording` (each mode re-records
  its VSM pages).
- `PlutoGERmlUiAntialiasingTests` (Vulkan and OpenGL): UI replayed from a deferred list equals UI recorded directly.

## Not done yet

- **One draw per pipeline for the main geometry pass** needs bindless material textures (descriptor indexing in every
  material shader, with an OpenGL fallback); the arena and draw tables are the groundwork.
- **The editor** still renders serially; its ImGui frame would need capturing like the runtime UI.
- About 100 KB of immutable uniform buffers are created every frame on Basin; finding their producer would remove them.
