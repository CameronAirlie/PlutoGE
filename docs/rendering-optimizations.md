# Volumetric, particle, and color-pass optimization

The RHI renderer uses the same shader implementations for optimized and reference paths on Vulkan and OpenGL.

## Clouds and fog

Cloud integration skips secondary lighting for exactly empty samples and stops light rays once transmittance falls below 0.0001. Samples outside the cloud's height/edge envelope skip procedural density noise. Ray-box intersections handle zero direction components without dividing by zero.

Cloud primary and light samples are independently stratified per cell with a stable spatial seed, avoiding coherent midpoint sheets without depending on temporal history. Cloud noise uses integer lattice hashes so shared corners remain identical across floating-point contraction modes. Depth-clipped cloud rays use the same full-resolution texel centre as fog. Parallel box slabs are handled explicitly; small nonzero local directions are preserved so large cloud volumes do not develop grazing-angle cutoffs. The changed lattice hash changes the generated cloud pattern.

Fog and clouds normally trace at half the internal width and height (one quarter of the rays, with dimensions rounded up). The trace stores integrated radiance in RGB and transmittance in alpha. A shared depth-aware reconstruction pass composites these values with the original scene color; scene detail is never downsampled. Trace and reconstruction use matching nearest depth samples. Foreground pixels without a compatible trace sample reject background scattering.

Depth compatibility accounts for the local surface slope before comparing neighboring samples. Limited one-sided depth gradients preserve silhouettes and let continuous grazing surfaces, including the last row at a horizon, retain fog instead of producing horizontal gaps. Prediction uses the full-resolution texel actually sampled by each trace ray, including odd viewport sizes.

Fog rays also reconstruct from that exact full-resolution texel centre. Reconstructing at the reduced-resolution UV while fetching nearest full-resolution depth changes the inferred surface height, particularly from elevated cameras. Ocean intersection distances use a dedicated `R32Float` attachment and matching pipeline; HDR half-float colour precision is insufficient for height-fog endpoints at altitude. Pooled post-process targets include format in their reuse key so toggling ocean/fog cannot reuse a colour attachment as distance data.

Fog uses a finite shadowed march followed by an analytical height-fog integral. **Shadow Detail Distance** controls the march budget, not the extent of the atmosphere. Cascaded shadows also cap that budget at their final view-depth split. Shadow influence fades over the last 20% of the detail range and near the edge of shadow coverage. Beyond it, fog receives unshadowed directional light and the same ambient lighting as the near volume. Missing VSM pages use unshadowed visibility; detailed distant occlusion is intentionally approximated.

Direct light is also attenuated by the fog above each point (`heightFogSunTransmittance`), using the closed-form exponential-height integral toward the light. A dense layer therefore darkens toward its base instead of glowing uniformly, and an infinite homogeneous medium (zero height falloff) receives no direct light. Analytic segments (the distant tail, particle fog and the glass tail) are lit once at their scattering midpoint: the distance before which half of the segment's in-scattering accumulates.

**Noise Strength** (default 0.5, 0 disables), **Noise Scale** (feature size in world units, default 40) and **Noise Wind X/Z** (world units per second) add mean-preserving density variation to the marched near volume. Two octaves of the baked cloud lattice (`CloudNoise.slang`) are centred on their mean and clamped symmetrically, so the analytic tail, glass and particle fog still integrate the same average medium. The variation fades with shadow detail distance so the march meets the tail without a boundary. Wind follows the shader-graph clock, wrapped to the lattice period.

The analytical tail stops at opaque geometry or the ocean surface. Sky rays integrate to infinity: upward rays through exponential height fog have finite optical depth, while horizontal and descending rays through nonzero density become opaque. Lowering the height offset therefore clears upward views but does not make an infinite horizontal medium transparent. Near and far radiance compose front-to-back using the near transmittance, without overlapping density or increasing ray-march steps for distant surfaces.

