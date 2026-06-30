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

    struct ParticleSystemAsset
    {
        bool looping = true;
        bool playOnAwake = true;
        float duration = 5.0f;
        int maxParticles = 1000;
        float startLifetime = 5.0f;
        float startSpeed = 2.0f;
        float startSize = 0.25f;
        glm::vec4 startColor{1.0f};
        float gravityModifier = 0.0f;
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
    };

    ParticleSystemAsset CreateDefaultParticleSystemAsset();
}
