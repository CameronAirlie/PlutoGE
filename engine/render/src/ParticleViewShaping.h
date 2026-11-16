#pragma once

#include "PlutoGE/render/SceneWeather.h"

#include <algorithm>
#include <glm/glm.hpp>

// How one view draws a simulated particle: where (a camera volume wraps it around the camera), how wide (a minimum
// screen size) and how opaque (fades near the camera, toward the volume's faces, and in wet weather). Pure functions,
// so they are tested without a renderer (PlutoGEParticleViewShapingTests). docs/AIRBORNE_PARTICLES.md.
namespace PlutoGE::render
{
    // The copy of `position` in the box of `size` centred on the camera: space tiled with the box, so a field of
    // particles spawned through one box surrounds every camera and stays put while the camera moves.
    inline glm::vec3 WrapIntoCameraVolume(const glm::vec3 &position, const glm::vec3 &camera, const glm::vec3 &size)
    {
        const glm::vec3 offset = position - camera;
        return camera + offset - size * glm::floor(offset / size + 0.5f);
    }

    // Opacity toward the faces of a camera volume, for an offset from its centre: 1 within the inner part of each
    // axis, easing to 0 at the face over `edgeFade` of the half-extent, so wrapping across a face is never seen.
    inline float CameraVolumeEdgeFade(const glm::vec3 &offset, const glm::vec3 &size, float edgeFade)
    {
        if (!(edgeFade > 0.0f))
            return 1.0f;
        const glm::vec3 reach = glm::abs(offset) / glm::max(size * 0.5f, glm::vec3(1e-4f));
        const float start = 1.0f - std::min(edgeFade, 1.0f);
        float fade = 1.0f;
        for (int axis = 0; axis < 3; ++axis)
            fade *= 1.0f - glm::smoothstep(start, 1.0f, reach[axis]);
        return fade;
    }

    // Opacity of a particle `distance` from the camera: 0 within half of `nearDistance`, 1 beyond it.
    inline float CameraNearFade(float distance, float nearDistance)
    {
        if (!(nearDistance > 0.0f))
            return 1.0f;
        return glm::smoothstep(nearDistance * 0.5f, nearDistance, distance);
    }

    // Pixels one metre spans at `viewDepth` in front of a camera: perspective projections shrink with depth,
    // orthographic ones do not. `viewportHeight` in pixels.
    inline float ParticlePixelsPerMetre(const glm::mat4 &projection, float viewportHeight, float viewDepth)
    {
        const float scale = std::abs(projection[1][1]) * viewportHeight * 0.5f;
        const bool perspective = projection[2][3] != 0.0f;
        return perspective ? scale / std::max(viewDepth, 1e-4f) : scale;
    }

    struct ParticleScreenFit
    {
        float size = 0.0f;    // drawn width, metres
        float opacity = 1.0f; // multiplies the particle's alpha
    };

    // A particle narrower on screen than `minPixels` drawn that wide instead, its opacity lowered by the area it
    // gained, so it keeps the coverage (and brightness) it had. Sub-pixel billboards otherwise flicker as they fall
    // between pixel centres and samples.
    inline ParticleScreenFit FitParticleToScreen(float size, float pixelsPerMetre, float minPixels)
    {
        const float pixels = size * pixelsPerMetre;
        if (!(minPixels > 0.0f) || !(pixels > 0.0f) || pixels >= minPixels)
            return {size, 1.0f};
        const float scale = minPixels / pixels;
        return {size * scale, 1.0f / (scale * scale)};
    }

    // Opacity left by the scene's weather for a system with this wet-weather fade: rain scavenges airborne dust and
    // damp ground raises none, so the larger of precipitation and wetness thins it.
    inline float ParticleWeatherVisibility(const SceneWeatherSettings &weather, float wetWeatherFade)
    {
        const float wet = std::clamp(std::max(weather.wetness, weather.precipitation), 0.0f, 1.0f);
        return 1.0f - std::clamp(wetWeatherFade, 0.0f, 1.0f) * wet;
    }
}
