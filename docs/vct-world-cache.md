# Persistent VCTGI world cache

VCTGI now defaults to a stationary 16 x 16 x 16 probe grid. Each probe stores six directional diffuse irradiance values and normalized distance moments. The outer voxel cascade supplies the probes; it is fixed in world space instead of following the camera. In World Cache mode, both cached shading and its fallback use the stationary field. Camera-following detail is deliberately excluded because switching between the two lighting estimates created a moving box of illumination. This works in the legacy OpenGL effect and the Slang OpenGL/Vulkan renderer.

## Controls

- **World Cache**: enabled by default. Disable to restore camera-following cascades throughout. World Cache uses one stationary voxel source; it does not spend work updating unused near cascades.
- **Cache Size**: default 432 world units, clamped to 16–4096. The actual span is at least the ordinary outer cascade span. The volume is centered on the snapped camera position when initialized and remains there; it is suitable for a bounded arena, not unlimited world streaming. Increasing the span does not increase the number of probes or voxels, but reduces spatial detail and can include more geometry in voxelization.
- **Cache Updates**: default 64 probes per rendered frame, clamped to 1–256. Lower values reduce update work and increase response time. Each probe uses 30 cones with at most 32 samples each. This is a work limit, not a guaranteed millisecond budget.

The two RGBA16F probe atlases occupy 384 KiB. A single voxel volume supplies the stationary source; additional camera-following volumes are not allocated in this mode. Additional lookup and update work means unchanged frame time is not guaranteed. Receivers with fully confident cached lighting skip their per-pixel cone traces.

## Lifetime and blending

A new cache is cleared before use. It visits all 4096 probes before fading in, preventing the initialization sweep from appearing as a moving lighting boundary. At the default budget, the first sweep takes 64 rendered frames after the stationary voxel source has been published. Fade-in then takes about 50 frames. Updates are interleaved spatially.

Publishing a changed stationary field schedules eight sweeps, blending previous probe radiance and distance moments into the new result. After those sweeps, unchanged lighting retains its cache without further probe tracing. Animated geometry or changing lights can keep the update work active. Camera movement does not relocate or clear the probe grid. Viewport resizing preserves it. Scene replacement in the legacy effect, replacement of the adapted RHI effect instance, and renderer shutdown/resource reconfiguration clear it. Direct BasicRenderer callers can set `historyOwner` to distinguish scenes.

Cached irradiance is used throughout the cache region. During warm-up and where probe confidence is low, cone tracing uses only the stationary outer field. There is no camera-distance or near-cascade mask in this blend. This trades near-field voxel detail for stable world-space illumination. Cached irradiance is applied with receiver albedo, metallic response and GI intensity exactly once. The cache is not fed from its own composited output. Invalid probes fall back to cone tracing. At the stationary world's outer boundary, lighting still fades out; finite cache coverage has not been eliminated.

Distance moments, receiver-facing weights and occupied-probe rejection reduce light leaks. Six directional lobes and coarse source voxels approximate visibility; thin walls and small emissive objects can still lose detail. The source retains the existing voxel light-injection/shadow implementation, including its shadow coverage limits. This is not ray-traced DDGI, a baked lightmap, or a disk-persistent cache.

## Validation

The initialization test compiles the legacy shaders and exercises the actual probe compute shader on a hidden OpenGL context. It checks cleared storage, the exact per-dispatch write budget, bounded radiance, preservation of untouched probes, invalidation when geometry covers probes, warm-up and scheduling. OpenGL and Vulkan renderer smoke tests run cache-enabled frames through initial population and fade-in. Visual quality and GPU timings in the arena still need scene-level measurement.

A GPU regression renders the same world-space receiver before and after moving a differently lit near cascade. It requires identical illumination both with a populated cache and during fallback, catching the moving-box failure that compilation and update-budget tests missed.
