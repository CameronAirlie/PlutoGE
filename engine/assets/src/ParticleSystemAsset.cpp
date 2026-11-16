#include "PlutoGE/assets/ParticleSystemAsset.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::assets
{
    namespace
    {
        float Clamped(float value, float low, float high, float fallback)
        {
            return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
        }
    }

    ParticleSystemAsset CreateDefaultParticleSystemAsset()
    {
        return {};
    }

    ParticleScatteringSettings Sanitized(ParticleScatteringSettings settings)
    {
        const ParticleScatteringSettings defaults;
        settings.anisotropy = Clamped(settings.anisotropy, -0.9f, 0.9f, defaults.anisotropy);
        settings.sunStrength = Clamped(settings.sunStrength, 0.0f, 100.0f, defaults.sunStrength);
        settings.ambientStrength = Clamped(settings.ambientStrength, 0.0f, 100.0f, defaults.ambientStrength);
        return settings;
    }

    ParticleCameraVolumeSettings Sanitized(ParticleCameraVolumeSettings settings)
    {
        const ParticleCameraVolumeSettings defaults;
        // A box thinner than 10 cm would wrap particles every frame.
        for (int axis = 0; axis < 3; ++axis)
            settings.size[axis] = Clamped(settings.size[axis], 0.1f, 10000.0f, defaults.size[axis]);
        settings.edgeFade = Clamped(settings.edgeFade, 0.0f, 1.0f, defaults.edgeFade);
        return settings;
    }

    ParticleViewFadeSettings Sanitized(ParticleViewFadeSettings settings)
    {
        settings.nearDistance = Clamped(settings.nearDistance, 0.0f, 1000.0f, 0.0f);
        settings.minScreenSize = Clamped(settings.minScreenSize, 0.0f, 16.0f, 0.0f);
        return settings;
    }

    float SanitizedSceneWindScale(float scale)
    {
        return Clamped(scale, 0.0f, 4.0f, 1.0f);
    }

    float SanitizedWetWeatherFade(float fade)
    {
        return Clamped(fade, 0.0f, 1.0f, 0.0f);
    }

    ParticleSystemAsset CreateWaterSplashParticleSystemAsset()
    {
        ParticleSystemAsset asset;
        // Only bursts on request (EmitAt); the water system keeps one emitter alive and asks it to splash.
        asset.looping = true;
        asset.playOnAwake = false;
        asset.duration = 1.0f;
        asset.maxParticles = 600;
        asset.emissionRateOverTime = 0.0f;
        asset.burstCount = 0;
        asset.simulationSpace = ParticleSimulationSpace::World;
        // A crown of droplets: up and outward in a wide cone from a small ring.
        asset.shape = ParticleShape::Cone;
        asset.coneAngle = 35.0f;
        asset.shapeRadius = 0.15f;
        asset.startSpeed = 4.0f;
        asset.speedVariation = 0.55f;
        asset.startLifetime = 0.9f;
        asset.lifetimeVariation = 0.35f;
        asset.gravityModifier = 1.0f;
        asset.drag = 0.6f;
        asset.startSize = 0.07f;
        asset.sizeVariation = 0.6f;
        asset.sizeOverLifetimeEnabled = true;
        asset.endSize = 0.03f;
        asset.startColor = {0.92f, 0.96f, 1.0f, 0.9f};
        asset.colorOverLifetimeEnabled = true;
        asset.endColor = {0.85f, 0.92f, 1.0f, 0.0f};
        asset.fadeOutFraction = 0.35f;
        // Streaks read as flying water rather than floating dots.
        asset.velocityStretch = 0.035f;
        asset.renderShape = ParticleRenderShape::Circle;
        asset.renderMode = ParticleRenderMode::Billboard;
        asset.softParticlesEnabled = true;
        asset.softParticleDistance = 0.2f;
        return asset;
    }

    std::optional<ParticleSystemAsset> FindBuiltinParticleSystemAsset(std::string_view reference)
    {
        if (reference == kBuiltinWaterSplashParticlesReference)
            return CreateWaterSplashParticleSystemAsset();
        return std::nullopt;
    }
}
