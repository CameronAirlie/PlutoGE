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

// Direct light reaching a point inside the height fog. The medium above the
// point attenuates the light first, so a dense layer darkens toward its base
// instead of glowing uniformly. Light from below the horizon is extinguished.
float heightFogSunTransmittance(float density, float falloff, float heightOffset,
                                float height, float lightDirectionY)
{
    return exp(-infiniteHeightFogOpticalDepth(density, falloff, heightOffset, height, lightDirectionY));
}

// Distance along a ray at which its accumulated optical depth reaches
// opticalDepth. Callers keep opticalDepth below the ray's total depth.
float heightFogDistanceAtOpticalDepth(float density, float falloff, float heightOffset,
                                      float startHeight, float directionY, float opticalDepth)
{
    if (density <= 0.0 || opticalDepth <= 0.0) return 0.0;
    falloff = max(falloff, 0.0);
    float slope = falloff * directionY;
    float logStartDensity = clamp(log(density) - falloff * (startHeight - heightOffset), -80.0, 80.0);
    // Distance in a homogeneous medium with the start density.
    float homogeneous = opticalDepth * exp(-logStartDensity);
    if (abs(slope * homogeneous) < 0.001) return homogeneous;
    // depth(s) = startDensity * (1 - exp(-slope * s)) / slope, inverted for s.
    return -log(max(1.0 - slope * homogeneous, 0.000001)) / slope;
}

// Analytic segments are lit at one representative point: the distance before
// which half of the segment's single scattering, 1 - T, is accumulated.
float heightFogScatteringMidpoint(float density, float falloff, float heightOffset,
                                  float startHeight, float directionY, float segmentDepth, float segmentLength)
{
    float halfDepth = -log(0.5 + 0.5 * exp(-segmentDepth));
    return min(heightFogDistanceAtOpticalDepth(density, falloff, heightOffset, startHeight, directionY, halfDepth),
               segmentLength);
}

float fogShadowWeight(float distance, float detailDistance)
{
    // Fade to the distant lighting model over the final fifth of coverage.
    return 1.0 - smoothstep(detailDistance * 0.8, detailDistance, distance);
}
// Artistic aerial perspective for a flat world. Unlike the local height layer,
// this broad angular band joins the sky to aerial perspective below the horizon.
// Smooth distance onset protects nearby silhouettes; width is in degrees.
float horizonHazeTransmittance(float directionY, float distance, bool sky,
                               float strength, float widthDegrees, float onsetDistance)
{
    if (strength <= 0.0) return 1.0;
    float width = sin(clamp(widthDegrees, 0.1, 45.0) * 0.01745329252);
    float elevation = max(directionY, 0.0) / width;
    float angularDensity = exp(-0.5 * elevation * elevation);
    float distanceWeight = sky ? 1.0 : smoothstep(0.0, max(onsetDistance, 1.0), distance);
    return exp(-8.0 * strength * angularDensity * distanceWeight);
}
#endif
