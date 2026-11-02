#pragma once
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/MaterialTextureSlotKeys.h"
#include "PlutoGE/render/Texture.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace PlutoGE::render
{
    // How one slot is stored, persisted and presented. Code that handles textures as a list (serialization,
    // dependency scanning, migration, editors, import) iterates MaterialTextureSlots() instead of naming fields, so a
    // new slot needs only an entry here, its MaterialConfig fields and its GPU binding.
    struct MaterialTextureSlotInfo
    {
        MaterialTextureSlot slot;
        std::string_view assetKey;   // .plutomaterial key holding the texture path
        std::string_view channelKey; // key holding the sampled channel; empty when the slot reads all channels
        std::string_view displayName;
        std::string_view propertyName; // component property stem: "<name>Path", "<name>Channel"
        TextureColorSpace colorSpace;
        Texture *MaterialConfig::*texture;
        TextureChannel MaterialConfig::*channel; // null when channelKey is empty
    };

    inline constexpr std::array<MaterialTextureSlotInfo, MaterialTextureSlotCount> MaterialTextureSlotTable{{
        {MaterialTextureSlot::Albedo, "AlbedoTexture", {}, "Albedo", "Albedo", TextureColorSpace::SRGB,
         &MaterialConfig::albedoTexture, nullptr},
        {MaterialTextureSlot::Normal, "NormalTexture", {}, "Normal", "Normal", TextureColorSpace::Linear,
         &MaterialConfig::normalTexture, nullptr},
        {MaterialTextureSlot::Metallic, "MetallicTexture", "MetallicTextureChannel", "Metallic", "Metallic", TextureColorSpace::Linear,
         &MaterialConfig::metallicTexture, &MaterialConfig::metallicTextureChannel},
        {MaterialTextureSlot::Roughness, "RoughnessTexture", "RoughnessTextureChannel", "Roughness", "Roughness",
         TextureColorSpace::Linear, &MaterialConfig::roughnessTexture, &MaterialConfig::roughnessTextureChannel},
        {MaterialTextureSlot::Emission, "EmissionTexture", {}, "Emission", "Emission", TextureColorSpace::SRGB,
         &MaterialConfig::emissionTexture, nullptr},
        {MaterialTextureSlot::Occlusion, "OcclusionTexture", "OcclusionTextureChannel", "Occlusion", "Occlusion",
         TextureColorSpace::Linear, &MaterialConfig::occlusionTexture, &MaterialConfig::occlusionTextureChannel},
        {MaterialTextureSlot::ClearCoat, "ClearCoatTexture", "ClearCoatTextureChannel", "Clear Coat", "ClearCoat",
         TextureColorSpace::Linear, &MaterialConfig::clearCoatTexture, &MaterialConfig::clearCoatTextureChannel},
        {MaterialTextureSlot::ClearCoatRoughness, "ClearCoatRoughnessTexture", "ClearCoatRoughnessTextureChannel",
         "Clear Coat Roughness", "ClearCoatRoughness", TextureColorSpace::Linear, &MaterialConfig::clearCoatRoughnessTexture,
         &MaterialConfig::clearCoatRoughnessTextureChannel},
        {MaterialTextureSlot::SheenColor, "SheenColorTexture", {}, "Sheen Color", "SheenColor", TextureColorSpace::SRGB,
         &MaterialConfig::sheenColorTexture, nullptr},
        {MaterialTextureSlot::SheenRoughness, "SheenRoughnessTexture", "SheenRoughnessTextureChannel", "Sheen Roughness",
         "SheenRoughness", TextureColorSpace::Linear, &MaterialConfig::sheenRoughnessTexture,
         &MaterialConfig::sheenRoughnessTextureChannel},
    }};

    constexpr const std::array<MaterialTextureSlotInfo, MaterialTextureSlotCount> &MaterialTextureSlots()
    {
        return MaterialTextureSlotTable;
    }

    constexpr const MaterialTextureSlotInfo &GetMaterialTextureSlotInfo(MaterialTextureSlot slot)
    {
        return MaterialTextureSlotTable[static_cast<std::size_t>(slot)];
    }

    // The slot whose asset key or channel key is `key`, or null.
    constexpr const MaterialTextureSlotInfo *FindMaterialTextureSlotByAssetKey(std::string_view key)
    {
        for (const auto &info : MaterialTextureSlotTable)
            if (info.assetKey == key) return &info;
        return nullptr;
    }

    constexpr const MaterialTextureSlotInfo *FindMaterialTextureSlotByChannelKey(std::string_view key)
    {
        for (const auto &info : MaterialTextureSlotTable)
            if (!info.channelKey.empty() && info.channelKey == key) return &info;
        return nullptr;
    }

    inline Texture *GetMaterialTexture(const MaterialConfig &config, MaterialTextureSlot slot)
    {
        return config.*(GetMaterialTextureSlotInfo(slot).texture);
    }

    inline void SetMaterialTexture(MaterialConfig &config, MaterialTextureSlot slot, Texture *texture)
    {
        config.*(GetMaterialTextureSlotInfo(slot).texture) = texture;
    }

    // The colour space a slot's texture must be decoded in. Emission is colour unless its channels are masks.
    constexpr TextureColorSpace GetMaterialTextureColorSpace(const MaterialConfig &config, MaterialTextureSlot slot)
    {
        if (slot == MaterialTextureSlot::Emission && config.emissionChannelMask) return TextureColorSpace::Linear;
        return GetMaterialTextureSlotInfo(slot).colorSpace;
    }

    static_assert([]
    {
        for (std::size_t i = 0; i < MaterialTextureSlotCount; ++i)
            if (static_cast<std::size_t>(MaterialTextureSlotTable[i].slot) != i ||
                MaterialTextureSlotTable[i].assetKey != MaterialTextureAssetKeys[i] ||
                MaterialTextureSlotTable[i].channelKey.empty() != (MaterialTextureSlotTable[i].channel == nullptr))
                return false;
        return true;
    }(), "MaterialTextureSlotTable must list every slot in enum order with its MaterialTextureAssetKeys key, and a "
                  "channel member exactly when it has a channel key");
}
