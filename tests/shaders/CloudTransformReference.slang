// Compile-time reference retains the original per-sample world/local transforms
// and slab intersections, and evaluates every noise octave. No reference branch
// exists in production pipelines.
#define PLUTO_CLOUD_REFERENCE_TRANSFORMS 1
#include "../../engine/render/shaders/VolumetricCloud.slang"
