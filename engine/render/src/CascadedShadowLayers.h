#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <algorithm>

namespace PlutoGE::render
{
    // Stability is observed from validated content signatures, never inferred
    // from object addresses or an authoring Static flag. Changed entries return
    // to the dynamic layer immediately; removals change the layer content key.
    class CascadedShadowLayers
    {
        struct History { std::uint64_t signature = 0; unsigned stableFrames = 0; };
        std::vector<History> m_history;
      public:
        struct Cascade
        {
            rhi::Texture color, depth;
            std::uint64_t content = 0, previousProjection = 0;
            unsigned resolution = 0;
            bool valid = false;
            unsigned stableProjectionFrames = 0;
            std::vector<std::size_t> stationary, moving;
        };
        std::array<Cascade, 4> cascades;
        // Two 32-bit targets per cached cascade. Keep secondary cameras and
        // high-resolution shadow settings from allocating unbounded caches.
        static constexpr std::uint64_t MaxCacheBytes = 64ull * 1024 * 1024;
        bool CanAllocate(unsigned index, unsigned resolution) const
        {
            std::uint64_t bytes = std::uint64_t(resolution) * resolution * 8;
            for (unsigned i = 0; i < cascades.size(); ++i)
                if (i != index && cascades[i].depth)
                    bytes += std::uint64_t(cascades[i].resolution) * cascades[i].resolution * 8;
            return bytes <= MaxCacheBytes;
        }
        void Observe(std::span<const std::uint64_t> signatures)
        {
            m_history.resize(signatures.size());
            for (std::size_t i = 0; i < signatures.size(); ++i)
            {
                auto &entry = m_history[i];
                entry.stableFrames = entry.signature == signatures[i] ? std::min(entry.stableFrames + 1, 2u) : 0;
                entry.signature = signatures[i];
            }
        }
        bool Partition(unsigned index, std::uint64_t projection, std::span<const std::size_t> visible)
        {
            auto &cascade = cascades[index];
            cascade.stationary.clear(); cascade.moving.clear();
            for (const auto caster : visible)
                (m_history[caster].stableFrames >= 2 ? cascade.stationary : cascade.moving).push_back(caster);
            const bool fixedProjection = cascade.previousProjection == projection;
            cascade.stableProjectionFrames = fixedProjection ? std::min(cascade.stableProjectionFrames + 1, 2u) : 0;
            cascade.previousProjection = projection;
            // Avoid a full-map seed for small or predominantly dynamic scenes.
            return cascade.stableProjectionFrames >= 2 && !cascade.moving.empty() && cascade.stationary.size() >= 64 &&
                cascade.stationary.size() >= cascade.moving.size() * 4;
        }
        void Invalidate() { for (auto &cascade : cascades) cascade.valid = false; }
    };
}
