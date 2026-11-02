# Cloud shadows and overcast light

Volumetric cloud volumes (`VolumetricCloudComponent`) shade the scene below them in two ways:

- **Cloud shadows.** Moving patches where the clouds block the directional light. Inside a sunlit gap the
  geometry shadows stay as sharp as on a clear day.
- **Overcast light.** As coverage closes the deck, the cloud base replaces the clear sky in ambient and sky
  reflections. Once the sun is fully blocked, the scene is lit only by soft, grey, nearly directionless light.

Neither needs any setup beyond a cloud volume. Both follow the cloud's settings every frame, so weather
changes, wind and time of day carry through to the ground immediately.

## Settings

These properties are on `VolumetricCloudComponent`. All of them can also be set at runtime from C++.

| Property | Default | Effect |
| --- | --- | --- |
| Coverage | 0.55 | Fraction of the deck that is cloud. Around 0.3 gives scattered clouds; 1 gives a closed overcast deck. |
| Density, Extinction | 0.42, 0.012 | How opaque the clouds are. Together with coverage they set how dark the shadows get and how much sky the deck replaces. |
| Wind Direction, Wind Speed | (1, 0, 0.25), 8 | Move the clouds and their shadows. |
| Cast Shadows | on | Whether the volume casts shadows through the shadow map. When off, the volume still dims light uniformly by its average density. |
| Shadow Distance | 3000 | Half the width of the shadow map, in world units, around the camera. Larger values cover more ground at lower resolution. With several volumes, the largest value is used. |
| Shadow Density | 1 | Scales the volume's extinction for shadows only. Use it to make shadows lighter or darker without changing how the clouds look. |

`SetCoverage`, `SetDensity`, `SetExtinction`, `SetCloudColor`, `SetAmbientLight`, `SetDetailErosion`,
`SetWindDirection`, `SetWindSpeed`, `SetCastShadows`, `SetShadowDistance` and `SetShadowDensity` take effect on
the next frame. A weather system can, for example, ramp coverage from 0.35 to 1 and density from 0.4 to 0.8 to
move from fair weather to overcast. The shadows fade out and the ambient light turns grey as it goes. These
setters are not yet exposed to C# scripts.

## How it works

**Shadow map.** Each frame, `CloudShadow.slang` traces sun transmittance through up to four nearby
shadow-casting volumes. It writes the result into a 256 × 256 region of the sky lighting atlas
(`SkyEnvironmentConfig.h`). Each texel lies on a horizontal plane at the lowest cloud base and marches 24 steps
toward the light. It uses the same density function as the cloud renderer (`VolumetricCloudDensity.slang`),
so shadows line up with the clouds drawn above them.

The map is centred where the camera's light ray crosses that plane, and snapped to whole texels so it does
not shimmer as the camera moves. A lit surface projects its position along the light onto the plane and takes
a bilinear sample (`CloudShadowSampling.slang`). Over the outer tenth of the map, the shadow fades to the
uniform estimate described below, so the map's edge never shows.

The map shares the sky atlas instead of using its own texture. The OpenGL surface shader's glass variant
already uses 32 fragment samplers, the common hardware limit.

**Uniform estimate.** `CloudOcclusion.slang` replaces each volume's noise with its mean density. That gives
the fraction of sky light the deck lets through, and the radiance its base scatters down. Surfaces blend the
clear sky toward that radiance by the blocked fraction. This applies to sky irradiance, flat ambient, sky
reflections and glass reflections. Fog, particles and glass already used this estimate, so they stay
consistent with surfaces.

**Low sun.** The map projects along the light with its elevation raised to at least 0.1, about 6°. Below
that, direct light is already fading at the horizon. When the light is below the horizon, no map is built.

## Limits and follow-ups

- Surfaces lit by baked probe grids, reflection captures or DDGI keep their own sky light. The cloud shadow on
  direct light still applies, but their ambient light does not turn overcast. DDGI probe rays and the ocean
  shader use clear-sky sunlight.
- Volumetric fog uses the uniform estimate, so light shafts through cloud gaps are not drawn.
- Receivers above the lowest cloud base, such as mountain tops inside the clouds, also take shadow from cloud
  below them.
- A thin, uniform deck dims the sun but does not widen geometry-shadow penumbrae.
