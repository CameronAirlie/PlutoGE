#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace PlutoGE::assets
{
    struct TextureCookStats
    {
        std::size_t materials = 0;    // material files read
        std::size_t textures = 0;     // distinct texture files they name
        std::size_t encoded = 0;      // samplings encoded now
        std::size_t alreadyCooked = 0;
        std::size_t uncompressed = 0; // samplings the renderer keeps RGBA8 (small textures)
        std::size_t unreadable = 0;   // texture files that could not be read or decoded (left to the runtime)
        std::size_t failed = 0;       // encodes that could not be stored
    };

    // Block compresses every texture the material files under `projectRoot` name, for each way a material samples it
    // (render::GetMaterialTextureSampling), into `projectRoot` / render::kCookedTextureDirectory, where the runtime looks
    // before encoding (render/TextureCooking.h). `projectRoot` is a staged export: its PlutoAssetCatalog.manifest
    // resolves asset IDs as the runtime will. Unreadable textures are skipped (the runtime encodes them as it would
    // without cooking). False, with `errorMessage`, only when the store could not be written.
    bool CookMaterialTextures(const std::filesystem::path &projectRoot, const std::string &assetDirectory,
                              std::uint32_t assetPipelineVersion, TextureCookStats *stats = nullptr,
                              std::string *errorMessage = nullptr);
}
