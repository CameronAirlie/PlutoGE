#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace PlutoGE::scene
{
    class Scene;

    class SceneSerializer
    {
    public:
        using LoadTraceCallback = std::function<void(std::string_view)>;

        static bool Save(const Scene &scene, const std::string &filePath, std::string *errorMessage = nullptr);
        static std::unique_ptr<Scene> Load(const std::string &filePath, std::string *errorMessage = nullptr);
        static std::unique_ptr<Scene> Load(const std::string &filePath,
                                           std::string *errorMessage,
                                           const LoadTraceCallback &trace);
        static bool SaveToString(const Scene &scene, std::string &outputText, std::string *errorMessage = nullptr);
        static std::unique_ptr<Scene> LoadFromString(const std::string &text, std::string *errorMessage = nullptr);
        static std::unique_ptr<Scene> LoadFromString(const std::string &text,
                                                     std::string *errorMessage,
                                                     const LoadTraceCallback &trace);
    };
}
