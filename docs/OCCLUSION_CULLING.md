# GPU occlusion culling

## Implementation plan and architecture

1. Keep visibility decisions on the GPU and use current-frame depth. CPU readback is diagnostic only.
2. Isolate depth resources, reverse-Z hierarchy construction, bounds tests, indirect commands, and delayed counters in `OcclusionCulling`.
3. Integrate only with main-pass single-instance draws. Preserve shadow/GI lists and all motion-history bookkeeping.
4. Expose Off, Measure, and Cull in the profiler. Default to Off until total scene timings establish a benefit for the scene.
5. Validate image parity and rejection counts on visible/hidden geometry, camera changes and jitter, VSM reuse, invalid bounds, partial exposure, instance fallback, masked coverage, and odd viewport sizes.

## Operation

The profiler's **Occlusion culling** selector controls editor scene rendering:

- **Off**: no occlusion resources are dispatched.
- **Measure**: run the same depth/hierarchy/test passes as Cull, but submit all main geometry.
- **Cull**: set fully hidden draws' indirect instance counts to zero.

`RHI Occlusion Depth`, `RHI Occlusion Hierarchy`, and `RHI Occlusion Test` expose GPU costs. Delayed counters report tested bounds, hidden draws, hidden triangles, and the originating occlusion frame. Existing geometry submission counts still describe submitted commands, not GPU-surviving geometry. Compare total scene GPU time, not just geometry time. Measure/Cull counters are asynchronously observed and are not aligned with the current CPU frame.

VSM receiver depth is reused only for compatible opaque single-instance receivers with no temporal jitter or shader graphs. Otherwise a dedicated current-frame depth pass rasterizes eligible opaque meshes with the same camera jitter as the main pass. Alpha-tested, transparent, outline, shader-graph, and multi-instance draws do not supply occluder depth. Missing occluders reduce effectiveness, not correctness.

The hierarchy stores minimum depth for reverse Z. Each level covers 2x2 cells of the preceding level. Odd edges include uncovered samples; uncovered depth must propagate rather than becoming a fictitious occluder. Bounds tests use the current world-space AABB of rigid submeshes where available (including negative/nonuniform scale and shear), otherwise the world-space bounding sphere's enclosing box. They project the eight corners, expand the screen rectangle by a pixel, and sample every overlapping cell at the selected level. Strict depth separation and a bias avoid self-occlusion. Invalid bounds, near/far-plane crossings, vertex-deforming shader graphs, and unsupported instance groups fail open. Fragment-only graph draws can be occludees, but do not provide occluder depth.

An inconclusive coarse depth test retries progressively finer levels, including original depth texels, with a maximum budget of 256 depth reads per draw. Budget exhaustion leaves the draw visible. This can recover hidden objects whose coarse cells overlapped sky outside their actual projected bounds. Diagnostics partition draws into tested, unsupported, invalid-bound, and clipped/offscreen categories; additional counters report tight bounds, refinement attempts, recovered rejections, and budget limits.

Each single-instance draw has a stable indirect slot for the current input span. Instanced, transparent, and outline rendering keeps its existing submission path. No readback controls drawing, and no prior-frame visibility is trusted. Resource lifetime and upload versioning are provided by the RHI.

## Limits and follow-up work

This saves main-pass vertex/raster/shading work for fully hidden eligible draws. It does not skip CPU animation/skinning, depth-pass geometry, shadow casters, GI contributors, or the CPU's material binding work. The fallback depth pass can outweigh savings in open scenes or scenes with few hidden objects. Per-instance compaction and partial-mesh/meshlet culling are outside this implementation.

The supplied world-space bounds must conservatively enclose current geometry. The scene translator already provides updated deformed bounds; unknown bounds remain visible. Extending support to vertex graphs requires trustworthy deformed bounds and coverage-equivalent depth shaders.

Regression entry point: `PlutoGEVulkanRhiTests --occlusion` (`PlutoGEVulkanOcclusionTests` in CTest). The OpenGL harness also accepts `--occlusion` on compatible hardware.
