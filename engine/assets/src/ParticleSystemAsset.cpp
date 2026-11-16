#include "PlutoGE/assets/ParticleSystemAsset.h"

namespace PlutoGE::assets
{
    ParticleSystemAsset CreateDefaultParticleSystemAsset()
    {
        return {};
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
