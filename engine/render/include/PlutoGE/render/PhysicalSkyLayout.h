#pragma once
// Shared C++/Slang layout of the physical-sky parameter lanes. The sky
// post-process and every lit surface read the same lanes, so the background
// and its lighting cannot drift apart. PhysicalSkyModel.cpp packs them and
// PhysicalSkyModel.slang decodes them.
#include "PostProcessLayout.h"

#define PLUTO_SKY_LANE_SUN 0          // xyz toward the sun, w Rayleigh strength
#define PLUTO_SKY_LANE_SUN_COLOR 1    // rgb sun color, w Mie strength
#define PLUTO_SKY_LANE_MOON_COLOR 2   // rgb moon color, w Mie anisotropy
#define PLUTO_SKY_LANE_GROUND 3       // rgb ground color, w ozone strength
#define PLUTO_SKY_LANE_SUN_DISC 4     // sun intensity, sun radius (degrees), airglow, star intensity
#define PLUTO_SKY_LANE_MOON_DISC 5    // moon intensity, moon radius (degrees), earthshine, star twinkle
#define PLUTO_SKY_LANE_MOON 6         // xyz toward the moon (zero: opposite the sun), w moonlit sky radiance
#define PLUTO_SKY_LANE_NIGHT 7        // rgb light pollution, w Milky Way intensity
// The sky travels as a post-process packet and is copied whole into the
// frame's lighting block, so both use the post-process lane count.
#define PLUTO_SKY_LANE_COUNT PLUTO_POST_PROCESS_LANES

// Twilight transitions, as the sine of the sun's elevation. smoothstep(begin, end).
#define PLUTO_SKY_DAY_BEGIN -0.09
#define PLUTO_SKY_DAY_END 0.035
#define PLUTO_SKY_NIGHT_BEGIN -0.31   // night is 1 - smoothstep(begin, end)
#define PLUTO_SKY_NIGHT_END -0.04
// Direct light fades across the horizon so it never shines through terrain.
#define PLUTO_SKY_SUNLIGHT_BEGIN -0.02
#define PLUTO_SKY_SUNLIGHT_END 0.03
#define PLUTO_SKY_MOONLIGHT_BEGIN -0.04
#define PLUTO_SKY_MOONLIGHT_END 0.04

#if PLUTO_SKY_LANE_NIGHT >= PLUTO_SKY_LANE_COUNT
#error Physical sky lanes must fit in the post-process parameter block
#endif
