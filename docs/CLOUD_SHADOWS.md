# Cloud shadows, overcast light and cloud rendering

Volumetric cloud volumes (`VolumetricCloudComponent`) affect the scene below them in three ways:

- **Cloud shadows.** Moving patches where the clouds block the directional light. Inside a sunlit gap the
  geometry shadows stay as sharp as on a clear day.
- **Light shafts.** In volumetric fog, sunlight through gaps between clouds forms shafts.
- **Overcast light.** As coverage closes the deck, the cloud base replaces the clear sky in ambient light and sky
  reflections. Once the sun is fully blocked, the scene is lit only by soft, grey, nearly directionless light.
  A closed deck leaves about a quarter of clear-sky light.

None of these need setup beyond a cloud volume. All of them follow the cloud's settings every frame, so weather
changes, wind and time of day carry through immediately.

## Settings

These properties are on `VolumetricCloudComponent`. They can be set from the inspector, from C++ (`SetCoverage`,
`SetDensity`, ...) and from C# scripts ([`VolumetricCloudComponent`](CSHARP_SCRIPTING.md#volumetriccloudcomponent)).

| Property | Default | Effect |
| --- | --- | --- |
| Coverage | 0.55 | Fraction of the deck that is cloud. Around 0.3 gives scattered clouds; 1 gives a closed overcast deck. |
| Density, Extinction | 0.42, 0.012 | How opaque the clouds are. Together with coverage they set how dark the shadows get and how much sky the deck replaces. |
| Wind Direction, Wind Speed | (1, 0, 0.25), 8 | Move the clouds and their shadows. |
| Render Scale | 0.5 | Resolution of the cloud trace. Temporal reconstruction keeps lower scales clean. |
| Primary Steps, Light Steps | 24, 3 | Samples along each view ray and toward the sun. With temporal reconstruction, 12–24 primary steps resolve as well as 128 without it. |
| Cast Shadows | on | Whether the volume casts shadows through the shadow map. When off, the volume still dims light uniformly by its average density. |
| Shadow Distance | 3000 | Half the width of the shadow map, in world units, around the camera. Larger values cover more ground at lower resolution. With several volumes, the largest value is used. |
| Shadow Density | 1 | Scales the volume's extinction for shadows only. Use it to make shadows lighter or darker without changing how the clouds look. |

A weather system can, for example, ramp coverage from 0.35 to 1 and density from 0.4 to 0.8 to move from fair
weather to overcast. As it does, the shadows fade out and the ambient light turns grey.

## Who uses cloud lighting

There is one model, `CloudOcclusionParameters` in `CloudOcclusion.slang` and `CloudOcclusion.h`, and the renderer
fills it once per frame. Every consumer reads the same state:

| Consumer | Direct light | Sky light |
| --- | --- | --- |
| Lit surfaces (BasicLit, terrain, glass) | Shadow map | Overcast blend of sky irradiance, flat ambient and sky reflections |
| Volumetric fog | Shadow map, so cloud gaps cast shafts | Overcast blend |
| Glass atmosphere, particles | Deck estimate | Overcast blend |
| Live-sky ambient probe grids | (baked) | Overcast blend of the live sky |
| DDGI rays that reach the sky | — | Overcast blend |
| DDGI and VCT sunlight | Deck estimate at the camera, quantized so drifting clouds do not relight voxel caches every frame | — |

Shaders opt into the shadow map by defining `PLUTO_CLOUD_SHADOW_ATLAS` as the sky lighting atlas sampler before
including `CloudOcclusion.slang`. Without it, `evaluateCloudLighting` falls back to the deck estimate. A new
consumer therefore needs no cloud code of its own.

## How it works

**Shadow map.** Each frame, `CloudShadow.slang` traces sun transmittance through up to four nearby
shadow-casting volumes. It writes the result into a 256 × 256 region of the sky lighting atlas
(`SkyEnvironmentConfig.h`). Each texel lies on a horizontal plane at the lowest cloud base and marches 24 steps
toward the light. It uses the same density function as the cloud renderer (`VolumetricCloudDensity.slang`),
so shadows line up with the clouds drawn above them.

The map is centred where the camera's light ray crosses that plane, and snapped to whole texels so it does
not shimmer as the camera moves (`CloudShadowMap.h`). A receiver projects its position along the light onto the
plane and takes a bilinear sample. Over the outer tenth of the map, the shadow fades to the deck estimate, so the
map's edge never shows. The map shares the sky atlas instead of using its own texture: the OpenGL surface shader's
glass variant already uses 32 fragment samplers, the common hardware limit.

**Deck estimate.** `CloudOcclusion.cpp` replaces each volume's noise with its mean density. That gives the
fraction of sky light the deck lets through, and the radiance its base scatters down. Receivers blend the clear sky
toward that radiance by the blocked fraction. `EvaluateCloudLighting` is its CPU mirror.

**Low sun.** The map projects along the light with its elevation raised to at least 0.1, about 6°. Below
that, direct light is already fading at the horizon. When the light is below the horizon, no map is built.

## Cloud rendering

A cloud trace marches a fixed number of steps across distances that can be kilometres long. Earlier versions
used the same step offsets in every pixel to avoid grain, because the trace had no history. Each step's samples
then formed a shell through the clouds, and those shells showed as thin slices.

**Temporal reconstruction** (`CloudTemporalFilter.h`, `CloudTemporal.slang`) fixes this at no extra trace cost:

1. Once a cloud volume has history, the trace offsets its steps by a per-pixel, per-frame phase
   (interleaved gradient noise advanced by the golden ratio). Light samples are offset the same way.
2. The filter reprojects the previous result to this frame. It uses a depth a quarter of the way into the
   volume segment the ray crosses, so camera rotation reprojects exactly and translation nearly so.
3. It clips the history to the current trace's neighbourhood (mean ± 1.25σ over 3 × 3), so moving clouds and
   disocclusions cannot ghost.
4. It blends in 10% of the new trace.

With history, a 12-step trace comes about 10× closer to a 128-step reference than a single frame does. The first
frame of a volume, a resize or a camera cut traces with the shared offsets, so one-off renders such as thumbnails
stay free of grain. Each volume keeps its own history at its trace resolution, which costs 16 bytes per
trace pixel.

**Lighting.** The trace approximates multiple scattering with three octaves of single scattering: extinction
falls by ¼, contribution by ½ and anisotropy by ½ per octave (Wrenninge, *Oz: The Great and Volumetric*). Dense
interiors therefore glow softly instead of turning black a few metres behind their lit edge. A two-lobe phase
function, 80% forward and 20% back, adds the silver lining toward the sun without flattening the far side. Sky
light falls to 55% at a cloud's base, so clouds keep their shape when seen from below. The CPU deck estimate
follows the same lighting, so overcast radiance matches the clouds drawn.

## Limits and follow-ups

- Surfaces lit by baked probe grids or reflection captures keep their baked sky light. Only live-sky grids follow
  the clouds. The cloud shadow on direct light still applies everywhere.
- DDGI hardware rays and VCT take one cloud factor for the whole scene, not per hit.
- Receivers above the lowest cloud base, such as mountain tops inside the clouds, also take shadow from cloud
  below them.
- A thin, uniform deck dims the sun but does not widen geometry-shadow penumbrae.
- Temporal reconstruction reprojects at one depth per pixel. Fast camera translation past nearby cloud can
  briefly soften its edges until the history clips.
