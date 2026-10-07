# Soldier geometry and occlusion implementation

## Plan and scope

1. Correct the Blender-to-engine primary UV convention and regenerate tangents.
2. Generate offline, index-only soldier LODs using the shared import pipeline.
3. Use current-pose bounds for skinned occlusion and test visibility transitions.
4. Compare a matching gameplay profile before considering static-world HLOD.

The gameplay soldier now contains 96,129 / 62,723 / 43,301 / 27,195 triangles
at LOD0–3 (35%, 55%, and 72% fewer triangles in the additional levels).
Vertices, skin influences, rig, material slots and LOD0 indices remain intact.
LODs use existing projected-radius thresholds (260 / 150 / 80 pixels).
Simplification protects borders and considers normals, UVs and joint weights.
Submeshes exceeding the simplifier attribute limit retain LOD0 conservatively.
These index-only LODs reduce rasterization work, but retain the full skinning
vertex stream. They do not promise a corresponding reduction in CPU skinning.

## Asset preparation

`Tools/export_bot_assets.py` in CoD now writes `(u, 1-v)` and requests tangent
generation rather than supplying arbitrary tangent axes. The existing gameplay
asset was converted once; do not flip it again. The full-resolution source asset
and Blender sources were preserved. An original gameplay backup is stored in
`out/performance-soldier/Soldier_Game.original.plutomesh` in this engine checkout.

Build `PlutoGEPrepareNativeMeshLods`, then pass the project root, an absolute
native input path, and a fresh absolute output path. Use `--flip-v` only for an
old, unconverted Blender export. Omit it for new exports. The tool refuses to
overwrite outputs or process an input that already contains additional LODs.
Review the prepared artifact before replacing the existing registered asset.
The soldier migration validator lives at `CoD/Tools/validate_soldier_lods.py`;
it compares an old unconverted input with the prepared converted result.

Zero-length source normals now use a finite fallback while generating tangents.
The asset validator checks finite normalized tangents, UV conversion, original
topology, rig bytes, material references, and bounds containing every LOD vertex.

## Occlusion behavior and remaining measurement

CPU and GPU skinning supply a current-pose AABB to the existing conservative
world-space occlusion bounds path. Invalid bounds fail open. Shadow bounds
retain their existing sphere behavior. Tests compare culling against unculled
images as an animated actor hides, emerges and hides again.

Occlusion remains opt-in through the existing profiler Off / Measure / Cull
controls. Its depth coverage pass still processes geometry before rejection;
it does not eliminate initial skinning or shadow work. Compare matching captures
in all three modes before enabling it broadly. No gameplay FPS improvement or
visual LOD quality has been measured in a running game in this change.

HLOD is deferred until static map groups have been profiled: merging animated
soldiers would conflict with independent animation and visibility. Static-world
HLOD needs authored clusters, proxy material handling and streaming policy,
which should be designed around an actual map rather than added speculatively.

## Validation completed

RelWithDebInfo editor, runtime, offline preparation tool and affected test targets
built successfully. All seven focused tests passed: mesh LOD generation plus
OpenGL/Vulkan occlusion, CPU skinning rendering and GPU skinning rendering.
The installed gameplay mesh passed the standalone migration validator.
Exporter and validator Python syntax checks and source whitespace checks passed.
