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
