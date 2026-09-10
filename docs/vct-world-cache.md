# Persistent VCTGI world cache

World Cache supplements voxel cone tracing with a stationary 16 x 16 x 16 probe grid. It retains detailed local voxel cascades: nearby surfaces contribute radiance and occlusion before a cone can use cached lighting. The stationary outer volume supplies the probes in the legacy OpenGL effect and the Slang OpenGL/Vulkan renderer.

## Controls

- **World Cache**: enabled by default. The cache source occupies the last cascade slot; a slot is added when capacity permits, up to three total. Disabling it restores ordinary camera-following cascades.
- **Cache Size**: default 432 world units, clamped to 16-4096. The actual span is at least the requested ordinary outer cascade span. It is centered on the snapped camera position when initialized and remains fixed. Increasing its span reduces voxel and probe spatial detail; it does not increase their resolution.
- **Cache Updates**: default 64 probes per rendered frame, clamped to 1-256. Lower values reduce update work and increase response time. Each probe uses 30 cones with at most 32 samples each.

With 128 voxels, three cascades, Volume Size 48 and Cache Size 432, both modes retain 48-, 144- and 432-unit volumes. The nearest voxels remain 0.375 units wide. World Cache must not collapse this configuration to one 432-unit volume with 3.375-unit voxels.

The two RGBA16F probe atlases occupy 384 KiB. Detailed cascades remain allocated and updated, so cache mode no longer saves their memory or voxelization work. Cache lookup can shorten distant traces, but does not guarantee a frame-time improvement.

## Lighting integration

Every receiver begins tracing its cones through the detailed voxel field. Cached lighting cannot replace the entire receiver's indirect illumination. A cone becomes eligible for the cache only when its diameter exceeds the probe spacing (27 units at the default cache size). Its handoff weight ramps smoothly over one to two probe spacings, multiplied by probe confidence and cache warm-up.

A cache lookup estimates the remaining distant contribution at that point along the cone. It is weighted by the cone's remaining transmission: nearby opaque geometry blocks it, and previously accumulated local radiance is retained. The corresponding fraction of the remaining path is consumed so cached and traced light are not simply added twice. Short traces and cones blocked by nearby geometry may never use the cache. Low-confidence lookups continue ordinary tracing through the cascades.

Receiver albedo, metallic response and GI intensity are applied once after averaging the cones. There is no camera-distance mask that switches a receiver wholesale between cached and uncached GI. The finite outer volume still limits coverage. Broad directional probes remain an approximation of distant diffuse lighting, especially around thin walls and small emitters.

Emissive rigid submeshes use their source LOD for GI, independently of the camera's visible LOD. Both voxelizers conservatively rasterize the projected bounds of emissive triangles, then clip the original triangle against each unit raster cell. Only the actual overlap contributes radiance and opacity. Material UVs, normals and deposited world positions come from a point inside that overlap. Non-emissive occluders retain ordinary rasterization.

Resolve retains fractional coverage in RGB rather than dividing it back out: summed premultiplied radiance is normalized only when accumulated coverage exceeds one full footprint. Directional mip generation and cone compositing already consume premultiplied radiance. This prevents a small fixture from becoming a full-voxel light source and makes isolated planar emitters conserve projected energy across tessellation and voxel size changes, within storage quantization.

This remains an approximation: the base field is isotropic, a tilted triangle's overlap is deposited at one representative depth per projected cell, and overlapping surfaces share a voxel. Alpha textures are sampled at the representative point rather than integrated over the overlap. Fixed-point accumulation can lose extremely small, dim contributions. Conservative bounds increase raster work for large diagonal emissive triangles; no extra voxel textures are allocated. These changes do not add photometric units, directional base-level emission, or inverse-square attenuation to cone samples.

## Lifetime

A new cache is cleared and visits all 4096 probes before fading in. At the default update budget, the first sweep takes 64 rendered frames after the stationary source is published, followed by about 50 frames of fade-in. Updates are interleaved spatially.

Publishing a changed stationary field schedules eight sweeps, blending previous probe radiance and distance moments. Unchanged lighting retains the cache after those sweeps. Camera movement does not relocate or clear the grid. Viewport resizing preserves it; scene/effect replacement and resource reconfiguration clear it. Direct BasicRenderer callers can set `historyOwner` to distinguish scenes.

