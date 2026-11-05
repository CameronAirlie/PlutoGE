#include "PlutoGE/import/MeshImporter.h"

namespace PlutoGE::assetimport
{
    ImportedMaterialDefinition DescribeImportedMaterial(const ImportedMaterialData &material)
    {
        using render::MaterialTextureSlot;
        ImportedMaterialDefinition definition;
        definition.textureIndices.fill(-1);
        auto &config = definition.config;
        config.color = material.color;
        config.surfaceType = material.surfaceType;
        config.alphaMode = material.alphaMode;
        config.alphaCutoff = material.alphaCutoff;
        config.castsShadow = material.castsShadow;
        config.twoSided = material.twoSided;
        config.metallic = material.metallic;
        config.roughness = material.roughness;
        config.emission = material.emission;
        config.emissionTexCoord = material.emissionTexCoord;
        config.subsurface = material.subsurface;
        config.subsurfaceColor = material.subsurfaceColor;
        config.subsurfaceRadius = material.subsurfaceRadius;
        config.diffuseTransmission = material.diffuseTransmission;
        config.diffuseTransmissionColor = material.diffuseTransmissionColor;
        config.diffuseTransmissionAlbedoTint = material.diffuseTransmissionAlbedoTint;
        config.transmission = material.transmission;
        config.ior = material.ior;
        config.thickness = material.thickness;
        config.attenuationColor = material.attenuationColor;
        config.attenuationDistance = material.attenuationDistance;
        config.flipNormalY = material.flipNormalY;
        config.occlusionStrength = material.occlusionStrength;
        config.clearCoat = material.clearCoat;
        config.clearCoatRoughness = material.clearCoatRoughness;
        config.sheenColor = material.sheenColor;
        config.sheenRoughness = material.sheenRoughness;

        auto setTexture = [&](MaterialTextureSlot slot, int index)
        { definition.textureIndices[static_cast<std::size_t>(slot)] = index; };
        setTexture(MaterialTextureSlot::Albedo, material.albedoTextureIndex);
        setTexture(MaterialTextureSlot::Normal, material.normalTextureIndex);
        setTexture(MaterialTextureSlot::Emission, material.emissionTextureIndex);
        // glTF packs roughness in green and metallic in blue; some exporters leave blue empty.
        if (material.metallicRoughnessTextureIndex >= 0)
        {
            setTexture(MaterialTextureSlot::Roughness, material.metallicRoughnessTextureIndex);
            config.roughnessTextureChannel = render::TextureChannel::Green;
            if (material.metallicRoughnessTextureHasMetallicChannel)
            {
                setTexture(MaterialTextureSlot::Metallic, material.metallicRoughnessTextureIndex);
                config.metallicTextureChannel = render::TextureChannel::Blue;
            }
        }
        setTexture(MaterialTextureSlot::Occlusion, material.occlusionTextureIndex);
        config.occlusionTextureChannel = render::TextureChannel::Red;
        // KHR_materials_clearcoat and KHR_materials_sheen channel conventions.
        setTexture(MaterialTextureSlot::ClearCoat, material.clearCoatTextureIndex);
        config.clearCoatTextureChannel = render::TextureChannel::Red;
        setTexture(MaterialTextureSlot::ClearCoatRoughness, material.clearCoatRoughnessTextureIndex);
        config.clearCoatRoughnessTextureChannel = render::TextureChannel::Green;
        setTexture(MaterialTextureSlot::SheenColor, material.sheenColorTextureIndex);
        setTexture(MaterialTextureSlot::SheenRoughness, material.sheenRoughnessTextureIndex);
        // KHR_materials_diffuse_transmission keeps its factor in alpha.
        setTexture(MaterialTextureSlot::DiffuseTransmission, material.diffuseTransmissionTextureIndex);
        config.diffuseTransmissionTextureChannel = render::TextureChannel::Alpha;
        config.sheenRoughnessTextureChannel = render::TextureChannel::Alpha;
        return definition;
    }
}
