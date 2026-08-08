#pragma once

#include <array>
#include <cstddef>

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    inline constexpr std::size_t kWorldVisibilityMaxCascades = 3;
    inline constexpr std::size_t kWorldVisibilityDirectionCount = 6;

    struct WorldVisibilityCascade
    {
        glm::vec3 origin{0.0f};
        float size = 0.0f;
        bool valid = false;
    };

    // Non-owning frame snapshot. Texture lifetime remains with the provider.
    // Consumers must acquire a fresh snapshot each frame and never cache it.
    struct WorldVisibilitySnapshot
    {
        std::array<unsigned int, kWorldVisibilityDirectionCount> directionalVolumeTextures{};
        std::array<WorldVisibilityCascade, kWorldVisibilityMaxCascades> cascades{};
        int cascadeCount = 0;
        int cascadeResolution = 0;

        bool IsValid() const
        {
            return cascadeCount > 0 && cascadeResolution > 0 && directionalVolumeTextures[0] != 0;
        }
    };

    class IWorldVisibilityProvider
    {
    public:
        virtual ~IWorldVisibilityProvider() = default;
        virtual WorldVisibilitySnapshot GetWorldVisibilitySnapshot() const = 0;
    };
}
