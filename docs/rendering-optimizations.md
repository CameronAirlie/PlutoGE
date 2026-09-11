# Volumetric, particle, and color-pass optimization

The RHI renderer uses the same shader implementations for optimized and reference paths on Vulkan and OpenGL.

## Clouds and fog

Cloud integration skips secondary lighting for exactly empty samples and stops light rays once transmittance falls below 0.0001. Samples outside the cloud's height/edge envelope skip procedural density noise. Ray-box intersections handle zero direction components without dividing by zero.

Fog and clouds normally trace at half the internal width and height (one quarter of the rays, with dimensions rounded up). The trace stores integrated radiance in RGB and transmittance in alpha. A shared depth-aware reconstruction pass composites these values with the original scene color; scene detail is never downsampled. Trace and reconstruction use matching nearest depth samples. Foreground pixels without a compatible trace sample reject background scattering.

`BasicPostProcessEffect::volumetricResolutionDivisor` selects the trace scale: 1 uses the full-resolution reference, 2 is the default, and larger values are capped at 4. Missing optional trace/composite shader packages retain the full-resolution path. Reconstruction is spatial; it introduces no independent temporal history or camera-cut invalidation requirements. Small cloud features can soften, and thin foreground features may reject fog where no depth-compatible ray exists. Use divisor 1 where those tradeoffs are unsuitable.

## Particles

`BasicParticleDraw::instances` contains one 48-byte record per billboard, replacing six 64-byte CPU-expanded vertices. The vertex shader expands a shared indexed quad, retaining material, flipbook, soft-depth and smoke-lighting behavior. Alpha sorting remains back-to-front. `vertices` is the alternative path for trails and existing callers; use one representation per packet.

The scene renderer culls actual live particle bounds before sorting and material preparation, so an offscreen emitter's particles remain visible when they travel into view. Trail segments use conservative bounds. Particle packets and sorting scratch retain capacity between frames, sort depths are computed once, and GPU buffers grow geometrically. Vulkan storage uploads are recorded before beginning particle rendering, preserving prior-frame reads.

## Tone and color

Adjacent tone-mapping, gamma and color-grading operations execute in authored order in one fullscreen pass, in groups of at most eight. Repeated grades remain distinct operations; they are not deduplicated. Effects that resample neighboring pixels, including chromatic aberration and FXAA, end a group. Standalone and fused passes share `ColorOperations.slang` so grading formulas cannot drift. Removing intermediate FP16 stores can produce small rounding differences.

Each recorded pass retains its own parameters and output attachment. Fusion reduces attachment count without reintroducing the renderer's previously problematic attachment ping-pong behavior. The profiler reports these groups as `RHI Fused Tone and Color`.

## Regression checks

Build `PlutoGEVulkanRhiTests` and `PlutoGEOpenGLRhiTests`, then run:

```powershell
ctest --test-dir out/build/msvc-nvidia -C RelWithDebInfo -R RenderOptimizationTests --output-on-failure
```

The focused checks compare optimized and reference pixel output, verify Vulkan draw reduction, exercise repeated grades and grouping boundaries, rotated/overlapping particle instances across consecutive frames, culling boundaries, odd viewport dimensions, empty media, and the full-resolution volumetric fallback. Existing RHI tests cover the surrounding rendering pipeline. Performance in a particular scene still needs a new capture at the same camera, resolution, settings, and build configuration; presentation waiting is separate from these rendering changes.
