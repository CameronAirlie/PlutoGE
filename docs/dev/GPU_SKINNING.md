# GPU skinning

The performance capture showed CPU deformation spikes up to 4.18 ms despite
cross-camera reuse. This pass replaces recurring vertex deformation/upload work
with compute while retaining the existing rendering, animation and IK interfaces.

## Design

1. Cache immutable bind vertices once per source mesh/revision, shared by actors.
2. Keep current/previous bone palettes and a compute-written vertex buffer per
   animated owner. Record one dispatch per changed mesh/pose, before any draws.
3. Write the existing `BasicVertex` layout. All materials, coverage, shadows,
   transparency and motion-vector passes consume that same buffer.
4. Keep conservative per-bone shadow bounds on the CPU. They require no vertex
   readback. Invalid bounds fail open. Camera-stack/render-texture reuse remains
   independent of per-camera temporal history and material-cache aging.
5. Retain CPU skinning when the shader package lacks compute skinning support.

`RhiGpuSkinning` owns compute setup, packed source data and palette dispatches.
`BasicRenderer` owns geometry buffers and records pending dispatches inside its
existing submission. `RhiSceneRenderer` owns pose caching, history decisions,
source sharing and conservative bounds. Compute never submits or waits itself.

`VertexStorage` explicitly describes compute-written vertex data in the RHI.
Vulkan allocates uninitialized output in preferred device-local memory; OpenGL
uses a buffer bound for both SSBO writes and vertex reads. Shader memory barriers
include vertex fetch and transfer readback, and bracket the dispatch batch.

The shader uses byte-address layouts with compile-time C++ stride/offset checks:
88 bytes per source vertex and 72 per output vertex. Palette matrices are stored
as four column vectors. Weight filtering/normalization, inverse-transpose normals,
tangent orthogonalization, reflections, unweighted and degenerate fallbacks follow
the CPU implementation. Motion positions are recomputed from the previous palette
instead of reading/writing the output in place.

An unchanged pose needs one settling dispatch after movement; subsequent stationary
frames skip work. Missing history, topology changes, palette-size changes and
temporal resets use the current palette for previous positions. Shared cameras
consume completed geometry without dispatching it again.

For nine 65,157-vertex bots with 63 joints, recurring full vertex uploads of about
40 MiB are replaced by approximately 71 KiB of current/previous palettes. This is
a transfer-volume calculation, not a measured FPS improvement. Outputs stay on
the GPU, and there is no recurring CPU vertex allocation/copy on the compute path.

## Validation

`PlutoGEVulkanGpuSkinningTests` and `PlutoGEOpenGLGpuSkinningTests` compare GPU
readbacks against the independent CPU reference, including invalid/non-normalized
weights, reflected/nonuniform/degenerate transforms, UV channels and independent
actors across in-flight submissions. Rendering checks cover bone-only movement,
shadows, motion settling/reset, asset invalidation, camera sharing, material edits,
large streams, and render-texture camera reordering/removal. Existing CPU skinning
tests retain coverage for the fallback path.

Profiler captures expose dispatch/vertex/palette-byte counts and an asynchronous
`RHI GPU skinning` GPU scope. CPU deformation and skinned vertex upload timings
should be zero on the compute path; a camera borrowing geometry should also report
zero GPU skinning dispatches. Shader packages must be rebuilt and shipped together
with editor/runtime binaries.

## Measured validation (2026-10-06)

Native editor/runtime builds succeeded. Both GPU skinning suites pass on Vulkan
and OpenGL. Seven regression checks also pass: CPU skinning unit checks, Vulkan
and OpenGL CPU fallback rendering, Vulkan VSM membership, camera stacks,
render textures and large-buffer uploads.

The game project's `Tools/benchmark_bot_gpu_skinning.cpp` compares nine actual
65,157-vertex soldiers (414 submesh draws) on Vulkan. Across 40 measured changed-
pose frames after 12 warm-up frames, command translation averaged 7.37 ms on CPU
skinning versus 0.30 ms on GPU skinning. Active renderer CPU time, excluding frame
fence waits, averaged 10.19 ms versus 0.43 ms. The asynchronous GPU skinning scope
averaged 0.68 ms. Both modes use identical 256x256 lit geometry with shadows off;
this isolates renderer work and does not predict full-game FPS.

Both modes rendered all samples and drained their geometry submissions. The
standalone benchmark stalled while releasing the device/assets afterward and
was terminated. This shutdown limitation also occurred in earlier Vulkan runtime
smoke runs; a clean whole-game shutdown has not been established by this benchmark.