Distance moments interpolate continuously between directional lobes. Unoccluded rays use the cache diagonal as their miss distance; leaving the voxel field does not invent a blocker at its edge. Occupied probes are invalidated. This is not ray-traced DDGI, a baked lightmap, or a disk-persistent cache.

The RHI renderer supplies GI with the unculled scene and its actual materials, including surfaces that do not cast direct shadows. Shadow-only draw packets and camera-visible lists are not substitutes for this input. Direct BasicRenderer callers that cull visible draws should supply the optional `giDraws` scene span.

RHI voxel rebuilds snapshot their draws, instance transforms and lighting. Directional injection uses a dedicated 1024-square shadow map fitted to the voxel volume with upstream caster coverage, independent of camera rotation and presentation shadow cascades. Shadow drawing is budgeted in chunks before voxelization; the map stays fixed for the entire job. The staging depth/color pair adds 8 MiB and extra draw work when a volume rebuilds. Publishing a volume invalidates screen-space GI history. The legacy effect retains its existing shadow snapshot path. Finite caster coverage, opaque shadow treatment of cutouts and voxel filtering still limit shadow accuracy.

## Validation

OpenGL GPU checks exercise the actual probe compute shader, its clear/update budget, persistence, occupied-probe invalidation, empty-field distance moments and scheduling. Trace checks require local radiance to survive cold, partially populated and fully populated caches; an opaque local blocker must reject cached light, while an unobstructed distant path must still use it. A legacy lookup check covers continuity across directional probe boundaries.

A Vulkan image regression rasterizes and voxelizes a red emissive ceiling over a diffuse floor using the reported 128/3/48/432 configuration, 64 updates, six cones and an 81-unit trace limit. It compares local bounce lighting with the cache disabled, warming up and populated. Replacing the corrected tracing shader with the old full-receiver cache implementation fails this comparison. These controlled checks do not establish visual parity for every scene or eliminate coarse far-field approximation errors.

The same regression turns the camera 180 degrees, removes the emitter from visible draws and requires identical GI on the first frame looking back. A separate initialization comparison moves the presentation shadow map completely away from the scene and requires the same GI as initialization with full presentation-shadow coverage. Both exercise Vulkan with progressive update budgets.

A Vulkan voxel-radiance check renders a 0.02-unit emissive submesh at seven alignments within a 0.375-unit voxel. Every alignment must inject radiance; the original unexpanded rasterizer fails this check. The source radiance is 16 so its coverage-weighted result remains visible in the 8-bit readback; the check no longer requires a tiny emitter to look like a filled voxel.

`PlutoGEVctEmissionCoverageTests` reads floating-point GPU voxel data from both the Slang and legacy source pipelines. It compares integrated radiance times projected voxel area against emitter radiance times physical area across two resolutions, three source sizes, four grid alignments, two tessellations, three dominant axes and reversed winding. It also checks unit emission and compares a directional mip against reference premultiplied compositing, including the existing opacity dilation. Tolerances account for integer-atomic and half-float quantization.

## RHI local injection and trace resolution

The RHI voxelizer injects up to 16 point/spot lights whose ranges intersect each cascade when Inject Local Lights is enabled. Spot cones and range falloff match the legacy injector. These local sources are unshadowed during injection; voxel occlusion still affects subsequent cone tracing. Light position, range, color, intensity, spot direction, and the injection toggle invalidate the field. Progressive jobs retain their starting light snapshot.

Trace Quality now controls RHI cone-trace dimensions: High traces at half width/height, Balanced at quarter width/height. Depth/normal-aware reconstruction feeds full-resolution temporal history and compositing. Debug views retain full-resolution tracing. This reduces cone-trace invocations by four or sixteen respectively, without reducing voxel resolution; total GPU savings depend on scene and rebuild costs. Directional shadow staging is skipped when directional intensity is zero.

The focused regressions cover point/spot injection, stationary-cache refresh after light color changes, the injection toggle, and trace-divisor changes under broad lighting. At very small output sizes, quarter-resolution tracing can undersample narrow spotlight bounce; High retains more of that detail. These checks establish behavior, not a measured scene-wide frame-time speedup.

**Local Light Bounce** scales point/spot radiance during injection (0–16, default 1). Use 2–4 for stronger local bounce without raising direct illumination, directional GI, or emissive sources. Both render paths rebuild affected volumes after edits. This is an artistic gain; radiance storage still clamps at 16, so very bright sources can saturate. RHI packets encode gain minus one in lane 5.x so zero-initialized packets preserve unit gain.
