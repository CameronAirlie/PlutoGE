#pragma once

#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/MaterialTextureSlots.h"

#include <array>
#include <iosfwd>
#include <string>

namespace PlutoGE::assets
{
    // Persisted texture references of a material, one per texture slot (empty when the slot has no texture).
    struct MaterialTextureReferences
    {
        std::array<std::string, render::MaterialTextureSlotCount> paths;

        std::string &operator[](render::MaterialTextureSlot slot) { return paths[static_cast<std::size_t>(slot)]; }
        const std::string &operator[](render::MaterialTextureSlot slot) const
        {
            return paths[static_cast<std::size_t>(slot)];
        }
    };

    // CPU-only serialization shared by authored saves and import workers.
    // Texture pointers and compiled shader objects are not accessed here.
    bool WriteMaterialAsset(std::ostream &output, const render::MaterialConfig &config,
                            const MaterialTextureReferences &textures, std::string *errorMessage = nullptr);
}