`FogIntegration.slang` owns the finite and infinite density integrals, direct-light transmittance, scattering midpoints and shadow-distance fade. It is shared by RHI fog, glass, particles and the generated legacy GLSL source, so it stays scalar-only. `FogEnvironment.slang` owns `fogPointRadiance`, the per-point lighting shared by RHI fog, glass and particles: physical-sky ambient light, direct sunlight with a normalized phase function, cloud occlusion and fog self-attenuation. Legacy fog retains its environment prepass. Existing serialized **Max Distance** values load as **Shadow Detail Distance**.

**Max Opacity** scales the height-fog layer's opacity and in-scattering rather than clamping them. A clamp made every surface beyond a fixed distance sit behind the same veil, so the ground lost its depth gradient while distant objects kept constant contrast. Scaling keeps contrast falling with distance and approaches the cap only asymptotically. New effects default to 1, which is unaffected.

## Cloud occlusion

Fog, glass and particle fog dim and tint their lighting under the scene's volumetric clouds. `BuildCloudOcclusion` packs up to `PLUTO_CLOUD_OCCLUSION_VOLUMES` (4) cloud boxes, nearest first, into the frame lighting block. Each box replaces its noise with the noise's mean extinction: `EstimateMeanCloudNoiseDensity` integrates the cloud shader's coverage, erosion and octaves over the shared lattice (cached per authored value), multiplied by the vertical profile mean, Density and Extinction. `CloudOcclusion.slang` then evaluates, per fog point:

- sun transmittance along the box chord toward the light;
- sky visibility along the zenith chord, scaled by the two-stream diffusivity factor 1.66;
- the cloud-base radiance that replaces the blocked sky: the cloud's ambient fill plus isotropically scattered sunlight reaching mid-cloud, in the cloud's colour and albedo.

This is coarse by design. Gaps between individual clouds do not cast light shafts, and broken clouds transmit somewhat more light than their mean. Surfaces are not yet cloud-shadowed. The legacy renderer has no cloud occlusion or density noise.

`BasicPostProcessEffect::volumetricResolutionDivisor` selects the trace scale: 1 uses the full-resolution reference, 2 is the default, and larger values are capped at 4. Missing optional trace/composite shader packages retain the full-resolution path. Reconstruction is spatial; it introduces no independent temporal history or camera-cut invalidation requirements. Small cloud features can soften, and thin foreground features may reject fog where no depth-compatible ray exists. Use divisor 1 where those tradeoffs are unsuitable.

## Horizon haze

Volumetric Fog includes a separate artistic aerial-perspective band for flat worlds. **Horizon Haze** controls its strength (default 1; 0 disables it), **Haze Width** controls its angular width in degrees (default 4), and **Haze Distance** controls the smooth distance onset for surfaces (default 2000 world units). Sky rays receive the distant limit. The band fades away above the horizon and continues as distance-dependent aerial perspective below it. Its colour follows the physical sky at the same azimuth, excluding the sun disc, with a daylight fill that grades toward blue below the horizon. Without a physical sky it uses a blue-tinted ambient/direct-light estimate. Legacy rendering uses its environment prepass.

This is a visual approximation, not a curved-planet atmosphere simulation. Haze accumulates with distance, so `composeHorizonHaze` splits it at the height fog's opacity-weighted scattering distance: haze nearer than the fog veils it and haze beyond lies behind it. Nearby ground fog therefore stays in front of distant aerial perspective instead of being painted over, while a distant fog layer, such as fog over the sea seen from altitude, still recedes into the haze and joins the sky without a seam. Its sky colour and sun fill follow cloud occlusion at the camera, so under an overcast the band takes the cloud-base colour instead of clear blue. Height fog's **Max Opacity** scales only that local layer; haze has independent strength. Existing effect files inherit the new defaults when these parameters are absent. Disable Horizon Haze to retain the previous flat height-fog appearance. For a clear blue sea like an aerial reference, use lower local fog density and let horizon haze supply the distant band.

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
