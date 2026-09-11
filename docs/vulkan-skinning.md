# Vulkan skeletal animation

The RHI scene renderer now deforms weighted mesh vertices using the animation
component's joint matrices. Each mesh/pose owner has its own cached vertex stream,
so characters sharing an asset can play different animations. Positions, normals
and tangents are deformed on the CPU; Vulkan renders the resulting geometry.
Unchanged palettes reuse the cache. CPU cost scales with animated vertex count.

Vertex updates are recorded before render passes, with transfer/vertex-read
barriers protecting frames in flight. Lit and shadow passes share the deformed
stream. Geometry revisions invalidate cached shadows; posed bounds cover limbs.
Previous positions supply skeletal motion vectors and reset on pause, temporal
reset or visibility gaps. Static meshes retain their shared upload cache.
Skinned meshes remain excluded from voxel GI contributions.

Validation (RelWithDebInfo, AMD Radeon Graphics):

- `PlutoGEVulkanRhiTests --skinning`: rendered pixels, independent actors,
  submeshes, shadow invalidation, motion, pause/reset, asynchronous uploads,
  weights and normal transforms.
- Append a ShadowSouls Paladin GLB path and optional output directory to render
  bind, idle and heavy-attack fixtures. Actual asset checks passed with 22,101
  and 24,065 changed pixels between these poses.
- Full Vulkan suite, `--temporal-motion`, `--opaque-batching`, and full OpenGL
  RHI suite passed after rebuilding the shared vertex layout and shaders.

Rebuild both editor/runtime and shader outputs. ShadowSouls `Edit.ps1` and
`Play.ps1` select the patched local builds; the older Program Files executables
could not be overwritten because Windows denied access.

## CPU kernel optimization

The captured editor profile spent 57.6 ms in command translation, versus 1.1 ms
in animation sampling. The local build's configuration optimization flags were
empty. Skinning now lives in a separately optimized translation unit, uses an
explicit affine 3x4 blend and cofactor normals, and reuses the per-character output
allocation. Other engine code retains its existing debugging settings. The kernel
retains symbols, but stepping through its optimized instructions is less direct.
A dedicated CPU trace scope identifies skeletal vertex deformation.

On the 9,669-vertex Paladin, a 40-pose benchmark measured the preserved original
kernel at 27.79 ms/pose and the new kernel at 0.458 ms/pose (about 61x faster).
These are kernel measurements, not a claim of equivalent whole-frame speedup.
The benchmark and reference comparison run with the imported GLB skinning test.
