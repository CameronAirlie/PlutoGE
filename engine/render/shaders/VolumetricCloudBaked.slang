// Production cloud entry points: VolumetricCloud.slang sampling its noise from
// the baked lattice volume (CloudNoiseVolume) instead of hashing per sample.
#define PLUTO_CLOUD_NOISE_VOLUME 1
#include "VolumetricCloud.slang"
