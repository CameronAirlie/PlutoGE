// Compile-time reference retains the original per-sample world/local transforms
// and slab intersections, and evaluates every noise octave. No reference branch
// exists in production pipelines.
#define PLUTO_CLOUD_REFERENCE_TRANSFORMS 1
// Share production's baked noise so only the transforms differ.
#define PLUTO_CLOUD_NOISE_VOLUME 1
#include "../../engine/render/shaders/VolumetricCloud.slang"
