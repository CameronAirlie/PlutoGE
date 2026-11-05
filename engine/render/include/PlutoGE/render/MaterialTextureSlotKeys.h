#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// The material texture slots and their .plutomaterial keys, without rendering dependencies, for code that only reads
// or rewrites material files (dependency scanning, migration). MaterialTextureSlots.h has the full slot table.
namespace PlutoGE::render
{
    // Every texture a standard material can reference. Order is not serialized (assets use each slot's asset key),
    // so slots may be inserted anywhere; append new ones before Count.
    enum class MaterialTextureSlot : std::uint8_t
    {
        Albedo,
        Normal,
        Metallic,
        Roughness,
        Emission,
        Occlusion,
        ClearCoat,
        ClearCoatRoughness,
        SheenColor,
        SheenRoughness,
        DiffuseTransmission,
        Count,
    };

    inline constexpr std::size_t MaterialTextureSlotCount = static_cast<std::size_t>(MaterialTextureSlot::Count);

    // The .plutomaterial key holding each slot's texture path, in slot order.
    inline constexpr std::array<std::string_view, MaterialTextureSlotCount> MaterialTextureAssetKeys{
        "AlbedoTexture", "NormalTexture", "MetallicTexture", "RoughnessTexture", "EmissionTexture", "OcclusionTexture",
        "ClearCoatTexture", "ClearCoatRoughnessTexture", "SheenColorTexture", "SheenRoughnessTexture",
        "DiffuseTransmissionTexture"};

    constexpr bool IsMaterialTextureAssetKey(std::string_view key)
    {
        for (const auto assetKey : MaterialTextureAssetKeys)
            if (assetKey == key) return true;
        return false;
    }
}
