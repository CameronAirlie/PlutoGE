#pragma once
// Shared C++/Slang layout of the dynamic diffuse GI probe volume (DdgiVolume, DDGICommon.slang).
//
// Every probe owns one square tile in each atlas: an octahedral interior surrounded by a one-texel border that
// mirrors the opposite interior edge, so hardware bilinear filtering never blends neighbouring probes.

// Irradiance: 6 x 6 interior texels (cosine-weighted mean radiance, rgb).
#define PLUTO_DDGI_IRRADIANCE_INTERIOR 6
#define PLUTO_DDGI_IRRADIANCE_TILE 8
// Visibility: 14 x 14 interior texels (mean and mean square of the hit distance, rg; share of rays that reached the
// sky, b).
#define PLUTO_DDGI_DISTANCE_INTERIOR 14
#define PLUTO_DDGI_DISTANCE_TILE 16

// Nested probe cascades (DdgiProbeGrid.h), each this many times the spacing of the one inside it.
#define PLUTO_DDGI_MAX_CASCADES 4

// Rays traced in fixed (unrotated) directions, used only for probe relocation and classification. Inactive probes
// trace just these; the blended rays follow them in the ray buffer.
#define PLUTO_DDGI_FIXED_RAYS 16
#define PLUTO_DDGI_MAX_BLENDED_RAYS 256
#define PLUTO_DDGI_MAX_RAYS (PLUTO_DDGI_FIXED_RAYS + PLUTO_DDGI_MAX_BLENDED_RAYS)
// Hit distance a ray source reports for a ray that left the scene and saw the sky (IDdgiRaySource).
#define PLUTO_DDGI_MISS_DISTANCE 10000.0

// Probe state texel: xyz relocation offset in units of probe spacing, w flags.
#define PLUTO_DDGI_PROBE_INACTIVE 1
// History is invalid (new, or entered the volume by scrolling): the next blend replaces it.
#define PLUTO_DDGI_PROBE_FRESH 2

// Compute group sizes.
#define PLUTO_DDGI_TRACE_GROUP 64
#define PLUTO_DDGI_STATE_GROUP 64

// Apply and debug views (DDGIEffect "Debug View").
#define PLUTO_DDGI_VIEW_NONE 0
#define PLUTO_DDGI_VIEW_IRRADIANCE 1
#define PLUTO_DDGI_VIEW_INDIRECT 2
#define PLUTO_DDGI_VIEW_PROBES 3
// Probe sky visibility along each pixel's reflection, and the environment reflection after occlusion.
#define PLUTO_DDGI_VIEW_SKY_VISIBILITY 4
#define PLUTO_DDGI_VIEW_REFLECTIONS 5
