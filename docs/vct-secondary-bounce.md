# VCT secondary diffuse bounce

## Plan and implementation

1. Resolve the existing direct-light and emissive voxel field and its directional mips.
2. Replay the immutable voxelization job with a smaller triangle budget. At each surface, gather incident light from that first-pass field and multiply by diffuse material reflectance.
3. Resolve direct + emissive + secondary radiance, regenerate the directional mips, and refresh the stationary probe cache when its source cascade completes.

The legacy OpenGL effect and the Slang OpenGL/Vulkan renderer use the same cone integration source, `VCTSecondaryBounce.slang`. The replay retains the original geometry, material, light and shadow snapshot. Accumulation images are cleared between passes; the sampled atlas stays read-only throughout the replay. The result is a finite extra bounce, not recursive feedback from previous frames.

This allows a wall lit by an emissive surface or injected local light to become a secondary source for other surfaces. It preserves material colour bleeding and diffuse metallic response. Black surfaces and fully metallic surfaces do not produce diffuse secondary light. Reflectance is bounded to 0.95 per colour channel, and the existing radiance storage limit remains in effect.

## Controls

**Secondary Bounce** appears in the Voxel Cone Traced GI settings. Its default is 1; its range is 0–1. Zero skips the additional pass and restores the original single-pass field. Intermediate values reduce the secondary contribution, but use the same work budget as 1. Changes rebuild the field. The setting is serialized through the existing post-process parameter system and forwarded to the RHI renderer.

For a room, start at 1 and enable **Inject Local Lights** if point/spot lights should contribute. **Local Light Bounce** still scales only injected local lights; **Intensity** controls the final GI result. Existing scenes without an explicit Secondary Bounce value receive the effect's default of 1. Direct `BasicRenderer` callers set parameter lane 5.y; zero-initialized packets keep the original disabled behavior.

## Performance and limits

- No additional voxel textures or surface buffers are allocated. Existing accumulation volumes are reused.
- An unchanged field does no secondary rebuild work. Final screen-space tracing has the same sample count as before.
- Each additional surface evaluation uses four cones, at most 24 steps each, with early termination on occlusion or leaving the cascade. Directional interpolation reads three texture samples per step.
- Secondary voxelization submits at most 32,768 triangles per frame, also respecting **Voxelization Command Budget**. Large meshes resume from an index cursor. Raster footprint and overdraw still affect GPU cost; a triangle budget is not a millisecond guarantee.
- A rebuild adds one geometry replay and another resolve/mip generation. The first-pass field can be displayed while the secondary pass progresses. Lighting therefore takes longer to settle, especially with moving lights or large scenes. Lowering strength does not reduce this cost unless it is set to zero.
- The stationary cache samples the completed secondary field and pauses its probe updates while its source cascade is rebuilding.

This is one additional diffuse bounce within each cascade, not an arbitrary number of bounces or uniform ambient fill. Volume extent, voxel resolution, coarse mip filtering, normal bias, thin walls and existing local-light injection limits still affect the result. The secondary pass does not introduce local-light shadow maps or increase the number of injected lights. It cannot recover light outside the voxel coverage.

## Validation

The floating-point OpenGL voxel tests exercise both shader paths against a controlled incident field, checking strength, reflected energy, metallic rejection and dark-source rejection. Vulkan scene checks compare deposited radiance with the feature disabled/enabled, coloured diffuse and black/metallic receivers, persistence without feedback, cache-enabled rendering, source removal and disabling after use. Existing emissive coverage and world-cache regressions remain applicable.

These checks verify behavior. They do not establish a frame-time speedup or a measured cost for a production scene.

The scene-facing adapter regression also applies camera clip planes before checking the bounce controls. Previously, the scene renderer overwrote VCT's local-light gain and secondary strength with near/far distances, forcing secondary bounce to 1 after clamping regardless of the editor control. Camera decoration now writes 5.xy only for SSAO, whose shader expects those values there. This regression fails with the previous scene-camera mapping.


## Faint light and preview display

Radiance accumulation uses 16-bit source precision in the existing 32-bit integer volumes (a radiance step of 1/4095 instead of 16/4095). This preserves secondary contributions that previously rounded to zero. Coverage remains independently encoded; accumulation is bounded to prevent integer wrap on heavily overlapping geometry. No voxel texture memory is added. The finer precision also preserves faint first-bounce emission.

The RHI **Indirect Only** preview uses fixed exposure 1 and gamma 2.2 with the normal ACES display transform. Automatic exposure and the rest of the scene's post-processing remain excluded, so they cannot compensate away the comparison. Other raw voxel/debug buffers remain raw. The engine integration regression compares this preview with the normally tone-mapped scene when direct lighting is zero.

The secondary pass's triangle limit was increased from 8,192 to 32,768, while retaining the configured draw-command limit. It still only publishes after a cascade finishes. Large scenes can take hundreds of rendered frames to update; rapidly toggling the setting is not an instantaneous A/B comparison.

The optional read-only project capture is available as:

```powershell
PlutoGEEngineGraphicsApiTests.exe --vct-scene <project-root> <scene-file> <output-directory> <frames-per-setting> [eye-x eye-y eye-z target-x target-y target-z]
```

It renders the final indirect-only output through the actual scene adapter, with a fixed camera/exposure and no temporal filtering, capturing every 100 frames. Bistro was checked with 1,533 draw commands, 2,828,278 triangles, 100 emissive draws, 64 voxels, two cascades and an eight-command budget. At the tested camera, the first captured visible secondary change moved from frame 500 to frame 400; the completed preview showed more ceiling illumination. These are frame-count and image comparisons, not production frame-time benchmarks. Tests also check that light reaches a third surface, and that a 0.001 incident source survives accumulation in both voxelizers.
