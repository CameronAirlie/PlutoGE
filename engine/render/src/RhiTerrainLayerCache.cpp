#include "PlutoGE/render/RhiTerrainLayerCache.h"

#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/rhi/RenderDevice.h"

#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        void Mix(std::uint64_t &hash, std::uint64_t value)
        {
            hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        }
    }

    std::uint64_t RhiTerrainLayerCache::Signature(const TerrainLayerSet &set)
    {
        std::uint64_t hash = set.layers.size();
        for (const auto &layer : set.layers)
            for (const auto *texture : {layer.albedoMap, layer.normalMap, layer.roughnessMap, layer.heightMap})
            {
                Mix(hash, texture ? texture->GetIdentity() : 0);
                Mix(hash, texture ? texture->GetContentRevision() : 0);
            }
        return hash;
    }

    std::shared_ptr<const RhiTerrainLayerCache::Arrays> RhiTerrainLayerCache::Prepare(
        rhi::IRenderDevice &device, const TerrainLayerSet &set, const TerrainTexturePixels &pixels)
    {
        if (set.layers.empty())
            return nullptr;
        const auto signature = Signature(set);
        if (const auto found = m_entries.find(signature); found != m_entries.end())
            return found->second;
        const auto built = BuildTerrainLayerArrays(set, pixels);
        if (built.layers == 0)
            return nullptr;
        const auto create = [&](rhi::Format format, const std::vector<std::byte> &data, const char *name) {
            rhi::TextureDescriptor descriptor{built.size, built.size, format, rhi::TextureUsage::Sampled, name};
            descriptor.arrayLayers = built.layers;
            return rhi::Texture(device, device.CreateTexture(descriptor, data));
        };
        auto arrays = std::make_shared<Arrays>();
        arrays->layers = built.layers;
        arrays->albedo = create(rhi::Format::R8G8B8A8Srgb, built.albedo, "Terrain layer albedo and height");
        arrays->normal = create(rhi::Format::R8G8B8A8Unorm, built.normal, "Terrain layer normals and roughness");
        if (!arrays->albedo || !arrays->normal)
            return nullptr;
        return m_entries.insert_or_assign(signature, std::move(arrays)).first->second;
    }

    void RhiTerrainLayerCache::Collect()
    {
        std::erase_if(m_entries, [](const auto &entry) { return entry.second.use_count() == 1; });
    }
}
