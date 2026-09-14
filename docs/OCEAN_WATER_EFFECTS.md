# RHI ocean waves and water effects

## Requirements and implementation plan

1. Replace the fixed five-wave surface with directional swell and wind waves. Preserve existing serialized wave controls; add wind direction, directional spread, wind-sea fraction, and dispersion depth.
2. Separate wave evaluation from rendering. Build one deterministic eight-mode spectrum on the CPU and upload its coefficients and wrapped phases. Provide local height, analytical slope/normal, vertical velocity, and a crest metric for C++ consumers.
3. Keep animation stable across long sessions. Accumulate time in double precision, wrap each mode's phase independently, reject invalid frame deltas, and avoid a discontinuous global clock reset.
4. Improve intersection and shading. Bound the surface with its amplitude envelope, use safeguarded Newton intersection, derive normals from the displaced height field, and retain existing RHI lighting, shadow, mask, and refraction behavior.
5. Add water detail: curvature-driven crest whitecaps, wind-advected multiscale foam, animated shoreline wash, adjustable close-range ripples, and shallow-water caustics. Light foam and caustics with the existing sun and shadow resources.
6. Verify analytical derivatives, displacement bounds, finite-depth dispersion, stopped waves, serialization/masks, long-running animation, CPU/GPU parity, and rendered effects on OpenGL and Vulkan. Build editor and runtime.

## Architecture

`scene/OceanWaveModel.h` is a pure, deterministic model with no graphics or entity dependencies. `OceanComponent` owns authoring settings and simulation time. `RhiOcean` translates the component into an immutable `OceanParameters` packet. `OceanWaves.slang` evaluates that packet; `Ocean.slang` handles visibility, intersections, refraction and composition; `OceanLighting.slang` owns scene-light and shadow evaluation. The inspector uses the component's serialized properties rather than maintaining a separate property list.

The spectrum uses eight directional modes split between swell and wind sea, with finite-depth dispersion `omega² = g k tanh(k depth)`. A bounded second harmonic sharpens crests and broadens troughs. This is a single-valued, Stokes-style procedural height field, not an FFT spectrum, horizontally displaced Gerstner mesh, or fluid solver. Crest foam uses positive dimensionless curvature as an artistic breaking-wave indicator; it is not simulated fluid compression.

## Authoring

Existing Wave Amplitude, Wave Length, Wave Speed and Wave Choppiness remain available. Choppiness now changes crest shape; normals follow that shape rather than receiving a separate slope multiplier. Old scenes load without migration, but their wave appearance changes.

| Control | Meaning |
| --- | --- |
| Wind Direction | Degrees from local +X toward local +Z |
| Directional Spread | 0 aligns wave directions; 1 gives a broader crossing sea |
| Wind Sea | Amplitude fraction assigned to the four shorter modes |
| Water Depth | Uniform dispersion depth in local units; does not move the floor or query terrain |
| Crest Foam Threshold / Intensity | Curvature threshold and amount of whitecaps |
| Foam Scale | Spatial frequency of the moving foam breakup |
| Foam Distance / Intensity | Shoreline wash extent and amount |
| Ripple Strength | Small close-range normal ripples; fades with distance |
| Caustics Intensity / Scale | Strength and frequency of shallow-water focusing patterns |

For calm water, reduce amplitude, wind sea and ripple strength. For a rougher sea, increase amplitude and wind sea, then adjust crest-foam threshold. Set Wave Speed to zero to stop wave and surface-detail animation. Zero amplitude gives a flat displacement surface. Settings and masks survive inspector edits and scene serialization.

## Visibility through and within water

Max Visibility Depth is the maximum visible distance travelled through water, in world units, for both viewpoints. Above the surface the air segment is excluded; below it the path starts at the camera. The same attenuation and fade curve applies in both cases. The existing Underwater Fade Start, Fade Softness, Turbidity and Depth Falloff controls now affect both views; Light Falloff still controls underwater illumination.

Surface Opacity controls the contribution of surface reflections/highlights and foam relative to the already attenuated water color. It cannot expose an unattenuated deep floor. Even zero opacity or turbidity retains the visibility cutoff. Shallow floors remain visible; floors beyond Max Visibility Depth contribute no transmitted color.

## Sampling from C++

```cpp
const auto sample = ocean.SampleLocalSurface(localXZ);
// sample.height, sample.gradient, sample.normal, sample.verticalVelocity
```

Coordinates and returned heights/velocities are in the ocean entity's local space. Transform query positions and normals appropriately for a rotated or scaled ocean. The sampler describes the mathematical surface independently of component activation and exclusion masks; callers must check those separately. For many queries, obtain `GetWaveSpectrum()` once per frame and call `SampleOceanSurface(spectrum, position)` repeatedly. Sampling does not apply forces or supply buoyancy automatically. Subpixel normal ripples are a rendering detail and are excluded from gameplay sampling.

## Limits

These effects target the RHI OpenGL and Vulkan paths; the legacy OceanPass retains its previous wave model. Foam is procedural and has no persistent history, wakes, spray or collision interaction. Shoreline wash and caustics use visible opaque scene depth, so hidden or transparent receivers cannot contribute. Caustics are an artistic focusing approximation, not refracted light transport, and currently appear when viewing the bottom through the surface. Water depth for dispersion is uniform; there is no bathymetry-driven shoaling or coastal refraction. Surface intersections remain an approximation at very grazing angles and extreme steepness.

Physical-sky reflections and directional shadows remain supported. Scene-geometry reflections, local point/spot lights, water motion/depth output, and complete transparent/temporal integration remain future work. No mesh, asset migration, or additional post-process component is needed.

## Verification

`PlutoGEOceanWaveModelTests` checks analytical slopes and velocity against finite differences, height bounds, finite-depth dispersion, stationary/zero-amplitude waves, property round trips, mask preservation, invalid deltas and continuity across the old clock wrap. The OpenGL/Vulkan ocean tests exercise rendered whitecaps, shoreline foam, caustics, masks, lighting, both directional shadow paths and underwater fading. A test-only shader compiles the production wave evaluator and compares GPU heights and slopes with CPU samples at several phases and wind directions. GPU comparisons use 128x128 render targets; this is correctness validation, not a full-resolution performance benchmark.
