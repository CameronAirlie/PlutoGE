# Parallel command recording and object streaming

Recording the frame's GPU commands was the largest CPU cost of a full scene (5.5 to 6.5 ms on the ThirdPersonProject's
Kestrel Basin), all of it serial on the main thread and dominated by per-draw work. Two changes address it:

1. **Parallel recording:** shadows and geometry record on worker threads into their own command contexts.
2. **Object streaming:** single objects read their constants from shared per-frame blocks indexed by first instance,
   so a draw no longer binds a buffer of its own.

Both leave the image unchanged (`CheckParallelRecording` in `PlutoGEVulkanRhiTests` compares every combination pixel
for pixel) and both have switches for comparisons: `BasicRenderer::SetParallelRecordingEnabled` and
`SetObjectStreamingEnabled`.

## Results

ThirdPersonProject, Kestrel Basin with the Works guards awake, `PlutoGERuntime --benchmark-project` (240 frames after
240 warm-up, vsync off, Ryzen 7 9700X), mean of three alternating runs of the build before and after:

| View | Frame | Frame p95 | Render CPU | Command recording | GPU |
| --- | --- | --- | --- | --- | --- |
| Works gate, before | 17.99 ms | 21.60 ms | 11.05 ms | 7.64 ms | 10.21 ms |
| Works gate, after | **13.60 ms** | 17.24 ms | 7.41 ms | **4.31 ms** | 9.46 ms |
| Hall, before | 15.44 ms | 19.56 ms | 9.99 ms | 7.18 ms | 10.12 ms |
| Hall, after | **11.93 ms** | 15.31 ms | 6.71 ms | **4.03 ms** | 9.17 ms |

Uncontended, the geometry pass alone went from 3.2 to 2.8 ms with streaming (descriptor-set binds per frame from about
11,000 to 6,600). Parallel recording first gained little because every uniform bind took the device lock; the uniform
arena is lock-free for that reason.

## Layers

| Layer | Piece | Role |
| --- | --- | --- |
| Core | `core::TaskPool` | Fixed worker threads; a batch runs `task(0..n-1)` with the caller taking part, rethrows the first exception, runs nested batches inline; batches from different threads run at once, sharing the workers. `TaskPool::Shared()` is the engine's pool. |
| RHI | `ICommandContext::BeginParallelRecording` / `EndParallelRecording` | Hands out N contexts whose commands execute in order inside the frame. `MaxParallelRecordingContexts() == 0` means serial (OpenGL). `BindToCurrentThread` routes device calls that record commands (buffer uploads) on a thread into a context. |
| RHI | `DrawIndexedInstanced(..., firstInstance)`, `SupportsFirstInstance()` | Lets a draw select its entry in an instanced constant block. |
| Vulkan | `VulkanRecorder` | Everything that turns RHI calls into commands: binding and descriptor state, draws, barriers, uploads, GPU scopes. |
| Vulkan | `VulkanCommandContext` | Owns the frame (command buffer, fence, pools, staging, timestamps, submission) and parallel sections. |
| Vulkan | `VulkanSegmentContext` | One parallel context: a secondary command buffer with its own descriptor pools and its own view of image layouts. |
| Renderer | `ParallelRecording` | Takes jobs in GPU order; runs them on the task pool, each into its own context, when the device allows, else serially on the frame's context. |
| Renderer | `ObjectStream` | Per-frame blocks of object constants that draws index by first instance. |

## How a parallel section works (Vulkan)

