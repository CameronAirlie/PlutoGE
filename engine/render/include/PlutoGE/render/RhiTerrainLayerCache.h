#pragma once

#include "PlutoGE/render/TerrainLayers.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace PlutoGE::render
{
    // Device-owned GPU arrays for terrain layers (albedo + height, normal + roughness; see TerrainLayerArrayPixels).
    // Entries are keyed by the layers' textures (identity and content revision), not by the set: editing a layer's
    // tint or tiling makes a new set but reuses its arrays, and terrains sharing layer textures share one allocation.
    // Callers hold the returned arrays for as long as they draw with them; Collect frees what nobody holds.
    class RhiTerrainLayerCache
    {
    public:
        struct Arrays
        {
            rhi::Texture albedo, normal;
            std::uint32_t layers = 0;
        };

        // The arrays for `set`, built on first use or after one of its textures changed; null for an empty set.
        std::shared_ptr<const Arrays> Prepare(rhi::IRenderDevice &device, const TerrainLayerSet &set,
                                              const TerrainTexturePixels &pixels);
        void Collect();

        static std::uint64_t Signature(const TerrainLayerSet &set);

    private:
        std::unordered_map<std::uint64_t, std::shared_ptr<const Arrays>> m_entries;
    };
}
