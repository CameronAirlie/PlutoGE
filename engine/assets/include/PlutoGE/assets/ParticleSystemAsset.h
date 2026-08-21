#pragma once

#include <glm/glm.hpp>

#include <string>

namespace PlutoGE::assets
{
    enum class ParticleSimulationSpace
    {
        Local = 0,
        World = 1,
    };

    enum class ParticleShape
    {
        Point = 0,
        Sphere = 1,
        Box = 2,
        Cone = 3,
    };

    enum class ParticleRenderShape
    {
        Circle = 0,
        Quad = 1,
    };

    enum class ParticleCollisionMode
    {
        Kill = 0,
        Bounce = 1,
        Stop = 2,
    };

    struct ParticleSystemAsset
    {
        bool looping = true;
        bool playOnAwake = true;
        float duration = 5.0f;
        int maxParticles = 1000;
        float startLifetime = 5.0f;
        float lifetimeVariation = 0.0f;
        float startSpeed = 2.0f;
        float speedVariation = 0.0f;
        float startSize = 0.25f;
        float sizeVariation = 0.0f;
        glm::vec4 startColor{1.0f};
        bool colorOverLifetimeEnabled = false;
        glm::vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};
        bool sizeOverLifetimeEnabled = false;
        float endSize = 0.0f;
        float gravityModifier = 0.0f;
        float drag = 0.0f;
        float buoyancy = 0.0f;
        glm::vec3 windVelocity{0.0f};
        float turbulenceStrength = 0.0f;
        float turbulenceFrequency = 0.5f;
        float rotationSpeed = 0.0f;
        float rotationSpeedVariation = 0.0f;
        float fadeInFraction = 0.0f;
        float fadeOutFraction = 0.25f;
        float emissionRateOverTime = 10.0f;
        float burstTime = 0.0f;
        int burstCount = 0;
        ParticleSimulationSpace simulationSpace = ParticleSimulationSpace::Local;
        ParticleShape shape = ParticleShape::Point;
        glm::vec3 shapeSize{1.0f};
        float shapeRadius = 1.0f;
        float coneAngle = 25.0f;
        ParticleRenderShape renderShape = ParticleRenderShape::Circle;
        std::string materialAssetReference;

        bool collisionEnabled = false;
        ParticleCollisionMode collisionMode = ParticleCollisionMode::Kill;
        float collisionDampening = 0.0f;
        float collisionBounce = 0.5f;
        float collisionLifetimeLoss = 0.0f;
        float collisionRadius = 0.05f;
        int collisionMaxChecksPerFrame = 256;

        bool trailsEnabled = false;
        float trailLifetime = 0.5f;
        float trailWidth = 0.08f;
        bool trailInheritParticleColor = true;
        std::string trailMaterialAssetReference;

        std::string collisionSubEmitterAssetReference;
        int collisionSubEmitterCount = 0;
        std::string deathSubEmitterAssetReference;
        int deathSubEmitterCount = 0;
    };

    ParticleSystemAsset CreateDefaultParticleSystemAsset();
}