- `BeginParallelRecording(n)` begins `n` secondary command buffers for the current frame slot (the frame already
  waited for that slot's fence). The frame context records nothing until the section ends.
- Each segment tracks image layouts locally. The first time a segment uses an image it records no barrier; instead it
  notes the layout it assumes there (the layout requested, or the image's layout when the section began).
- `EndParallelRecording` executes the segments in order. Before each, the frame context transitions every image the
  segment uses into its assumed layout and adds a full memory barrier (consecutive passes over the same attachments
  need that dependency), then adopts the segment's final layouts. Executed secondaries leave the primary's bindings
  undefined, so the frame context forgets them.
- Shared device state is thread-safe: the handle registries never move resources and look up without locks; resource
  creation, updates and destruction are serialized by `Impl::sharedMutex`; the per-frame uniform arena allocates,
  marks dirty and counts without locks (two threads making one buffer resident both copy identical data); staging,
  readbacks and timestamp queries are reserved through `VulkanFrameServices`.
- RHI bindings are per slot and resolved at draw time, so resources may be bound with no pipeline bound (the next
  pipeline bind marks every descriptor set dirty; a draw still requires a pipeline).

## What a job may do

A job may read anything prepared before `ParallelRecording::Run` and create or update resources through the device.
It may write only state no other job of the same run touches, unless the two share a chain
(`ParallelRecording::AddAfter`): a chain records on one thread in the order added, each job into its own context, so a
later job may continue CPU state an earlier one left while other jobs record in parallel between them in GPU order.
`ParallelRecording::Reserve` takes a GPU position for a job given later with `Set`, when the job's state is prepared
after jobs that must execute after it. `BasicRenderer` records up to four jobs, in this GPU order:

- **Coverage depth** (with the depth prepass): opaque and alpha-tested surfaces' exact depth. Reserved first so VSM
  planning can read it; chained with the geometry job, whose draw and binding state it shares.
- **VSM planning** (when VSM is active): receiver depth and the GPU planning dispatches
  (`VirtualShadowMaps::RecordPlanning`). When the coverage depth holds every VSM receiver, planning reads it and
  renders no receiver depth of its own. Writes nothing on the CPU beyond its own context.
- **Shadows:** VSM caster pages and the residency pass (`RecordPages`, the longest shadow recording), the cascaded
  fallback, point shadows. Writes only shadow caches, shadow buffers and the shadow fields of `m_frameStats`.
- **Geometry:** occlusion, opaque and alpha-tested draws, outlines, and collecting transparent draws.
  Writes only the geometry parameter, material and object-stream caches and the geometry fields of `m_frameStats`.

Everything both read is prepared before `Run`: VSM preparation and receivers, geometry targets, the point shadow atlas
(`EnsurePointShadowAtlas`), the frame's camera constants. Helpers called from a job record into the context they are
given (`OcclusionCulling::Record` takes it; `recordDraw` takes it as its first parameter), never into
`GetImmediateContext()`.

## Object streaming

Single objects (not multi-instance draws, not transparent, not occlusion-indirect draws whose arguments the GPU writes)
append their constants to the frame's `ObjectStream` and draw as one instance of the existing instanced vertex input,
with `firstInstance` selecting their entry. The object binding changes once per 64 draws instead of every draw, which
removes most per-draw descriptor-set binds. Per-draw backface culling still follows the draw's own transform
(`GeometryPipeline` decides it from the draw's instance count, not the vertex input). Backends without first-instance
support keep binding a buffer per draw.

## Extending

- **More parallelism:** add jobs to the same `ParallelRecording` run (`Add` returns the job's index into
  `JobMilliseconds`). Split at GPU scope boundaries: a scope begun in one job cannot end in another, and consumers
  read some scopes by name (the VSM budget policy reads `RHI Virtual Shadow Pages`), so a pass split across jobs
  needs a scope per part. Splitting a pass across jobs needs a render pass
  per job (attachments load instead of clear after the first) and per-job state for anything a job writes (binding
  trackers, parameter caches, an object stream each). Pipeline selection (`GeometryPipeline`) creates pipelines lazily
  and is not thread-safe; resolve pipelines before `Run` or guard that cache before calling it from two jobs.
- **Profiling:** CPU trace scopes are per thread and only the main thread is captured, so scopes inside jobs appear
  only for the job the main thread happens to run. `BasicRendererTimingStats` reports each pass's recording time and
  `parallelRecording`; the project benchmark CSV has `parallel_recording`, `descriptor_cpu_ms` and `descriptor_binds`.
- **Other backends:** a backend opts in by returning a nonzero `MaxParallelRecordingContexts` and implementing the
  section; until then callers record serially through the same code.
