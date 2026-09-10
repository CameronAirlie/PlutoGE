# VCT secondary diffuse bounce

## Implementation

1. Voxelize the direct-light and emissive field once. Alongside the original integer radiance/coverage sums, capture a packed surface normal and diffuse reflectance per occupied voxel.
2. Resolve the direct field and directional mips. Gather one unit-strength secondary bounce into a separate RGBA16F volume using `VCTBounceUpdate.slang` and the shared four-cone integration in `VCTSecondaryBounce.slang`.
3. Publish direct + strength × secondary and rebuild the directional mips. Refresh the stationary probe cache when its source cascade completes.

Both the RHI OpenGL/Vulkan renderer and the legacy OpenGL effect use this compute shader. The sampled direct atlas stays immutable until the gather finishes. The original integer sums remain intact, allowing strength changes to recombine the cached fields without repeating geometry or cone tracing. Geometry, material, light and volume changes invalidate the unit bounce. Partial gathers never become visible; disabling midway preserves the direct field and a later enable can resume the gather.

This lets illuminated walls become secondary sources for other surfaces. Black and fully metallic surfaces contribute no diffuse secondary light. A 32-bit record stores five bits per normal/reflectance component and a validity bit. Atomic maximum selects one coherent representative where surfaces overlap, independent of fragment ordering. This is an approximation: mixed materials/normals within one voxel are represented by one surface, and gathering starts at the voxel center. Reflectance is clamped before quantization, with a maximum encoded value of 29/31.

## Controls and latency

**Secondary Bounce** defaults to 1 and ranges from 0 to 1. Zero restores the direct/emissive field and skips unfinished secondary gathering. Cached strength changes publish one cascade per frame. The RHI parameter is lane 5.y; zero-initialized BasicRenderer packets retain the disabled behavior. The scene adapter preserves this lane when applying camera parameters.

For a room, enable **Inject Local Lights** to include point/spot lights. **Local Light Bounce** controls their injected energy; **Intensity** controls the final GI result. These controls retain their existing meanings.

The compute budget targets 32,768 voxel cells per frame, rounded to groups of four Z slices (minimum four slices). Empty and nonreflecting cells skip cone tracing:

| Resolution | Slices/frame | Gather frames/cascade | Added memory/cascade |
| --- | ---: | ---: | ---: |
| 64³ | 8 | 8 | 3 MiB |
| 128³ | 4 | 32 | 24 MiB |

The extra memory is an R32UI surface record and RGBA16F unit-bounce texture. Original accumulation textures are reused for the direct contribution. Each reflecting cell traces four cones with at most 24 steps and early exit on occlusion/volume boundaries. GPU cost still depends on occupancy and trace length; the work budget is not a millisecond guarantee. Publication performs the resolve and directional mip generation in one frame. `RHI VCT Secondary Gather` and `RHI VCT Volume Publish` expose those costs in GPU profiling.

An unchanged scene performs no secondary updates and retains the existing screen-space trace cost. Initial geometry voxelization and subsequent geometry/light changes still use the existing progressive draw/triangle budgets. The improvement removes the secondary geometry replay and prevents strength changes from restarting primary voxelization; it does not make initial loading or dynamic source reinjection instantaneous. Stationary probes retain their own update budget.

## Validation and Bistro comparison

Vulkan and OpenGL rendering regressions check the eight-frame gather, one-frame cached strength changes for a single cascade, material colour bleeding, black/metal rejection, stable results without feedback, final received light on another surface, source removal and world-cache behavior. Floating-point GPU checks exercise both voxelizers feeding the actual compute shader, checking gain, reflected energy, metallic/dark rejection and a 0.001 incident source. Existing emission coverage, dense accumulation, scene-adapter and preview-display regressions also pass.

The optional read-only project capture is:

```powershell
PlutoGEEngineGraphicsApiTests.exe --vct-scene <project-root> <scene-file> <output-directory> <frames-per-setting> [eye-x eye-y eye-z target-x target-y target-z]
```

It renders off/on/off/on through the scene adapter at 320×180, with a fixed camera/exposure and no temporal filtering. It captures the first 20 frames after each change, then every 100 frames, and reports VCT GPU scopes when available.

Bistro was measured with 1,533 commands, 2,828,278 triangles, 100 emissive draws, 64³ voxels, two cascades and an eight-command geometry budget. After the initial direct field settled:

- First enable: visible near-field change at frame 8; both cascades settled at frame 16. Previously the first captured change was frame 400.
- Cached disable/re-enable: near-field change at frame 1; both cascades settled at frame 2.
- Disabling returned exactly to the reference image. The first-enable result and cached-enable result matched at the corresponding cascade publication.

At 60 FPS these frame counts correspond to approximately 133/267 ms for first enable and 17/33 ms for cached changes. These are response latencies, not GPU execution times. Captures are in `out/vct-bistro-compute-timing/`, including `comparison.png`.

On this machine's RTX 4070 SUPER, Vulkan timestamps from the same isolated capture measured 0.005–0.018 ms per gather slice dispatch (16 samples; median 0.014 ms) and 0.186–0.190 ms per cascade publication including directional mips (six samples; median 0.189 ms). These measurements use Bistro's 64³/two-cascade configuration and a 320×180 fixed view; they are not full editor frame times or a guarantee for other scenes/hardware. The timing log is `out/build/vct-bistro-compute-timing.log`.

## Existing precision and display behavior

Direct radiance accumulation retains its 1/4095 precision and bounded integer sums, preserving faint emission and preventing wrap under heavy overlap. Coverage remains independently encoded. The RHI **Indirect Only** preview retains fixed exposure 1, gamma 2.2 and ACES display mapping; raw voxel diagnostic views retain their existing mapping.

This is one additional diffuse bounce within the voxel coverage. Volume extent, coarse filtering, thin walls, material quantization and existing local-light injection limits still affect the result. No additional local-light shadow maps or unlimited recursive bounces are introduced.
