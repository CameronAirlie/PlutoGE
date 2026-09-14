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

The spectrum uses eight directional modes split between swell and wind sea, with finite-depth dispersion `omega² = g k tanh(k depth)`. A bounded second harmonic sharpens crests and broadens troughs. Nonzero directional spread also introduces smooth spatial phase bending and bounded wave-group envelopes, breaking uninterrupted parallel ridges. The CPU and GPU include their analytical gradient terms. Zero spread intentionally retains planar wave trains. Foam uses broad irregular patches as well as fine breakup so distant coverage does not form continuous rows. This is a single-valued, Stokes-style procedural height field, not an FFT spectrum, horizontally displaced Gerstner mesh, or fluid solver. Crest foam uses positive dimensionless curvature as an artistic breaking-wave indicator; it is not simulated fluid compression.

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

## Stylized pirate sea

Select an ocean entity and click **Apply Stylized Sea Preset** in its Ocean inspector. This applies larger rolling swells, a swell-dominated spectrum, turquoise crests, deep blue troughs, warm-white foam, and stronger glints. The action supports scene undo and marks changed prefab properties. Masks, transforms, simulation time and visibility distance are preserved.

**Stylization** blends the art-directed crest/trough palette and elongated foam breakup from 0 (the existing shading) to 1. **Crest Color** controls the turquoise wave tops. Wave controls remain editable after applying the preset. Surface colors still receive scene illumination and directional shadows, so a physical sky and directional light are needed for a bright daytime sea; the preset does not modify scene lights. Deep-floor attenuation remains unchanged.

The visual direction is inspired by stylized pirate-adventure seas rather than recreating a game's assets or entire rendering system. Wakes, spray, and full fluid simulation remain outside this preset.

## Harbour water and buildings

Apply **Harbour Preset** in the Ocean inspector for flooded streets, quays and sheltered city water. The undoable preset implements the five city-water changes in order:

1. Low-amplitude, slower swells with subtle ripples; the separate Stylized Sea preset remains available for exposed ocean.
2. Restrained blue-green colors and low Stylization, so reflections and lights dominate the surface instead of bright turquoise bands.
3. Sparse crest whitecaps, a narrow shoreline wash, and localized wall foam.
4. Screen-space building reflections traced from the displaced water surface into the opaque scene depth and normal buffers. Misses fall back to physical-sky lighting.
5. Damped displacement and ripples immediately beside visible walls, a subtle dark contact band and broken contact foam.

Reflection Strength blends scene reflection hits into the sky reflection; Reflection Distance limits ray travel. Contact Distance controls the wall-neighborhood radius in world units; Contact Damping, Contact Darkening and Contact Foam independently control the three contact effects. Set Reflection Strength or Contact Distance to zero to disable the respective feature. The preset preserves area masks and visibility depth.

The screen-space path uses a fixed bounded ray budget, crossing refinement, thickness/backface checks, and fades at screen edges and maximum distance. It cannot reflect off-screen or hidden geometry, and it approximates rough reflection filtering by fading toward the sky. Contact samples reject horizontal floors and require actual world-space proximity, avoiding dark outlines from distant silhouettes. Hidden walls cannot contribute; this is visual contact treatment, not fluid collision. Camera-dependent near-wall damping is not included in the camera-independent CPU gameplay sampler. It does not create solid collision boundaries or prevent a mathematical wave from passing through geometry.

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

Physical-sky reflections and directional shadows remain supported. Off-screen scene reflections, local point/spot lights, water motion/depth output, and complete transparent/temporal integration remain future work. No mesh, asset migration, or additional post-process component is needed.

## Verification

`PlutoGEOceanWaveModelTests` checks analytical slopes and velocity against finite differences, height bounds, finite-depth dispersion, stationary/zero-amplitude waves, property round trips, mask preservation, invalid deltas and continuity across the old clock wrap. The OpenGL/Vulkan ocean tests exercise rendered whitecaps, shoreline foam, caustics, masks, lighting, both directional shadow paths and underwater fading. A test-only shader compiles the production wave evaluator and compares GPU heights and slopes with CPU samples at several phases and wind directions. GPU comparisons use 128x128 render targets; this is correctness validation, not a full-resolution performance benchmark.
