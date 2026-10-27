#define PLUTO_SSR_STAGE 1
#define PLUTO_SSR_UNROLL_RAYS
// Trace for fewer than sixteen rays per pixel (SSR "Rays per Pixel").
#define PLUTO_SSR_REDUCED_RAYS
#include "SSR.slang"
