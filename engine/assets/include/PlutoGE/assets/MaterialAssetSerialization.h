#pragma once

#include "PlutoGE/render/Material.h"

#include <iosfwd>
#include <string>

namespace PlutoGE::assets
{
    struct MaterialTextureReferences
    {
        std::string albedo;
        std::string normal;
        std::string metallic;
        std::string roughness;
        std::string emission;
    };

    // CPU-only serialization shared by authored saves and import workers.
    // Texture pointers and compiled shader objects are not accessed here.
    bool WriteMaterialAsset(std::ostream &output, const render::MaterialConfig &config,
                            const MaterialTextureReferences &textures, std::string *errorMessage = nullptr);
}
