#pragma once

#include "PlutoGE/render/BakedLighting.h"

#include <filesystem>
#include <optional>
#include <string>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::scene
{
    // A .plutolightmap file: one lightmap atlas (render::LightmapAtlas) as written by a bake.
    //   "PLMAP\0\0\0", uint32 version (1), uint32 encoding, uint32 width, uint32 height,
    //   width * height * 4 half floats (RGBA, little-endian, rows from UV v = 0).
    // Readers reject other versions, unknown encodings and sizes above kMaxLightmapAtlasSize.
    inline constexpr std::uint32_t kLightmapFileVersion = 1;
    inline constexpr std::uint32_t kMaxLightmapAtlasSize = 8192;
    inline constexpr const char *kLightmapFileExtension = ".plutolightmap";

    bool WriteLightmapFile(const std::filesystem::path &path, const render::LightmapAtlas &atlas, std::string *error = nullptr);
    std::optional<render::LightmapAtlas> ReadLightmapFile(const std::filesystem::path &path, std::string *error = nullptr);

    // Writes <asset directory>/<folder>/<name>.plutolightmap into the project, gives it a stable identity sidecar on
    // first write, registers it and includes it in builds (it is only referenced from scene data). Returns its
    // project:// reference.
    std::optional<std::string> SaveLightmapAsset(assets::Project &project, const render::LightmapAtlas &atlas,
                                                 const std::string &name, const std::string &folder = "Lightmaps",
                                                 std::string *error = nullptr);
}
