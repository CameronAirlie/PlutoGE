# Point and spot light attenuation

Point/spot intensity is interpreted as candela, with one world unit equal to one metre. Away from the source, incident illumination is intensity divided by distance squared, multiplied by color and the receiving surface cosine. Spotlights additionally apply their existing cone factor. Directional-light behavior is unchanged.

Range is no longer an authored LightComponent property. Its internal culling/shadow radius is sqrt(intensity * brightest color channel / 0.01), corresponding to a 0.01-lux cutoff. The last 10% fades smoothly to zero. This threshold is an engine approximation: physical lights have no finite range. Within 1 cm of the ideal point source, attenuation is capped to avoid division by zero.

Examples for white sources: 1 cd gives a 10 m cutoff, 4 cd gives 20 m, and 100 cd gives 100 m. Four times the intensity doubles the radius. These radii are performance bounds, not distances with uniform illumination. Direct lighting, shadows, viewport gizmos, VCT, LPV, particles, transparent materials, and CPU/GPU baking use the new model. Local Light Bounce remains an optional artistic GI multiplier.

Old scene Range properties are accepted and ignored, and disappear when saved. Numeric intensity values are retained; old lighting appearance cannot be preserved at every distance because the falloff law changed. Re-tune intensities and rebake lightmaps as needed. Low-level BasicPointLight packets still carry an explicit internal cutoff for renderer callers; the scene adapter always derives it. C++ callers of the removed LightComponent::SetRange should set intensity instead.

Reference: [Khronos KHR_lights_punctual](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_lights_punctual/README.md).
