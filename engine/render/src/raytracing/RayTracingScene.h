#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

namespace PlutoGE::render
{
    // A traced draw's surface, indexed by the hit's instance custom index (RayTracedInstance in
    // DDGIRayQueryTrace.slang). Every instance of a draw shares its draw's record.
    struct alignas(16) RayTracedInstance
    {
        glm::vec4 albedo{0.0f};   // rgb diffuse reflectance
        glm::vec4 emission{0.0f}; // rgb emitted radiance
    };
    static_assert(sizeof(RayTracedInstance) == 32);

    // The triangles hardware ray queries trace: a bottom-level acceleration structure per mesh range (built once per
    // geometry revision, released when unused for a while) and a top-level structure over the draws given, with a
    // record per draw. The top level is rebuilt only when the traced draws change: retained draw packets
    // (BasicDraw::preparationRevision) identify unchanged ones, so a static scene builds it once. Like GI, it leaves
    // out GPU-skinned and transparent geometry; surfaces are their material's mean diffuse reflectance and emission
    // over its textures (BasicDraw::meanDiffuse), as textures are not sampled at hits.
    class RayTracingScene
    {
    public:
        // Bottom levels built per frame at most, so a level appearing does not stall one frame.
        static constexpr std::size_t BuildBudget = 64;
        // Top-level builds a bottom level stays alive unused.
        static constexpr std::uint64_t RetainFrames = 240;

        // Which draws are traced.
        struct Settings
        {
            // Draws whose largest instance's bounding radius (BasicDraw::maxInstanceRadius) is below this, in world
            // units, are left out: grass and debris add top-level instances far out of proportion to the light they
            // bounce or block. 0 traces every draw.
            float minimumInstanceRadius = 0.0f;
            bool operator==(const Settings &) const = default;
        };

        // False (and unavailable) without hardware ray tracing.
        bool Initialize(rhi::IRenderDevice &device);
        void Shutdown();
        [[nodiscard]] bool Available() const noexcept { return m_device != nullptr; }
        // Takes effect at the next Update, which then rebuilds the top level.
        void SetSettings(const Settings &settings) noexcept { m_settings = settings; }
        [[nodiscard]] const Settings &GetSettings() const noexcept { return m_settings; }
        // Builds the bottom levels `draws` need (within the budget) and, when the traced draws changed, the top level
        // over every traced instance whose bottom level exists, recording the builds outside rendering. False when
        // nothing can be traced.
        bool Update(rhi::ICommandContext &commands, std::span<const BasicDraw> draws);
        [[nodiscard]] rhi::AccelerationStructureHandle TopLevel() const noexcept { return m_topLevel.Get(); }
        [[nodiscard]] rhi::BufferHandle Instances() const noexcept { return m_instanceRecords.Get(); }
        [[nodiscard]] std::size_t InstanceCount() const noexcept { return m_instanceCount; }
        // Instances of traceable draws the settings left out at the last top-level build.
        [[nodiscard]] std::size_t SkippedInstanceCount() const noexcept { return m_skippedInstanceCount; }
        // Whether the last Update rebuilt the top level (false when it reused the previous one).
        [[nodiscard]] bool Rebuilt() const noexcept { return m_rebuilt; }
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

        // Traceable, and not too small for the settings.
        [[nodiscard]] bool Traced(const BasicDraw &draw) const noexcept;
        // Identifies the traced input: every traced draw's retained packet with its geometry and surface. None when a
        // traced draw is not retained (its packet may change in place), which forces a rebuild.
        [[nodiscard]] std::optional<std::uint64_t> InputSignature(std::span<const BasicDraw> draws) const;

        rhi::IRenderDevice *m_device = nullptr;
        Settings m_settings;
        std::map<Key, BottomLevel> m_bottomLevels;
        rhi::AccelerationStructure m_topLevel;
        std::uint32_t m_topLevelCapacity = 0;
        rhi::Buffer m_instanceRecords;
        std::size_t m_recordCapacity = 0;
        std::size_t m_instanceCount = 0, m_skippedInstanceCount = 0;
        std::vector<rhi::AccelerationStructureInstance> m_instances;
        std::vector<RayTracedInstance> m_records;
        // The last top level's input, valid when it held every traced draw (no bottom level was deferred).
        std::optional<std::uint64_t> m_builtSignature;
        bool m_rebuilt = false;
        std::uint64_t m_frame = 0; // top-level builds
    };
}
