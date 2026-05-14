#pragma once

#include <memory>
#include <string>

namespace PlutoGE::scene
{
    class Scene;

    class SceneSerializer
    {
    public:
        static bool Save(const Scene &scene, const std::string &filePath, std::string *errorMessage = nullptr);
        static std::unique_ptr<Scene> Load(const std::string &filePath, std::string *errorMessage = nullptr);
    };
}