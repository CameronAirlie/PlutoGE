# Bistro shadow CPU performance

The September 10 capture is CPU limited: 19.04 ms captured CPU frame versus
5.553 ms GPU scene time. Shadow recording costs 5.14 ms CPU; voxel GI costs
0.19 ms GPU. The VSM memory counter describes allocated memory, not uploads.

## Changes

- Rigid shadow receivers and casters use a 96-byte uniform block instead of
  the 4,128-byte block containing 64 transforms. Instanced draws retain the
  existing 64-transform path; a single remaining instance uses the rigid path.
- Unchanged draw uniforms and caster inputs retain their uploaded contents.
- An unchanged VSM frame skips receiver rasterization, GPU planning and page
  submissions once asynchronous counters confirm that those exact inputs have
  no dirty, updated, deferred or overflowing pages. Input changes immediately
  resume work. Pending adaptive resolution refinement also prevents reuse.
- The profiler identifies reused frames and explicitly labels allocated memory.

The reuse check compares camera/clipmap parameters, viewport, budgets, draw
transforms, mesh identity/revision, alpha parameters, texture handles, draw
ranges and caster signatures. Delayed feedback from before the most recent
input change cannot authorize reuse. No synchronous GPU readback is added.

## Reproduction

`PlutoGEEngineGraphicsApiTests --vsm-scene <project> <scene> <output-directory> 180`

This read-only project benchmark renders 180 warm-up frames, then stationary
and moving-camera runs at 1603 x 672. It uses a controlled GI-only post-process
setup and virtual directional shadows, rather than the complete editor stack.
Optional six trailing coordinates specify the camera eye and target. Run with
no concurrent compilation and compare the same build configuration.

For `S:/PlutoGE/SCGI/Assets/Scenes/bistro.plutoscene`, the tested view contains
1,532 commands and 2,828,266 triangles. Its existing update budget leaves three
pages deferred, so whole-frame reuse deliberately does not activate there.
Uniform uploads nevertheless fall from approximately 12.7 MB to 0.38 MB per
frame. This does not change scene settings or GI quality.

With compilation stopped, the repeated Debug baseline measured 12.88 ms
stationary / 11.98 ms moving; the final compact path measured 11.16 / 10.96 ms.
The same updated benchmark in RelWithDebInfo measured 4.45 / 4.62 ms.
These are render-service wall times, not complete editor frame times or an FPS
prediction. The Debug before/after GI captures were byte-identical.

Regression coverage includes stationary reuse, camera and caster changes,
65-instance chunks, alpha masking, removal, budget deferral, and switching
between cascaded and virtual shadows. The focused performance test checks that
unchanged, completed shadows stop recording redundant indexed draws.

Validation: all 12 selected CTest shadow/GI checks passed, as did the Vulkan
and OpenGL `--shadows-only` visual suites (including the 65-instance check).
