#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <glm/vec3.hpp>

namespace PlutoGE::ui
{
    struct ViewportBookmark
    {
        std::string name;
        glm::vec3 position{0, 2, 6};
        float yaw = 0;
        float pitch = 0;
        float fov = 45;
        float nearPlane = 0.1f;
        float farPlane = 100;
        float moveSpeed = 6;
        bool orthographic = false;
        float orthographicSize = 10;
    };

    // Editor-only sidecar data, independent from scene and runtime serialization.
    class ViewportBookmarks
    {
    public:
        static constexpr std::size_t MaxBookmarks = 128;
        static bool Validate(const std::vector<ViewportBookmark> &bookmarks, std::string &error);
        // A missing file is an empty collection. Failure leaves output untouched.
        static bool Load(const std::filesystem::path &path, std::vector<ViewportBookmark> &output, std::string &error);
        // Flush a temporary file and atomically replace the previous collection.
        static bool Save(const std::filesystem::path &path, const std::vector<ViewportBookmark> &bookmarks, std::string &error);
    };
}
