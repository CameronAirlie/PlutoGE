#pragma once

#include <glm/glm.hpp>
#include <string>

namespace PlutoGE::scene
{
    struct FoliageTypeAsset
    {
        std::string assetReference;
        float cellSize = 32.0f;
        // Zero inherits the owning foliage component's draw distance.
        float maxDrawDistance = 0.0f;
        bool collisionEnabled = false;
        glm::vec3 collisionCenter{0.0f, 1.0f, 0.0f};
        float collisionRadius = 0.35f;
        float collisionHeight = 2.0f;
    };

    bool SaveFoliageTypeAsset(const std::string &path, const FoliageTypeAsset &asset, std::string *errorMessage = nullptr);
    bool LoadFoliageTypeAsset(const std::string &path, FoliageTypeAsset &asset, std::string *errorMessage = nullptr);
}
