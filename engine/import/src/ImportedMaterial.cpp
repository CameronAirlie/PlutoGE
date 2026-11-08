#include "PlutoGE/import/MeshImporter.h"

namespace PlutoGE::assetimport
{
    bool ImportedMaterialParametersEqual(const ImportedMaterialData &a, const ImportedMaterialData &b)
    {
        return a.color == b.color &&
               a.surfaceType == b.surfaceType && a.alphaMode == b.alphaMode &&
               a.alphaCutoff == b.alphaCutoff && a.castsShadow == b.castsShadow && a.twoSided == b.twoSided &&
               a.metallic == b.metallic && a.roughness == b.roughness && a.emission == b.emission &&
               a.emissionTextureIndex == b.emissionTextureIndex && a.emissionTexCoord == b.emissionTexCoord &&
               a.subsurface == b.subsurface && a.subsurfaceColor == b.subsurfaceColor &&
               a.subsurfaceRadius == b.subsurfaceRadius && a.diffuseTransmission == b.diffuseTransmission &&
               a.diffuseTransmissionColor == b.diffuseTransmissionColor &&
               a.diffuseTransmissionAlbedoTint == b.diffuseTransmissionAlbedoTint &&
               a.diffuseTransmissionTextureIndex == b.diffuseTransmissionTextureIndex && a.transmission == b.transmission &&
               a.ior == b.ior && a.thickness == b.thickness &&
               a.attenuationColor == b.attenuationColor && a.attenuationDistance == b.attenuationDistance &&
               a.albedoTextureIndex == b.albedoTextureIndex && a.normalTextureIndex == b.normalTextureIndex &&
               a.metallicRoughnessTextureIndex == b.metallicRoughnessTextureIndex &&
               a.metallicRoughnessTextureHasMetallicChannel == b.metallicRoughnessTextureHasMetallicChannel &&
               a.occlusionTextureIndex == b.occlusionTextureIndex && a.occlusionStrength == b.occlusionStrength &&
               a.clearCoat == b.clearCoat && a.clearCoatRoughness == b.clearCoatRoughness &&
               a.clearCoatTextureIndex == b.clearCoatTextureIndex &&
               a.clearCoatRoughnessTextureIndex == b.clearCoatRoughnessTextureIndex &&
               a.sheenColor == b.sheenColor && a.sheenRoughness == b.sheenRoughness &&
               a.sheenColorTextureIndex == b.sheenColorTextureIndex &&
               a.sheenRoughnessTextureIndex == b.sheenRoughnessTextureIndex &&
               a.flipNormalY == b.flipNormalY;
    }

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
