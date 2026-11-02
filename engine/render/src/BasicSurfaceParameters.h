#pragma once

#include "PlutoGE/render/BasicRenderer.h"

#include <algorithm>

namespace PlutoGE::render
{
    // The material parameter lanes for a draw's optional surface textures and layers, shared by the bound
    // (BasicMaterialParameters) and bindless (BindlessMaterialRecord) paths so both shade a material alike.
    // BasicLit.slang's surfaceTexturePresent / surfaceTextureChannel decode them.
    struct BasicSurfaceParameters
    {
        // x: bit i set when BasicSurfaceTexture i is bound; y: its channel in bits 2i..2i+1.
        glm::uvec4 textures{0u};
        glm::vec4 occlusion{0.0f}; // x strength
        glm::vec4 clearCoat{0.0f}; // x factor, y roughness
        glm::vec4 sheen{0.0f};     // rgb colour, a roughness
    };

    inline BasicSurfaceParameters MakeBasicSurfaceParameters(const BasicDraw &draw)
    {
        static_assert(BasicSurfaceTextureCount <= 16, "Channels pack two bits per surface texture");
        BasicSurfaceParameters parameters;
        for (std::size_t index = 0; index < BasicSurfaceTextureCount; ++index)
        {
            if (!draw.surfaceTextures[index]) continue;
            parameters.textures.x |= 1u << index;
            parameters.textures.y |= (draw.surfaceTextureChannels[index] & 3u) << (2 * index);
        }
        parameters.occlusion.x = std::clamp(draw.occlusionStrength, 0.0f, 1.0f);
        parameters.clearCoat = {std::clamp(draw.clearCoat, 0.0f, 1.0f), std::clamp(draw.clearCoatRoughness, 0.0f, 1.0f),
                                0.0f, 0.0f};
        parameters.sheen = {glm::clamp(draw.sheenColor, glm::vec3(0.0f), glm::vec3(1.0f)),
                            std::clamp(draw.sheenRoughness, 0.0f, 1.0f)};
        return parameters;
    }
}
