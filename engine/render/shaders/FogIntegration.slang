#ifndef PLUTO_FOG_INTEGRATION
#define PLUTO_FOG_INTEGRATION

// Scalar-only helpers are also embedded in the legacy GLSL path. Density is
// extinction per world unit at heightOffset; sunlight and ambient use the same
// integral so the analytical tail cannot introduce a second fog layer.
float heightFogOpticalDepth(float density, float falloff, float heightOffset,
                           float startHeight, float directionY, float distance)
{
    if (density <= 0.0 || distance <= 0.0) return 0.0;
    falloff = max(falloff, 0.0);
    float span = abs(falloff * directionY * distance);
    float average = span < 0.001 ? 1.0 - span * 0.5 + span * span / 6.0
                                : (1.0 - exp(-span)) / span;
    float denseHeight = min(startHeight, startHeight + directionY * distance);
    float logDepth = log(density) + log(distance) + log(average) -
                     falloff * (denseHeight - heightOffset);
    return exp(clamp(logDepth, -80.0, 80.0));
}

float infiniteHeightFogOpticalDepth(float density, float falloff, float heightOffset,
                                   float startHeight, float directionY)
{
    if (density <= 0.0) return 0.0;
    float slope = max(falloff, 0.0) * directionY;
    // A horizontal or descending ray through a nonzero exponential medium
    // has infinite optical depth. Upward rays have a finite closed-form limit.
    if (slope <= 0.0) return 80.0;
    return exp(clamp(log(density) - falloff * (startHeight - heightOffset) -
                     log(slope), -80.0, 80.0));
}

float fogShadowWeight(float distance, float detailDistance)
{
    // Fade to the distant lighting model over the final fifth of coverage.
    return 1.0 - smoothstep(detailDistance * 0.8, detailDistance, distance);
}
#endif
