#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <span>
#include <tuple>
#include <vector>

namespace PlutoGE::render
{
    // A traced instance's surface, indexed by the hit's instance ID (RayTracedInstance in DDGIRayQueryTrace.slang).
    struct alignas(16) RayTracedInstance
    {
        glm::vec4 albedo{0.0f};   // rgb diffuse reflectance
        glm::vec4 emission{0.0f}; // rgb emitted radiance
    };
    static_assert(sizeof(RayTracedInstance) == 32);

    // The triangles hardware ray queries trace: a bottom-level acceleration structure per mesh range (built once per
    // geometry revision, released when unused for a while) and a top-level structure rebuilt each frame from the
    // draws given, with a record per instance. Like GI, it leaves out GPU-skinned and transparent geometry; surfaces
    // are their material's constant colour and emission (textures are not sampled at hits).
    class RayTracingScene
    {
    public:
        // Bottom levels built per frame at most, so a level appearing does not stall one frame.
        static constexpr std::size_t BuildBudget = 64;
        // Frames a bottom level stays alive unused.
        static constexpr std::uint64_t RetainFrames = 240;

        // False (and unavailable) without hardware ray tracing.
        bool Initialize(rhi::IRenderDevice &device);
        void Shutdown();
        [[nodiscard]] bool Available() const noexcept { return m_device != nullptr; }
        // Builds the bottom levels `draws` need (within the budget) and the top level over every traceable instance
        // whose bottom level exists, recording the builds outside rendering. False when nothing can be traced.
        bool Update(rhi::ICommandContext &commands, std::span<const BasicDraw> draws);
        [[nodiscard]] rhi::AccelerationStructureHandle TopLevel() const noexcept { return m_topLevel.Get(); }
        [[nodiscard]] rhi::BufferHandle Instances() const noexcept { return m_instanceRecords.Get(); }
        [[nodiscard]] std::size_t InstanceCount() const noexcept { return m_instanceCount; }
        [[nodiscard]] std::size_t BottomLevelCount() const noexcept { return m_bottomLevels.size(); }
        // Whether ray queries see `draw`: static opaque geometry that contributes to GI.
        [[nodiscard]] static bool Traceable(const BasicDraw &draw) noexcept;

    private:
        // Geometry identity: buffers (with generations), the range and the mesh revision.
        using Key = std::tuple<std::uint64_t, std::uint64_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint64_t>;
        struct BottomLevel
        {
            rhi::AccelerationStructure structure;
            std::uint64_t lastUsed = 0;
        };

        rhi::IRenderDevice *m_device = nullptr;
        std::map<Key, BottomLevel> m_bottomLevels;
        rhi::AccelerationStructure m_topLevel;
        std::uint32_t m_topLevelCapacity = 0;
        rhi::Buffer m_instanceRecords;
        std::size_t m_recordCapacity = 0;
        std::size_t m_instanceCount = 0;
        std::vector<rhi::AccelerationStructureInstance> m_instances;
        std::vector<RayTracedInstance> m_records;
        std::uint64_t m_frame = 0;
    };
}
