#pragma once

#include "InstanceStorage.h"
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/OcclusionCulling.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PlutoGE::render
{
    // Per-instance culling and LOD binning of instanced draws with LOD chains, on the GPU (InstanceCulling.slang): the
    // compute counterpart of BasicRenderer's CPU binning, with the same selection (BasicInstanceLodChain::Select), plus
    // occlusion against the previous frame's depth hierarchy. A frame's bins and their indirect draw arguments stay on
    // the GPU: a draw issues one indirect draw per LOD (or, with first instances, one batch for all of its LODs), so
    // neither the CPU's per-instance loop nor the bin upload remains. Chains of more than MaxLods LODs stay on the CPU.
    class GpuInstanceCulling
    {
    public:
        static constexpr std::uint32_t MaxLods = 8;

        using Shaders = BasicInstanceCullingShaderPackage;

        // One instanced draw to bin: its chain and instances, and where its records are.
        struct Source
        {
            const BasicDraw *draw = nullptr;
            InstanceStorage::Range records;
        };

        // The camera the bins are for.
        struct View
        {
            glm::mat4 viewProjection{1.0f};
            glm::vec3 cameraPosition{0.0f};
            float lodProjection = 0.0f; // pixels per unit of radius over distance
            // The previous frame's depth hierarchy, or none (no occlusion).
            const OcclusionDepthHierarchy *hierarchy = nullptr;
        };

        // A binned draw: its slots (one per LOD), whose arguments are in Arguments() at firstSlot * ArgumentStride.
        struct Bins
        {
            std::uint32_t firstSlot = 0;
            std::uint32_t lodCount = 0;
        };
        static constexpr std::size_t ArgumentStride = 20;

        bool Initialize(rhi::IRenderDevice &device, const Shaders &shaders);
        [[nodiscard]] bool Available() const noexcept { return static_cast<bool>(m_count); }
        // Whether the GPU can bin this draw (its chain has at most MaxLods LODs).
        [[nodiscard]] static bool Supports(const BasicDraw &draw) noexcept;

        // Uploads the frame's tables and records the three passes into `commands`, outside rendering and before any
        // draw reads the bins. Indirect arguments carry absolute first indices (BasicMesh::BaseIndex added, none for
        // impostor cards), so draws bind their index buffer at offset zero.
        void Record(rhi::ICommandContext &commands, std::span<const Source> sources, const View &view);
        // The bins Record made for a draw this frame, or null.
        [[nodiscard]] const Bins *Find(const BasicDraw &draw) const;
        [[nodiscard]] rhi::BufferHandle IndexBuffer() const noexcept { return m_indices.Get(); }
        [[nodiscard]] rhi::BufferHandle Arguments() const noexcept { return m_arguments.Get(); }
        // Whether the arguments carry each bin's offset as first instance (Vulkan with indirect first instances), so a
        // draw can batch its LODs; otherwise vertex shaders find a bin's offset in the index buffer's header.
        [[nodiscard]] bool UsesFirstInstance() const noexcept { return m_firstInstance; }

    private:
        struct Key
        {
            const void *models = nullptr, *chain = nullptr;
            bool operator==(const Key &) const = default;
        };
        struct KeyHash
        {
            std::size_t operator()(const Key &key) const noexcept
            {
                return std::hash<const void *>{}(key.models) ^ (std::hash<const void *>{}(key.chain) * 31u);
            }
        };
        static void Grow(rhi::IRenderDevice &device, rhi::Buffer &buffer, std::size_t &capacity, std::size_t bytes,
                         rhi::BufferUsage usage, const char *name);

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_count, m_scan, m_scatter; // compute pipelines
        rhi::Buffer m_draws, m_counters, m_indices, m_arguments, m_placeholder;
        std::size_t m_drawCapacity = 0, m_counterCapacity = 0, m_indexCapacity = 0, m_argumentCapacity = 0;
        std::vector<rhi::Buffer> m_dispatchParameters;
        std::unordered_map<Key, Bins, KeyHash> m_bins;
        bool m_firstInstance = false;
    };
}
