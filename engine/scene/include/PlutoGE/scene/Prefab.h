#pragma once

#include "PlutoGE/scene/Entity.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace PlutoGE::scene
{
    class Scene;

    class Prefab
    {
    public:
        static constexpr std::string_view kFileExtension = ".plutoprefab";

        static bool SaveFromEntity(const Entity &entity,
                                   const std::filesystem::path &filePath,
                                   std::string *errorMessage = nullptr);
        static Entity *Instantiate(Scene &scene,
                                   std::string prefabReference,
                                   Entity *parent = nullptr,
                                   std::string *errorMessage = nullptr);
        static bool UpdateInstance(Entity &instanceRoot, std::string *errorMessage = nullptr);
        static int UpdateInstances(Scene &scene,
                                   std::string_view prefabReference = {},
                                   std::string *errorMessage = nullptr);
        static bool ApplyInstanceToPrefab(const Entity &instanceRoot, std::string *errorMessage = nullptr);
    };
}
