#pragma once
// Shared C++/Slang constants of the foliage wind model. FoliageWind.cpp packs FoliageWindParameters and computes
// bounds from them; FoliageWind.slang deforms vertices with them. Change both sides together (docs/FOLIAGE_WIND.md).

// Wind speed (m/s) at which the normalized strength is 1, and the strength's ceiling. The ceiling keeps the
// displacement bounded, so culling can pad bounds without knowing the scene's wind.
#define PLUTO_WIND_REFERENCE_SPEED 10.0
#define PLUTO_WIND_MAX_STRENGTH 2.0

// Lean of a fully flexible plant (stiffness 0) at its bend height, as a fraction of that height, at strength 1.
#define PLUTO_WIND_MAX_BEND 0.35
// Heights above the pivot are measured in bend heights and clamped here: geometry far above the bend height moves
// no further, which bounds the displacement of meshes whose bend height was set too low.
#define PLUTO_WIND_MAX_RELATIVE_HEIGHT 1.5
// Share of the sway that follows the gusts (a steady lean) and that oscillates about it.
#define PLUTO_WIND_LEAN_SHARE 0.65
#define PLUTO_WIND_OSCILLATION_SHARE 0.35
// Sway frequency (Hz) of a 1 m tall plant at stiffness 0 and 1; taller plants sway slower (1 / sqrt(height)).
#define PLUTO_WIND_SWAY_FREQUENCY_SOFT 0.45
#define PLUTO_WIND_SWAY_FREQUENCY_STIFF 1.6

// Gusts: two waves travelling downwind (SceneWindGust). The amplitudes sum to 0.5 about a mean of 0.5, so the gust
// level spans [0, 1]; the second, shorter wave is skewed across the wind so fronts are not straight lines.
#define PLUTO_WIND_GUST_PRIMARY 0.32
#define PLUTO_WIND_GUST_SECONDARY 0.18
#define PLUTO_WIND_GUST_SECONDARY_SCALE 2.37
#define PLUTO_WIND_GUST_CROSS 0.61
#define PLUTO_WIND_GUST_SECONDARY_PHASE 1.7
// Plants take their phase from a hash of their root position on this grid (cells per metre).
#define PLUTO_WIND_SEED_CELLS_PER_METRE 4.0

// Flutter at flutter 1: amplitude as a fraction of the bend height at strength 1, and frequency (Hz).
#define PLUTO_WIND_FLUTTER_AMPLITUDE 0.04
#define PLUTO_WIND_FLUTTER_FREQUENCY 3.5
