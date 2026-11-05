#include "PlutoGE/assets/MaterialAssetSerialization.h"
#include "PlutoGE/assets/Project.h"

#include <ostream>
#include <unordered_set>

namespace PlutoGE::assets
{
    bool WriteMaterialAsset(std::ostream &output, const render::MaterialConfig &config,
                            const MaterialTextureReferences &textures, std::string *errorMessage)
    {
        auto fail = [&]
        {
            if (errorMessage) *errorMessage = "Invalid material serialization fields or stream write failure.";
            return false;
        };
        for (const auto &reference : textures.paths)
            if (reference.find_first_of("\r\n\0", 0, 3) != std::string::npos) return fail();
        if (config.shaderGraphReference.find_first_of("\r\n\0", 0, 3) != std::string::npos) return fail();
        std::unordered_set<std::string> textureNames;
        for (const auto &texture : config.shaderGraphTextures)
            if (texture.name.empty() || !textureNames.insert(texture.name).second ||
                texture.name.find_first_of("|\r\n\0", 0, 4) != std::string::npos ||
                texture.reference.find_first_of("|\r\n\0", 0, 4) != std::string::npos) return fail();
        for (const auto &variable : config.shaderGraphVariables)
            if (variable.name.empty() || variable.name.find_first_of("|\r\n\0", 0, 4) != std::string::npos) return fail();
        output << "Color=" << config.color.r << "," << config.color.g << "," << config.color.b << "," << config.color.a << "\n";
        output << "SurfaceType=" << (config.surfaceType == render::MaterialSurfaceType::Glass ? "Glass" : "Standard") << "\n";
        output << "AlphaMode=" << (config.alphaMode == render::AlphaMode::Blend ? "Blend" : config.alphaMode == render::AlphaMode::Mask ? "Mask"
                                                                                                                                        : "Opaque")
               << "\n";
        output << "AlphaCutoff=" << config.alphaCutoff << "\n";
        output << "CastsShadow=" << (config.castsShadow ? "true" : "false") << "\n";
        output << "TwoSided=" << (config.twoSided ? "true" : "false") << "\n";
        output << "UvScale=" << config.uvScale.x << "," << config.uvScale.y << "\n";
        output << "Metallic=" << config.metallic << "\n";
        output << "Roughness=" << config.roughness << "\n";
        output << "EmissionChannelMask=" << (config.emissionChannelMask ? "true" : "false") << "\n";
        const char *emissionChannelNames[]{"EmissionRed", "EmissionGreen", "EmissionBlue"};
        for (int i = 0; i < 3; ++i)
        {
            const auto &c = config.emissionChannels[i];
            output << emissionChannelNames[i] << "=" << c.r << "," << c.g << "," << c.b << "," << c.a << "\n";
        }
        output << "EmissionTexCoord=" << config.emissionTexCoord << "\n";
        output << "Emission=" << config.emission.r << "," << config.emission.g << "," << config.emission.b << "\n";
        output << "Subsurface=" << config.subsurface << "\n";
        output << "SubsurfaceColor=" << config.subsurfaceColor.r << "," << config.subsurfaceColor.g << "," << config.subsurfaceColor.b << "\n";
        output << "SubsurfaceRadius=" << config.subsurfaceRadius << "\n";
        if (config.diffuseTransmission > 0.0f)
        {
            output << "DiffuseTransmission=" << config.diffuseTransmission << "\n";
            output << "DiffuseTransmissionColor=" << config.diffuseTransmissionColor.r << "," << config.diffuseTransmissionColor.g << "," << config.diffuseTransmissionColor.b << "\n";
            output << "DiffuseTransmissionAlbedoTint=" << config.diffuseTransmissionAlbedoTint << "\n";
        }
        output << "Transmission=" << config.transmission << "\n";
        output << "Ior=" << config.ior << "\n";
        output << "Thickness=" << config.thickness << "\n";
        output << "AttenuationColor=" << config.attenuationColor.r << "," << config.attenuationColor.g << "," << config.attenuationColor.b << "\n";
        output << "AttenuationDistance=" << config.attenuationDistance << "\n";
        output << "FlipNormalY=" << (config.flipNormalY ? "true" : "false") << "\n";
        output << "OcclusionStrength=" << config.occlusionStrength << "\n";
        output << "ClearCoat=" << config.clearCoat << "\n";
        output << "ClearCoatRoughness=" << config.clearCoatRoughness << "\n";
        output << "SheenColor=" << config.sheenColor.r << "," << config.sheenColor.g << "," << config.sheenColor.b << "\n";
        output << "SheenRoughness=" << config.sheenRoughness << "\n";
        for (const auto &slot : render::MaterialTextureSlots())
        {
            output << slot.assetKey << "=" << textures[slot.slot] << "\n";
            if (slot.channel) output << slot.channelKey << "=" << static_cast<int>(config.*slot.channel) << "\n";
        }
        output << "ShaderGraph=" << (config.shaderGraphReference.empty() ? std::string(Project::kBuiltinDefaultShaderGraphReference) : config.shaderGraphReference) << "\n";
        for(const auto &t:config.shaderGraphTextures)output<<"ShaderGraphTexture="<<t.name<<'|'<<t.reference<<'\n';
        for (const auto &variable : config.shaderGraphVariables)
        {
            output << "ShaderGraphVariable=" << variable.name << '|'
                   << static_cast<int>(variable.type) << '|'
                   << variable.value.x << ',' << variable.value.y << ',' << variable.value.z << ',' << variable.value.w << "\n";
        }

        if (!output.good()) return fail();
        if (errorMessage) errorMessage->clear();
        return true;
    }
}
