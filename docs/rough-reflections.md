# Rough screen-space reflections

SSR integrates a single-scattering isotropic GGX microfacet reflection lobe with
16 deterministic stratified directions per shaded pixel (per half-resolution
pixel in the legacy renderer). Perceptual roughness maps to alpha = roughness^2.
Each sample is weighted by BRDF * N.L / PDF, including Smith masking and Schlick
Fresnel. Metal F0 comes from albedo; dielectric F0 is 0.04. Roughness changes the
angular distribution, with no roughness cutoff or opacity multiplier.

The RHI shader reconstructs world positions from reverse-Z depth and projects
world-space rays using the actual camera matrices. Thickness and binary
refinement reject false depth crossings. Reflected radiance adds to existing
lighting. Edge fading remains a screen-coverage heuristic.

Fresnel Power and Metallic Boost are accepted when reading older presets for
compatibility, but are no longer exposed or applied: Fresnel uses exponent 5 and
metallic comes from the material. Intensity remains an artistic multiplier;
use 1 for the unscaled BRDF estimate.

This is still screen-space, finite-sample, single-bounce reflection, not full
light transport. Off-screen and occluded geometry cannot contribute, thin
geometry may be missed, and 16 directions can show sampling structure. The
additional rays cost more than the previous single-ray effect. The legacy
renderer may also have environment specular already in its scene colour; a
future dedicated indirect-specular buffer is needed to replace overlapping
probe contributions instead of adding them.

Reference: [PBRT microfacet reflection and sampling](https://pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Sampling_Reflection_Functions).

GPU regressions in `tests/SsrRenderingChecks.h` check nonzero fully rough and
dielectric reflection, broadening beyond the smooth footprint, metallic tint,
and preservation of existing lighting on OpenGL and Vulkan.
