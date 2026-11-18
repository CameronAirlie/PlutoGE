#pragma once
// Shared C++/Slang layout of the light visibility mask (src/lighting/LightVisibilityMask.h, LightVisibility*.slang,
// BasicLit.slang lightVisibility).
//
// The mask holds, per pixel, the visibility of up to PLUTO_LIGHT_VISIBILITY_SLOTS lights: each slot is a
// PLUTO_LIGHT_VISIBILITY_BITS-bit level packed with three others into one 32-bit float channel as an exact integer
// (below 2^24, so no rounding), in rgb; alpha holds the device depth the slots were traced for.

// Lights the mask can hold in one frame.
#define PLUTO_LIGHT_VISIBILITY_SLOTS 12
#define PLUTO_LIGHT_VISIBILITY_SLOTS_PER_CHANNEL 4
#define PLUTO_LIGHT_VISIBILITY_BITS 6
#define PLUTO_LIGHT_VISIBILITY_LEVELS 63
// Light ids: local lights use their packed index (0 to PLUTO_MAX_LOCAL_LIGHTS - 1), the directional light this one.
#define PLUTO_LIGHT_VISIBILITY_DIRECTIONAL 32
#define PLUTO_LIGHT_VISIBILITY_IDS 33
// Rows of four light ids in the frame's id-to-slot table (SceneLightingParameters.slang lightVisibilitySlots).
#define PLUTO_LIGHT_VISIBILITY_ID_ROWS 9
// Compute group edge, in pixels.
#define PLUTO_LIGHT_VISIBILITY_GROUP 8
