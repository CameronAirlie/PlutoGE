#pragma once
// Shared C++/Slang layout of hardware ray-traced scenes (RayTracingScene, RayQueryDdgiRaySource,
// DDGIRayQueryTrace.slang).

// Local lights a ray-traced hit is lit by (unshadowed), besides the directional light (shadowed by a ray).
#define PLUTO_RT_MAX_LOCAL_LIGHTS 16
// How far probe rays look for surfaces; rays that reach it see the sky.
#define PLUTO_RT_MAX_DISTANCE 2000.0
