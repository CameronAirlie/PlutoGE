#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::scene
{
    class Scene;

    // Colours are straight (non-premultiplied) RGBA.
    struct MapBakeStyle
    {
        glm::u8vec4 floor{27, 28, 32, 140};
        glm::u8vec4 obstacle{42, 48, 54, 235};
        glm::u8vec4 outline{166, 160, 180, 242};
        float outlineWidth = 2.0f; // Output pixels, drawn outside obstacle edges.
    };

    struct MapBakeSettings
    {
        int size = 1024;          // Square output edge in pixels (64..4096).
        int supersample = 2;      // Drawn larger, then box-filtered down (1..4).
        float padding = 4.0f;     // World metres around the playable area.
        // Slabs at most this thick, or flat relative to their footprint, are walkable floors.
        float floorThickness = 1.05f;
        float floorRatio = 0.12f;
        // Crop to the scene's navigation-mesh bounds when one exists.
        bool cropToNavigation = true;
        // Entities (and their children) with any of these tags are left off the map.
        std::vector<std::string> excludeTags{"MapIgnore"};
        MapBakeStyle style;
    };

    // North-up square: world -Z is the image's top row and +X its right edge.
    struct MapBakeResult
    {
        bool success = false;
        std::string message;
        int obstacleCount = 0;
        int floorCount = 0;
        float centreX = 0.0f;
        float centreZ = 0.0f;
        float worldSize = 0.0f;
        int size = 0;
        std::vector<std::uint8_t> rgba; // size * size, straight alpha, row 0 = north.
    };

    // Rasterises solid (non-trigger) box colliders of active entities into an outline map.
    MapBakeResult BakeTopDownMap(const Scene &scene, const MapBakeSettings &settings = {});

    // Uncompressed 32-bit top-left-origin TGA, the format the RHI UI texture loader reads.
    bool WriteTga(const std::filesystem::path &path, int width, int height, const std::vector<std::uint8_t> &rgba,
                  std::string *error = nullptr);

    // JSON consumed by PlutoGE.ScriptCore.Gameplay.MapLayout.
    std::string MakeMapLayoutJson(const std::string &name, const std::string &imageReference, float centreX, float centreZ,
                                  float worldSize);

    struct MapBakeOutput
    {
        std::filesystem::path image;
        std::filesystem::path layout;
        std::string imageReference;
        std::string layoutReference;
    };

    // Writes <folder>/<name>.tga and <name>.plutomap under the project's asset directory,
    // registers both and adds them to BUILD_ALWAYS_INCLUDE (games pick maps at runtime), then
    // saves the manifest. folder is asset-relative, e.g. "Maps".
    bool SaveMapBake(assets::Project &project, const MapBakeResult &result, const std::string &name,
                     const std::string &folder, MapBakeOutput *output = nullptr, std::string *error = nullptr);
}
